#include "native_tool_slots.hpp"
#include "native_owner_profile.hpp"
#include <algorithm>
#include <cstring>
#include <stdexcept>
namespace evrm { namespace {
using U=std::uint64_t;
void need(bool yes,const char*why){if(!yes)throw std::runtime_error(why);}
struct Reader {
 const NativeOwnerMemory& memory;const NativeOwnerImage& image;NativeToolSlotTarget& target;U started;size_t calls=0,bytes=0;
 Reader(const NativeOwnerMemory&m,const NativeOwnerImage&i,NativeToolSlotTarget&t):memory(m),image(i),target(t),started(m.milliseconds()){}
 bool inside(U a,size_t n)const{return a>=image.base&&n<=image.size&&a-image.base<=image.size-n;}
 std::vector<std::uint8_t> read(U a,size_t n,bool guard=false){const auto now=memory.milliseconds();need(now>=started&&now-started<20,"tool target lookup deadline");
  need(n&&n<=65536&&a>=0x10000&&a<0x800000000000ULL&&n<=0x800000000000ULL-a&&++calls<=8192&&(bytes+=n)<=4*1024*1024,"tool target read budget/range");
  std::vector<std::uint8_t>d(n);need(memory.read(a,d.data(),n),"tool target range unreadable");if(guard){need(target.guards.size()<128,"tool target guard bound");target.guards.push_back({a,d});}return d;}
 template<class T=U>T get(U a,bool guard=false){auto b=read(a,sizeof(T),guard);T value;std::memcpy(&value,b.data(),sizeof value);return value;}
 std::string text(U a,U n,bool guard=false){need(n&&n<=256&&inside(a,n),"tool target name outside original image");auto d=read(a,n,guard);while(!d.empty()&&!d.back())d.pop_back();need(!d.empty()&&std::find(d.begin(),d.end(),0)==d.end(),"tool target name invalid");return {d.begin(),d.end()};}
 void expected(U a,U value){need(get(a,true)==value,"local visual owner changed");}
 void expected32(U a,std::uint32_t value){need(get<std::uint32_t>(a,true)==value,"local visual entity identity changed");}
 void verify(){for(const auto&g:target.guards)need(read(g.address,g.bytes.size())==g.bytes,"tool target changed during lookup");}
};
std::uint32_t hash(const std::string&s){std::uint32_t h=2166136261u;for(unsigned char c:s)h=(h^c)*16777619u;return h;}
}
bool resolve_native_tool_slot_target(const NativeOwnerMemory&memory,const NativeOwnerCandidate&o,NativeToolSlotTarget&out,std::string&why)noexcept {
 out={};why.clear();try{namespace p=native_owner_profile;namespace e=p::ecsReadLayout;namespace s=p::serviceLayout;
  need(o.image.base>=0x10000&&o.image.size==p::image_size&&o.image.timestamp==p::image_timestamp&&
   std::equal(o.image.sha256.begin(),o.image.sha256.end(),std::begin(p::image_sha256))&&o.entity&&o.entity_id&&o.native_buffer&&!o.guards.empty(),"tool target has no supported owner");
  NativeToolSlotTarget target;Reader m(memory,o.image,target);
  m.expected(o.root,o.image.base+p::ownerLayout::rootVtableRva);m.expected(o.root+p::ownerLayout::clientPointerOffset,o.client);
  m.expected(o.client,o.image.base+p::ownerLayout::clientVtableRva);m.expected(o.client+p::ownerLayout::controllerPointerOffset,o.controller);
  m.expected(o.controller,o.image.base+p::ownerLayout::controllerVtableRva);m.expected(o.controller+p::ownerLayout::controllerClientBackPointerOffset,o.client);
  m.expected(o.controller+p::ownerLayout::controllerMainOwnerOffset,o.main);m.expected(o.main+e::mainExecutionOwnerOffset,o.execution);
  need(o.registry==o.execution+e::executionRegistryOwnerOffset,"tool target registry owner mismatch");
  m.expected32(o.entity+16,o.entity_id);m.expected32(o.entity+20,o.entity_generation);m.expected(o.entity+24,o.entity_layout);m.expected(o.entity+32,o.entity_storage);m.expected32(o.entity+48,o.entity_row);
  const auto records=m.get(o.registry+8,true),count=m.get(o.registry+16,true),capacity=m.get(o.registry+24,true);
  need(count&&count<=1024&&capacity>=count&&capacity<=4096,"tool target registry dimensions invalid");unsigned found=0,seen=0;
  for(U i=0;i<count;++i){const U row=records+i*256;const auto textptr=m.get(row+16),length=m.get(row+24);const auto name=m.text(textptr,length);
   const bool slots=name=="keen::ecs::AnimationGraphSlotComponent",transform=name=="keen::ecs::RenderTransform",
    equipment=name=="keen::ecs::EquipmentAnimationSets",ranged=name=="keen::ecs::ClientRangedWeapon";
   if(!slots&&!transform&&!equipment&&!ranged)continue;
   const unsigned flag=slots?1:transform?2:equipment?4:8;need(!(seen&flag),"duplicate client visual/contact type");seen|=flag;
   if(slots||transform)++found;
   const unsigned size=slots?1760:transform?176:2;m.read(row,88,true);m.text(textptr,length,true);
   need(m.get<std::uint32_t>(row+32)==hash(name)&&(m.get<std::uint32_t>(row+56)&65535)==size,"client visual type size/hash mismatch");
   const auto meta=m.get(row+40);need(m.inside(meta,144),"client visual storage metadata outside image");m.read(meta,144,true);
   need(m.text(m.get(meta+32),m.get(meta+40))==name&&m.get<std::uint32_t>(meta+64)==size&&m.get<std::uint32_t>(meta+80)==hash(name),"client visual storage metadata mismatch");
   const auto bit=m.get<std::uint8_t>(o.entity_layout+i/8,true);
   if(!(bit&(1u<<(i%8)))){need(equipment||ranged,"client visual component absent");continue;}
   const auto offset=m.get<std::uint16_t>(o.entity_layout+e::componentOffsets+i*2,true),stride=m.get<std::uint16_t>(o.entity_layout+e::componentStrides+i*2,true);
   need(stride==size&&o.entity_row<=1u<<24,"client visual stride/row mismatch");const U address=o.entity_storage+offset+U(o.entity_row)*stride;
   need(address>=o.entity_storage&&address<0x800000000000ULL-size,"client visual address invalid");
   if(slots)target.client_slots=address;else if(transform)target.render_transform=address;
   else if(equipment)target.equipment_animation_sets=address;else target.client_ranged_weapon=address;
  }need(found==2&&target.client_slots&&target.render_transform,"unique client hand slot/transform storage unavailable");
  const auto systems=m.get<std::uint16_t>(o.registry+s::systemCountOffset,true);need(systems&&systems<=s::maximumSystems,"visual system count invalid");unsigned matches=0;
  for(U i=0;i<systems;++i){const auto row=o.registry+s::systemsOffset+i*s::systemStride;if(m.get(row+40)!=o.image.base+0x316ea0)continue;
   const auto descriptor=m.get(row);need(m.inside(descriptor,192),"visual system descriptor outside image");
   need(m.text(m.get(descriptor),m.get(descriptor+8))=="update_animation_graph_slots_2"&&m.get(descriptor+16)==o.image.base+0x316ea0,"visual publisher full identity mismatch");
   ++matches;m.get(row,true);m.read(descriptor,192,true);m.text(m.get(descriptor),m.get(descriptor+8),true);
   target.system_function_slot=row+40;target.original_function=o.image.base+0x31b670;target.publish_call_site=o.image.base+0x31b84b;target.entity_id=o.entity_id;
  }need(matches==1,"client visual publisher absent or ambiguous");
  m.verify();out=std::move(target);return true;
 }catch(const std::exception&ex){try{why=ex.what();}catch(...){}}catch(...){try{why="tool target internal failure";}catch(...){}}
 out={};return false;
}
}
