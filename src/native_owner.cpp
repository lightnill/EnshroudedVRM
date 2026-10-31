#include "native_owner.hpp"
#include "native_menu_owner.hpp"
#include "native_owner_profile.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <map>
#include <set>
#include <utility>

// Read-only contracts adapted from ShroudForge commit
// 46b59da1b3642f91fee3808c513efb996ec33f23, Copyright(c)2026 bonsaibauer.
// The accompanying native_owner_profile.hpp preserves provenance and MIT license.
namespace evrm { namespace {
namespace p=native_owner_profile;
namespace o=p::ownerLayout;
namespace e=p::ecsReadLayout;
namespace s=p::serviceLayout;
using U=std::uint64_t;
using Bytes=std::vector<std::uint8_t>;
using Phase=NativeOwnerObservationPhase;
using Location=NativeOwnerFailureLocation;
struct Rejected { const char* reason; };
void need(bool ok,const char* reason) { if(!ok)throw Rejected{reason}; }
bool range(U address,U size) {return address>=0x10000&&size>0&&address<0x800000000000ULL&&size<=0x800000000000ULL-address;}
U add(U a,U b){need(a<=std::numeric_limits<U>::max()-b,"address overflow");return a+b;}
U mul(U a,U b){need(!b||a<=std::numeric_limits<U>::max()/b,"size overflow");return a*b;}
template<class T> T get(const Bytes& data,std::size_t offset){need(offset<=data.size()&&sizeof(T)<=data.size()-offset,"field outside structure");T value;std::memcpy(&value,data.data()+offset,sizeof value);return value;}
std::uint32_t fnv(const std::string& name){std::uint32_t h=2166136261u;for(unsigned char c:name)h=(h^c)*16777619u;return h;}
void reason_set(std::string& out,const char* why)noexcept{try{out=why;}catch(...){out.clear();}}
struct Reader {
    const NativeOwnerMemory& memory;
    NativeOwnerLimits limits;
    U started;
    NativeOwnerObservationStats* stats;
    std::size_t calls=0,bytes=0;
    std::vector<NativeOwnerGuard> guards;
    Phase current_phase=Phase::image;
    Reader(const NativeOwnerMemory& m,const NativeOwnerLimits& l,NativeOwnerObservationStats* out):memory(m),limits(l),started(m.milliseconds()),stats(out){
        location(Location::validation);
        need(l.max_read&&l.max_read<=65536&&l.max_bytes&&l.max_bytes<=64*1024*1024&&l.max_calls&&l.max_calls<=100000&&l.max_milliseconds&&l.max_milliseconds<=30000,"invalid read budget");
    }
    void location(Location value)const noexcept{if(stats)stats->failure_location=value;}
    void phase(Phase value)noexcept{current_phase=value;if(stats){stats->phase=value;stats->failure_location=Location::validation;}}
    void elapsed(U now)const noexcept{if(stats){stats->clock_regressed=stats->clock_regressed||now<started;stats->elapsed_milliseconds=now>=started?now-started:0;}}
    void check(Location where=Location::precheck)const{const U now=memory.milliseconds();location(where);elapsed(now);
        need(now>=started&&now-started<limits.max_milliseconds,"owner observation deadline exceeded");location(Location::validation);}
    void read(U address,void* dst,std::size_t count){
        check();location(Location::range);need(range(address,count)&&count<=limits.max_read,"invalid read range");
        location(Location::budget);
        need(calls<limits.max_calls&&count<=limits.max_bytes-bytes,"owner read budget exceeded");
        ++calls;bytes+=count;if(stats){stats->attempted_calls=calls;stats->attempted_bytes=bytes;}
        location(Location::read);const bool readable=memory.read(address,dst,count);
        if(readable&&stats){++stats->completed_calls;stats->completed_bytes+=count;}
        need(readable,"unreadable owner range");check(Location::postcheck);
    }
    Bytes span(U address,U count,bool guard=true){
        need(range(address,count)&&count<=limits.max_bytes,"invalid bounded span");Bytes result(static_cast<std::size_t>(count));
        for(std::size_t offset=0;offset<result.size();){const auto n=std::min(limits.max_read,result.size()-offset);read(add(address,offset),result.data()+offset,n);if(guard){guards.push_back({add(address,offset),Bytes(result.begin()+offset,result.begin()+offset+n),current_phase});if(stats)stats->guard_total=guards.size();}offset+=n;}
        return result;
    }
    template<class T=U> T scalar(U address){return get<T>(span(address,sizeof(T)),0);}
    void verify(const std::vector<NativeOwnerGuard>& saved){
        std::size_t ordinal=0;if(stats)stats->guard_total=saved.size();
        // One bounded scratch allocation per verification; every retained
        // range is still read and compared in its original order.
        Bytes current(limits.max_read);
        for(const auto& g:saved){if(stats)stats->guard_ordinal=ordinal;location(Location::validation);
            need(!g.bytes.empty()&&g.bytes.size()<=limits.max_read,"invalid owned guard");
            if(stats){stats->guard_capture_phase=g.capture_phase;stats->guard_span_bytes=g.bytes.size();stats->guard_difference_offset=0;}
            read(g.address,current.data(),g.bytes.size());location(Location::compare);
            if(std::memcmp(current.data(),g.bytes.data(),g.bytes.size())!=0){
                if(stats){std::size_t at=0;while(at<g.bytes.size()&&current[at]==g.bytes[at])++at;stats->guard_difference_offset=at;}
                need(false,"owner guard changed");
            }
            location(Location::validation);++ordinal;}
        if(stats)stats->guard_ordinal=ordinal;
        check(Location::finalcheck);location(Location::none);
    }
};
bool image_identity(const NativeOwnerImage& image){return range(image.base,image.size)&&image.size==p::image_size&&image.timestamp==p::image_timestamp&&std::equal(image.sha256.begin(),image.sha256.end(),std::begin(p::image_sha256));}
struct Image {
    Reader& m;const NativeOwnerImage& image;
    std::vector<std::pair<U,U>> executable;
    bool inside(U address,U count)const{return address>=image.base&&count<=image.size&&address-image.base<=image.size-count;}
    bool code(U address)const{for(auto r:executable)if(r.first<=address&&address<r.second)return true;return false;}
    Image(Reader& r,const NativeOwnerImage& i):m(r),image(i){
        need(image_identity(image),"unsupported image SHA/timestamp/size");auto dos=m.span(image.base,64);need(dos[0]=='M'&&dos[1]=='Z',"DOS header mismatch");
        U nt=add(image.base,get<std::uint32_t>(dos,0x3C));need(inside(nt,24),"PE header outside image");auto pe=m.span(nt,24);need(get<std::uint32_t>(pe,0)==0x4550&&get<std::uint16_t>(pe,4)==0x8664,"PE signature/machine mismatch");
        U count=get<std::uint16_t>(pe,6),optional=get<std::uint16_t>(pe,20);need(count&&count<=96&&optional>=64&&optional<=512&&inside(add(nt,24),optional),"invalid PE dimensions");auto opt=m.span(add(nt,24),optional);
        need(get<std::uint16_t>(opt,0)==0x20B&&get<std::uint32_t>(pe,8)==image.timestamp&&get<std::uint32_t>(opt,56)==image.size,"loaded PE identity mismatch");
        U table=add(nt,24+optional);need(inside(table,mul(count,40)),"PE sections outside image");auto sections=m.span(table,count*40);
        for(U index=0;index<count;++index){U size=get<std::uint32_t>(sections,index*40+8),rva=get<std::uint32_t>(sections,index*40+12);need(inside(add(image.base,rva),size),"PE section range outside image");if(get<std::uint32_t>(sections,index*40+36)&0x20000000){need(size>0,"empty executable section");executable.push_back({image.base+rva,image.base+rva+size});}}
        need(!executable.empty(),"no executable image section");
    }
    std::string text(U pointer,U length,U bound,bool module_owned){
        need(length&&length<=bound&&(!module_owned||inside(pointer,length)),"name outside image or length bound");auto data=m.span(pointer,length);if(data.back()==0)data.pop_back();need(!data.empty(),"empty full name");
        for(auto c:data) { need(c>=32&&c<127,"invalid full name encoding"); }
        return std::string(data.begin(),data.end());
    }
};
struct Descriptor {std::string name;std::uint32_t size=0,hash=0;bool structure=false;};
struct Registration {Descriptor storage;U storage_pointer=0;std::uint32_t size=0,index=0;};
std::vector<Registration> registry(Reader& m,Image& image,U address){
    const U records=m.scalar(address+8),count=m.scalar(address+16),capacity=m.scalar(address+24);need(count&&count<=1024&&count<=capacity&&capacity<=4096,"invalid component registry dimensions");
    auto array=[&](U offset,U stride){U pointer=m.scalar(address+offset);need(m.scalar(address+offset+8)==count,"parallel registry count mismatch");return m.span(pointer,mul(count,stride));};
    auto sizes=array(232,2),types=array(256,8),callbacks=array(280,40);std::map<U,Descriptor> descriptors;
    auto descriptor=[&](U pointer){
        if(!pointer) { return Descriptor{}; }
        auto found=descriptors.find(pointer);if(found!=descriptors.end())return found->second;
        need(image.inside(pointer,0x90),"type metadata outside image");auto bytes=m.span(pointer,0x90);Descriptor result;
        result.name=image.text(get<U>(bytes,0x20),get<U>(bytes,0x28),512,true);result.size=get<std::uint32_t>(bytes,0x40);result.hash=get<std::uint32_t>(bytes,0x50);
        auto expected=std::lower_bound(std::begin(p::types),std::end(p::types),result.name,[](const auto& type,const auto& name){return std::strcmp(type.name,name.c_str())<0;});
        need(expected!=std::end(p::types)&&result.name==expected->name&&result.size&&result.size<=1u<<24&&result.size==expected->size&&result.hash==expected->hash&&result.hash==fnv(result.name),"registered type differs from pinned reflection");
        result.structure=expected->structure;descriptors.emplace(pointer,result);return result;
    };
    std::set<std::string> names,runtime_names;std::set<std::uint32_t> hashes;std::vector<Registration> result;
    for(U index=0;index<count;++index){
        // Only the identity prefix is retained. Model/animation/input payloads are never traversed.
        auto row=m.span(add(records,mul(index,256)),88);std::string name=image.text(get<U>(row,16),get<U>(row,24),512,true);auto hash=get<std::uint32_t>(row,32),packed=get<std::uint32_t>(row,56);U storage_pointer=get<U>(row,40),config_pointer=get<U>(row,48);
        auto storage=descriptor(storage_pointer),configuration=descriptor(config_pointer);const auto& identity=config_pointer?configuration:storage;
        need(name.compare(0,11,"keen::ecs::")==0&&names.insert(name).second&&hashes.insert(hash).second&&hash==fnv(name)&&identity.name==name&&identity.hash==hash,"ambiguous registration identity");
        auto size=packed&0xFFFF;need(size==storage.size&&size==get<std::uint16_t>(sizes,index*2)&&storage_pointer==get<U>(types,index*8),"parallel storage identity/size mismatch");if(storage_pointer)need(runtime_names.insert(storage.name).second,"duplicate runtime storage type");
        for(unsigned slot=0;slot<4;++slot){U callback=get<U>(callbacks,index*40+slot*8);need(!callback||image.code(callback),"component callback outside executable image");}
        result.push_back({storage,storage_pointer,size,static_cast<std::uint32_t>(index)});
    }
    return result;
}
void owners(Reader& m,const NativeOwnerImage& image,NativeOwnerCandidate& c){
    auto vt=[&](U pointer,U rva){need(m.scalar(pointer)==add(image.base,rva),"owner vtable mismatch");};
    c.root=add(image.base,o::rootObjectRva);vt(c.root,o::rootVtableRva);c.client=m.scalar(c.root+o::clientPointerOffset);vt(c.client,o::clientVtableRva);c.controller=m.scalar(c.client+o::controllerPointerOffset);vt(c.controller,o::controllerVtableRva);
    need(m.scalar(c.controller+o::controllerClientBackPointerOffset)==c.client,"controller backpointer mismatch");c.main=m.scalar(c.controller+o::controllerMainOwnerOffset);c.execution=m.scalar(add(c.main,e::mainExecutionOwnerOffset));
    c.registry=add(c.execution,e::executionRegistryOwnerOffset);c.entity_manager=add(c.execution,e::executionEntityManagerOffset);need(m.scalar(c.entity_manager+e::entityManagerOwnerOffset)==c.registry,"entity manager registry owner mismatch");
}
Bytes entity_equipment(Reader& m,const std::vector<Registration>& registrations,NativeOwnerCandidate& c){
    auto selected=[&](const char* name)->const Registration&{auto it=std::find_if(registrations.begin(),registrations.end(),[&](const auto& r){return r.storage.name==name;});need(it!=registrations.end()&&it->storage.structure,"selected reflected runtime struct missing");return *it;};
    const auto& input=selected("keen::ecs::ClientPlayerInput");const auto& equipment=selected("keen::ecs::RenderEquipment");need(input.size==1536&&equipment.size==788,"selected registration size mismatch");c.input_registration=input.index;c.equipment_registration=equipment.index;
    U capacity=m.scalar(c.entity_manager+e::entityManagerCount);need(capacity&&capacity<=e::maximumEntities&&!(capacity&(capacity-1)),"invalid entity table capacity");U table=m.scalar(c.entity_manager+e::entityManagerTable),occupancy=m.scalar(c.entity_manager+e::entityManagerOccupancy),keys=m.scalar(c.entity_manager+e::entityManagerKeys);
    auto slots=m.span(table,mul(capacity,8)),keydata=m.span(keys,mul(capacity,4)),occupied=m.span(occupancy,((capacity+63)/64)*8);auto bit=[&](U index){return (occupied[index/8]>>(index%8))&1;};for(U i=capacity;i<occupied.size()*8;++i)need(!bit(i),"entity occupancy exceeds capacity");
    U bit_size=((registrations.size()+63)/64)*8;need(bit_size<=e::componentOffsets&&registrations.size()*2<=e::componentStrides-e::componentOffsets,"component layout arrays overlap");std::map<U,Bytes> layouts;std::set<std::uint32_t> ids;std::set<U> pointers;unsigned candidates=0;
    for(U slot=0;slot<capacity;++slot){m.check();if(!bit(slot))continue;U pointer=get<U>(slots,slot*8);auto header=m.span(add(pointer,16),36);
        auto id=get<std::uint32_t>(header,0),generation=get<std::uint32_t>(header,4),row=get<std::uint32_t>(header,32);U layout=get<U>(header,8),storage=get<U>(header,16);
        need(id&&id==get<std::uint32_t>(keydata,slot*4)&&ids.insert(id).second&&pointers.insert(pointer).second&&range(pointer,52)&&range(layout,1)&&range(storage,1)&&row<=1u<<24,"invalid occupied entity identity/layout/storage/row");auto found=layouts.find(layout);if(found==layouts.end())found=layouts.emplace(layout,m.span(layout,bit_size)).first;
        if((found->second[input.index/8]>>(input.index%8))&1){++candidates;c.entity=pointer;c.entity_id=id;c.entity_generation=generation;c.entity_slot=static_cast<std::uint32_t>(slot);c.entity_row=row;c.entity_layout=layout;c.entity_storage=storage;}
    }
    need(candidates==1,"ClientPlayerInput missing or ambiguous in complete census");std::uint32_t hash=c.entity_id;for(unsigned i=0;i<2;++i)hash=((hash>>16)^hash)*0x45D9F3Bu;U start=(hash^(hash>>16))&(capacity-1);bool found=false;
    for(U probe=0;probe<std::min<U>(capacity,8192);++probe){U slot=(start+probe)&(capacity-1);need(bit(slot),"entity outside original ID hash chain");if(get<std::uint32_t>(keydata,slot*4)==c.entity_id){need(slot==c.entity_slot,"entity ID selector disagreement");found=true;break;}}need(found,"entity hash probe bound exceeded");
    m.phase(Phase::equipment);
    const auto& presence=layouts.at(c.entity_layout);need((presence[equipment.index/8]>>(equipment.index%8))&1,"RenderEquipment absent on input entity");
    for(const auto* entry:{&input,&equipment}){U offset=m.scalar<std::uint16_t>(add(c.entity_layout,e::componentOffsets+entry->index*2));U stride=m.scalar<std::uint16_t>(add(c.entity_layout,e::componentStrides+entry->index*2));need(stride==entry->size&&stride,"selected runtime component stride mismatch");U address=add(add(c.entity_storage,offset),mul(c.entity_row,stride));need(range(address,stride),"selected component address out of bounds");if(entry==&equipment)c.equipment=address;}
    // Pinned RenderEquipment modelHandles[24]@0 is the ownership input.
    // The remaining692 bytes are visual caches/colors/hide and pose state;
    // hideMask@768 changes normally near camera occlusion. Keep the complete
    //788-byte registered stride/address check above, but guard only consumed
    // model handles, each still validated against its allocation generation.
    return m.span(c.equipment,24u*sizeof(std::uint32_t));
}
U scene(Reader& m,Image& image,U registry_address){
    U rows=m.scalar(add(registry_address,s::recordsPointerOffset)),count=m.scalar(add(registry_address,s::recordCountOffset));need(count&&count<=s::maximumRecords,"invalid service count");
    U bits=m.scalar(add(registry_address,s::mapBitsOffset)),capacity=m.scalar(add(registry_address,s::mapCapacityOffset)),keys=m.scalar(add(registry_address,s::mapKeysOffset)),values=m.scalar(add(registry_address,s::mapValuesOffset));auto used=m.scalar<std::uint32_t>(add(registry_address,s::mapCountOffset));need(capacity&&capacity<=s::maximumMapCapacity&&!(capacity&(capacity-1))&&used&&used<=std::min(count,capacity),"invalid service map dimensions");
    auto occupancy=m.span(bits,((capacity+63)/64)*8),keydata=m.span(keys,capacity*4),valuedata=m.span(values,capacity*2);auto bit=[&](U index){return (occupancy[index/8]>>(index%8))&1;};U population=0;for(U i=0;i<occupancy.size()*8;++i){if(i>=capacity)need(!bit(i),"service occupancy exceeds capacity");else population+=bit(i);}need(population==used,"service occupancy/count disagreement");
    std::vector<std::uint32_t> hashes;std::set<std::string> names;std::set<std::uint32_t> unique_hashes;U selected=0;unsigned selected_count=0;
    for(U index=0;index<count;++index){U record=add(rows,mul(index,s::recordStride));auto view=m.span(record,16);std::string name=image.text(get<U>(view,0),get<U>(view,8),128,false);auto hash=m.scalar<std::uint32_t>(add(record,s::recordHashOffset));need(hash==fnv(name)&&names.insert(name).second&&unique_hashes.insert(hash).second,"duplicate service name/hash or hash mismatch");hashes.push_back(hash);if(name=="RenderScene"){selected=index;++selected_count;}}
    need(selected_count==1,"RenderScene full name missing or ambiguous");std::set<U> seen_indices;std::set<std::uint32_t> seen_keys;
    for(U slot=0;slot<capacity;++slot)if(bit(slot)){U index=get<std::uint16_t>(valuedata,slot*2);auto hash=get<std::uint32_t>(keydata,slot*4);need(index<count&&seen_indices.insert(index).second&&seen_keys.insert(hash).second&&hashes[index]==hash,"service map record disagreement");}
    U slot=fnv("RenderScene")&(capacity-1);bool found=false;for(U probe=0;probe<capacity;++probe){need(bit(slot),"RenderScene outside original hash probe chain");if(get<std::uint32_t>(keydata,slot*4)==fnv("RenderScene")){need(get<std::uint16_t>(valuedata,slot*2)==selected,"RenderScene map selector mismatch");found=true;break;}slot=(slot+1)&(capacity-1);}need(found,"service hash probe bound exceeded");
    U row=add(rows,mul(selected,s::recordStride));U result=m.scalar(add(row,s::recordNormalPointerOffset));need(range(result,0xA0)&&m.scalar(add(row,s::recordAlternatePointerOffset))==result,"RenderScene normal/alternate pointer mismatch");
    U descriptor=add(image.image.base,0x1DDDE80),function=add(image.image.base,0x383030);need(image.inside(descriptor,0xC0)&&image.code(function),"system metadata/function outside pinned image");auto desc=m.span(descriptor,0xC0);need(image.text(get<U>(desc,0),get<U>(desc,8),128,true)=="render_equipment_models_ag2"&&get<U>(desc,16)==function,"original system full name/function mismatch");
    const char* services[]={"BasicPlayerInfos","CharacterPresetRegistry","ColorGradientRegistry","MaterialFeedbackRegistry","g38_items::ItemRegistry","RenderScene"};
    for(unsigned access=0;access<2;++access){U offset=access?0xB0:0x98,pointer=get<U>(desc,offset),length=get<U>(desc,offset+8);need(length==(access?1u:5u)&&image.inside(pointer,length*16),"original system service array mismatch");auto views=m.span(pointer,length*16);for(U i=0;i<length;++i)need(image.text(get<U>(views,i*16),get<U>(views,i*16+8),128,true)==services[access?5:i],"original system service ordinal mismatch");}
    U systems=m.scalar<std::uint16_t>(add(registry_address,s::systemCountOffset));need(systems&&systems<=s::maximumSystems,"invalid system count");U selected_system=0;unsigned matches=0;for(U index=0;index<systems;++index){U address=add(add(registry_address,s::systemsOffset),mul(index,s::systemStride));if(m.scalar(address)==descriptor){selected_system=address;++matches;}}
    need(matches==1,"original equipment system missing or ambiguous");need(m.scalar(add(selected_system,0x28))==function,"system function mismatch");need(m.scalar<std::uint8_t>(add(selected_system,s::systemNormalCountOffset))==6&&m.scalar<std::uint8_t>(add(selected_system,s::systemAlternateCountOffset))==6,"system service counts mismatch");
    need(m.scalar(add(selected_system,s::systemNormalBindingsOffset+40))==result&&m.scalar(add(selected_system,s::systemAlternateBindingsOffset+40))==result,"system ordinal5 scene binding mismatch");return result;
}
void model_allocation(Reader& m,Image& image,const std::vector<std::uint32_t>& handles_input,NativeOwnerCandidate& c){
    U records=m.scalar(add(c.model_manager,0x1B0)),record_bytes=m.scalar(add(c.model_manager,0x1B8));U generations=m.scalar(add(c.model_manager,0x1C8)),capacity=m.scalar(add(c.model_manager,0x1D0));
    need(record_bytes&&record_bytes%432==0&&record_bytes/432==capacity&&capacity&&capacity<=131072&&m.scalar<std::uint32_t>(add(c.model_manager,0x48))==capacity,"model allocation dimensions mismatch");need(range(records,record_bytes)&&range(generations,capacity),"model allocation range invalid");std::set<std::uint32_t> handles,indices;
    for(unsigned slot=0;slot<handles_input.size();++slot){auto handle=handles_input[slot];if(!handle)continue;auto index=handle&0xFFFFFF,generation=handle>>24;need(handles.insert(handle).second&&indices.insert(index).second,"duplicate equipment model handle/index");need(index<capacity,"model handle index outside allocation");need(m.scalar<std::uint8_t>(add(generations,index))==generation,"model handle generation mismatch");c.models.push_back({slot,handle,index,generation,add(records,mul(index,432))});}need(!c.models.empty(),"no selected equipment model handles");c.writer=m.scalar(add(c.model_manager,0x948));
    // Pinned 208-byte writer: stable owner/resource prefix [0,112), dimensions
    // [192,200). Intervening update payload is not an identity guard.
    auto resources=m.span(c.writer,0x70),dimensions=m.span(add(c.writer,0xC0),8);U total=get<std::uint32_t>(resources,0x48);c.instance_capacity=get<std::uint32_t>(dimensions,0);c.instance_extra=get<std::uint32_t>(dimensions,4);
    need(c.instance_capacity==capacity&&c.instance_extra<=131072&&U(c.instance_capacity)+c.instance_extra<=262144&&total==(U(c.instance_capacity)+c.instance_extra)*128,"writer allocation dimensions mismatch");
    // Current wrapper is exactly 12 bytes: pointer owner + low-flag-free index.
    auto wrapper=m.span(add(c.writer,8),12);U owner=get<U>(wrapper,0);auto index=get<std::uint32_t>(wrapper,8);need(index<=65535&&range(owner,0x290),"current writer resource wrapper invalid");
    U table=m.scalar(add(owner,0x268)),count=m.scalar(add(owner,0x270)),dims=m.scalar(add(owner,0x280)),dimension_count=m.scalar(add(owner,0x288));need(count&&count<=65535&&dimension_count==count&&index<count&&range(table,count*8)&&range(dims,count*16),"resource table dimensions/bounds mismatch");
    U device=m.scalar(add(owner,8));need(range(device,0x280),"resource GPU device invalid");U interface=m.scalar(add(device,0x278));need(range(interface,8),"GPU backend interface invalid");U vtable=m.scalar(interface);need(image.inside(add(image.image.base,0x1DA69E0),0x68)&&vtable==add(image.image.base,0x1DA69E0),"GPU backend vtable mismatch");
    U thunk=m.scalar(add(vtable,0x60));need(thunk==add(image.image.base,0xFBA100)&&image.code(thunk),"GPU buffer constructor thunk mismatch");c.resource_object=m.scalar(add(table,U(index)*8));auto dimension=m.span(add(dims,U(index)*16),16),prefix=m.span(c.resource_object,32);U size=get<U>(prefix,8),buffer=get<U>(prefix,24);auto usage=get<std::uint32_t>(prefix,16);
    need(get<std::uint32_t>(prefix,0)==0x50001,"native buffer object tag mismatch");need(size==total&&size==get<U>(dimension,0)&&usage==0x13&&usage==get<std::uint32_t>(dimension,8),"native buffer size/usage mismatch");need(buffer,"native buffer is zero");c.native_buffer=buffer;c.native_buffer_bytes=size;
}
void models_and_buffer(Reader& m,Image& image,const Bytes& equipment,NativeOwnerCandidate& c){
    c.model_manager=m.scalar(add(c.render_scene,0x98));
    std::vector<std::uint32_t> handles;handles.reserve(24);
    for(unsigned slot=0;slot<24;++slot)handles.push_back(get<std::uint32_t>(equipment,slot*4));
    model_allocation(m,image,handles,c);
}
} // namespace
bool capture_native_menu_owner(const NativeOwnerMemory& memory,const NativeOwnerImage& identity,NativeMenuOwnerCandidate& out,std::string& why,const NativeOwnerLimits& limits)noexcept{
    const auto saved_image=identity;out={};why.clear();
    try{
        Reader m(memory,limits,nullptr);NativeMenuOwnerCandidate c;c.image=saved_image;Image image(m,c.image);
        m.phase(Phase::owners);
        const auto vt=[&](U pointer,U rva){need(m.scalar(pointer)==add(c.image.base,rva),"menu owner vtable mismatch");};
        c.root=add(c.image.base,o::rootObjectRva);vt(c.root,o::rootVtableRva);
        c.client=m.scalar(c.root+o::clientPointerOffset);vt(c.client,o::clientVtableRva);
        c.controller=m.scalar(c.client+o::controllerPointerOffset);vt(c.controller,o::controllerVtableRva);
        need(m.scalar(c.controller+o::controllerClientBackPointerOffset)==c.client,"menu controller backpointer mismatch");
        need(m.scalar(c.controller+o::controllerMainOwnerOffset)==0,"menu context has an active world");
        c.scene_owner=m.scalar(c.client+e::clientSceneOwnerOffset);
        c.scene_entries=m.scalar(add(c.scene_owner,e::sceneEntriesPointerOffset));
        const U count=m.scalar(add(c.scene_owner,e::sceneEntriesCountOffset));
        c.scene_capacity=m.scalar(add(c.scene_owner,e::sceneEntriesCapacityOffset));
        need(count==0,"menu context has loading or active scene entries");
        need(c.scene_capacity&&c.scene_capacity<=e::maximumSceneEntries&&
            range(c.scene_entries,mul(c.scene_capacity,e::sceneEntryStride)),"menu scene allocation dimensions invalid");
        m.phase(Phase::capture_verify);m.verify(m.guards);c.guards=std::move(m.guards);out=std::move(c);return true;
    }catch(const Rejected& failure){reason_set(why,failure.reason);}catch(...){reason_set(why,"menu observation allocation/internal failure");}
    out={};return false;
}
bool capture_native_character_preview_owner(const NativeOwnerMemory& memory,const NativeOwnerImage& identity,NativeMenuOwnerCandidate& out,std::string& why,const NativeOwnerLimits& limits)noexcept{
    const auto saved_image=identity;out={};why.clear();
    try{
        Reader m(memory,limits,nullptr);NativeMenuOwnerCandidate c;c.image=saved_image;c.kind=NativeMenuOwnerKind::character;
        Image image(m,c.image);auto& allocation=c.preview;allocation.image=c.image;
        m.phase(Phase::owners);
        const auto vt=[&](U pointer,U rva){need(m.scalar(pointer)==add(c.image.base,rva),"preview owner vtable mismatch");};
        c.root=add(c.image.base,o::rootObjectRva);vt(c.root,o::rootVtableRva);
        c.client=m.scalar(c.root+o::clientPointerOffset);vt(c.client,o::clientVtableRva);
        c.controller=m.scalar(c.client+o::controllerPointerOffset);vt(c.controller,o::controllerVtableRva);
        need(m.scalar(c.controller+o::controllerClientBackPointerOffset)==c.client,"preview controller backpointer mismatch");
        allocation.root=c.root;allocation.client=c.client;allocation.controller=c.controller;
        allocation.main=m.scalar(c.controller+o::controllerMainOwnerOffset);
        need(range(allocation.main,0x466b08),"preview has no current local world");
        // Exact-image player Character callbacks856ba0/856e00/8578a0 use this
        // renderer. It owns its own manager, not the gameplay RenderEquipment.
        c.renderer=m.scalar(allocation.main+0x466b00);
        allocation.render_scene=m.scalar(add(c.renderer,0x20));
        // f1b50 receives RenderScene and forwards through its +0x98 manager.
        allocation.model_manager=m.scalar(add(allocation.render_scene,0x98));
        const U body=m.scalar(add(c.renderer,0x58)),count=m.scalar(add(c.renderer,0x60)),capacity=m.scalar(add(c.renderer,0x68));
        need(count<=capacity&&capacity&&capacity<=64&&range(body,mul(capacity,4)),"preview body allocation invalid");
        std::vector<std::uint32_t> handles;handles.reserve(112);
        if(count){const auto values=m.span(body,count*4);for(U i=0;i<count;++i)handles.push_back(get<std::uint32_t>(values,i*4));}
        //23e250 creates the body vector;23fee0 owns two model handles in each
        // of24 equipment slots. Guard consumed handles only, never pose bytes.
        for(unsigned i=0;i<24;++i)for(U offset:{U(0x10c),U(0x120)})
            handles.push_back(m.scalar<std::uint32_t>(add(c.renderer,offset+U(i)*0x28)));
        m.phase(Phase::models);model_allocation(m,image,handles,allocation);
        m.phase(Phase::capture_verify);m.verify(m.guards);c.guards=std::move(m.guards);out=std::move(c);return true;
    }catch(const Rejected& failure){reason_set(why,failure.reason);}catch(...){reason_set(why,"preview observation allocation/internal failure");}
    out={};return false;
}
bool revalidate_native_menu_owner(const NativeOwnerMemory& memory,NativeMenuOwnerCandidate& c,std::string& why,const NativeOwnerLimits& limits)noexcept{
    why.clear();try{
        need(image_identity(c.image)&&c.root==add(c.image.base,o::rootObjectRva)&&c.client&&c.controller&&!c.guards.empty(),"invalid menu owner candidate");
        if(c.kind==NativeMenuOwnerKind::frontend)
            need(c.scene_owner&&c.scene_capacity&&c.scene_capacity<=e::maximumSceneEntries&&
                range(c.scene_entries,mul(c.scene_capacity,e::sceneEntryStride)),"invalid frontend context");
        else need(c.kind==NativeMenuOwnerKind::character&&c.renderer&&c.preview.main&&c.preview.model_manager&&
            c.preview.native_buffer&&c.preview.native_buffer_bytes&&!c.preview.models.empty()&&c.preview.models.size()<=112,"invalid Character preview context");
        Reader m(memory,limits,nullptr);m.phase(Phase::submission_verify);m.verify(c.guards);return true;
    }catch(const Rejected& failure){reason_set(why,failure.reason);}catch(...){reason_set(why,"menu revalidation allocation/internal failure");}
    c={};return false;
}
bool same_native_menu_owner(const NativeMenuOwnerCandidate& a,const NativeMenuOwnerCandidate& b)noexcept{
    if(a.kind!=b.kind)return false;
    if(a.kind==NativeMenuOwnerKind::character){
        if(a.renderer!=b.renderer||a.preview.main!=b.preview.main||a.preview.model_manager!=b.preview.model_manager||
            a.preview.render_scene!=b.preview.render_scene||a.preview.native_buffer!=b.preview.native_buffer||a.preview.native_buffer_bytes!=b.preview.native_buffer_bytes||
            a.preview.models.size()!=b.preview.models.size())return false;
        for(std::size_t i=0;i<a.preview.models.size();++i)if(a.preview.models[i].handle!=b.preview.models[i].handle||
            a.preview.models[i].record!=b.preview.models[i].record)return false;
    }
    return !a.guards.empty()&&!b.guards.empty()&&a.image.base==b.image.base&&a.image.size==b.image.size&&
        a.image.timestamp==b.image.timestamp&&a.image.sha256==b.image.sha256&&a.root==b.root&&a.client==b.client&&
        a.controller==b.controller&&a.scene_owner==b.scene_owner&&a.scene_entries==b.scene_entries&&a.scene_capacity==b.scene_capacity;
}
bool capture_native_owner(const NativeOwnerMemory& memory,const NativeOwnerImage& identity,NativeOwnerCandidate& out,std::string& why,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats)noexcept{
    if(stats){*stats={};stats->phase=Phase::image;stats->failure_location=Location::validation;}
    // Copy identity first: callers may pass out.image by reference.
    const NativeOwnerImage identity_copy=identity;out={};why.clear();
    try{Reader m(memory,limits,stats);NativeOwnerCandidate candidate;candidate.image=identity_copy;Image image(m,candidate.image);
        m.phase(Phase::owners);owners(m,candidate.image,candidate);m.phase(Phase::registry);auto registrations=registry(m,image,candidate.registry);
        m.phase(Phase::census);auto equipment=entity_equipment(m,registrations,candidate);m.phase(Phase::services);candidate.render_scene=scene(m,image,candidate.registry);
        m.phase(Phase::models);models_and_buffer(m,image,equipment,candidate);m.phase(Phase::capture_verify);m.verify(m.guards);
        candidate.read_calls=m.calls;candidate.read_bytes=m.bytes;const U now=memory.milliseconds();candidate.elapsed_milliseconds=now-m.started;m.elapsed(now);
        candidate.guards=std::move(m.guards);out=std::move(candidate);return true;
    }catch(const Rejected& failure){reason_set(why,failure.reason);}catch(...){if(stats)stats->failure_location=Location::internal;reason_set(why,"owner observation allocation/internal failure");}out={};return false;
}
bool revalidate_native_owner(const NativeOwnerMemory& memory,NativeOwnerCandidate& candidate,std::string& why,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats)noexcept{
    if(stats){*stats={};stats->phase=Phase::submission_verify;stats->failure_location=Location::validation;stats->guard_total=candidate.guards.size();}
    why.clear();try{need(image_identity(candidate.image)&&candidate.native_buffer&&candidate.native_buffer_bytes&&candidate.entity&&candidate.entity_id&&!candidate.models.empty()&&candidate.models.size()<=24&&!candidate.guards.empty(),"invalid native owner candidate");Reader m(memory,limits,stats);m.verify(candidate.guards.view());return true;
    }catch(const Rejected& failure){reason_set(why,failure.reason);}catch(...){if(stats)stats->failure_location=Location::internal;reason_set(why,"owner revalidation allocation/internal failure");}candidate={};return false;
}
namespace {
// Both phases forward to the same read-only adapter. Reader's existing clock
// checks update this absolute budget before each read; changing phase cannot
// reset the total deadline or call/byte allowance.
struct OwnerCallBudget final:NativeOwnerMemory {
    const NativeOwnerMemory& source;
    NativeOwnerLimits limits;
    U started;
    mutable U last;
    mutable std::size_t calls=0,bytes=0,completed_calls=0,completed_bytes=0;
    mutable bool regressed=false,read_budget=false;
    OwnerCallBudget(const NativeOwnerMemory& m,const NativeOwnerLimits& l):source(m),limits(l),started(m.milliseconds()),last(started){}
    U milliseconds()const noexcept override {
        last=source.milliseconds();regressed=regressed||last<started;return last;
    }
    bool timed_out()const noexcept {return regressed||last<started||last-started>=limits.max_milliseconds;}
    bool available()const noexcept {return !timed_out()&&!read_budget&&calls<limits.max_calls&&bytes<limits.max_bytes;}
    bool read(U address,void* destination,std::size_t count)const noexcept override {
        if(timed_out())return false;
        if(calls>=limits.max_calls||bytes>limits.max_bytes||count>limits.max_bytes-bytes){read_budget=true;return false;}
        ++calls;bytes+=count;
        const bool result=source.read(address,destination,count);
        if(result){++completed_calls;completed_bytes+=count;}return result;
    }
};
bool same_owner_image(const NativeOwnerImage& a,const NativeOwnerImage& b) {
    return a.base==b.base&&a.size==b.size&&a.timestamp==b.timestamp&&a.sha256==b.sha256;
}
}
bool NativeOwnerCache::capture(const NativeOwnerMemory& memory,const NativeOwnerImage& image,NativeOwnerCandidate& out,
                              std::string& why,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats,bool* reused)noexcept {
    const auto identity=image;out={};why.clear();if(stats)*stats={};if(reused)*reused=false;
    OwnerCallBudget budget(memory,limits);NativeOwnerObservationStats progress;
    bool accepted=false,hit=false;
    try{
        if(source_!=&memory||!same_owner_image(identity,cached_.image))clear();
        if(!cached_.guards.empty()) {
            accepted=revalidate_native_owner(budget,cached_,why,limits,&progress);hit=accepted;
            progress.phase=Phase::capture_verify;
        }
        if(!accepted&&budget.available()) {
            clear();accepted=capture_native_owner(budget,identity,cached_,why,limits,&progress);
        }
        if(accepted)out=cached_; // Caller mutations cannot change retained guards/identity.
    }catch(...){progress.failure_location=Location::internal;reason_set(why,"owner observation allocation/internal failure");accepted=false;}
    (void)budget.milliseconds();
    if(budget.timed_out()){reason_set(why,"owner observation deadline exceeded");progress.failure_location=Location::finalcheck;accepted=false;}
    if(budget.read_budget){reason_set(why,"owner read budget exceeded");progress.failure_location=Location::budget;accepted=false;}
    if(!accepted){clear();out={};}else source_=&memory;
    if(stats){
        *stats=progress;stats->attempted_calls=budget.calls;stats->attempted_bytes=budget.bytes;
        stats->completed_calls=budget.completed_calls;stats->completed_bytes=budget.completed_bytes;
        stats->elapsed_milliseconds=budget.last>=budget.started?budget.last-budget.started:0;stats->clock_regressed=stats->clock_regressed||budget.regressed;
    }
    if(reused)*reused=accepted&&hit;
    return accepted;
}
}
