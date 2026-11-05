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
namespace evrm {
// Private decoded image/registry metadata. This is not ownership authority;
// its original guard prefix must pass fresh reads in the same cache call.
struct NativeOwnerCachePrefix {
    struct Descriptor {std::string name;std::uint32_t size=0,hash=0;bool structure=false;};
    struct Registration {Descriptor storage;std::uint64_t storage_pointer=0;std::uint32_t size=0,index=0;};
    std::vector<Registration> registrations;
    std::vector<std::pair<std::uint64_t,std::uint64_t>> executable;
    std::size_t guard_count=0;
};
namespace {
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
// Ephemeral to ONE verify() call. Physical reads can include small gaps between
// known spans, but never cross a4096-byte region. Only original guards compare;
// gaps are neither interpreted nor retained in the candidate.
struct OwnerReadBatches {
    static constexpr std::uint16_t absent=65535;
    struct Group {U address=0;std::size_t size=0,offset=0;unsigned state=0;};
    std::vector<std::uint16_t> lookup;
    std::vector<Group> groups;
    Bytes data;
    static OwnerReadBatches prepare(const NativeOwnerMemory& memory,const NativeOwnerLimits& limits,
                                   std::size_t used_calls,std::size_t used_bytes,const std::vector<NativeOwnerGuard>& saved)noexcept{
        try{
            if(saved.size()<64||saved.size()>16384||!memory.permit_read_ahead(0,0)||used_calls>limits.max_calls||used_bytes>limits.max_bytes)return {};
            const auto available_calls=limits.max_calls-used_calls,available_bytes=limits.max_bytes-used_bytes;
            if(saved.size()>available_calls)return {};
            std::size_t exact_bytes=0;
            struct Request {U address,end;std::size_t ordinal;};
            std::vector<Request> requests;requests.reserve(saved.size());
            for(std::size_t i=0;i<saved.size();++i){const auto& g=saved[i];
                // A malformed later guard must not preempt an earlier mismatch.
                if(g.bytes.empty()||g.bytes.size()>limits.max_read||!range(g.address,g.bytes.size())||g.bytes.size()>available_bytes-exact_bytes)return {};
                exact_bytes+=g.bytes.size();const U end=g.address+g.bytes.size();
                if(g.bytes.size()<=512&&g.address/4096==(end-1)/4096)requests.push_back({g.address,end,i});
            }
            std::sort(requests.begin(),requests.end(),[](const auto& a,const auto& b){return a.address<b.address;});
            OwnerReadBatches plan;plan.lookup.assign(saved.size(),absent);plan.groups.reserve(512);std::size_t total=0;
            for(std::size_t first=0;first<requests.size();){
                std::size_t last=first+1;U end=requests[first].end;
                while(last<requests.size()&&requests[last].address/4096==requests[first].address/4096){end=std::max(end,requests[last].end);++last;}
                const auto size=std::size_t(end-requests[first].address);
                if(last-first>1&&plan.groups.size()<512&&size<=limits.max_read){
                    const auto group=static_cast<std::uint16_t>(plan.groups.size());
                    plan.groups.push_back({requests[first].address,size,total,0});total+=size;
                    for(auto i=first;i<last;++i)plan.lookup[requests[i].ordinal]=group;
                }
                first=last;
            }
            if(plan.groups.empty()||plan.groups.size()>available_calls-saved.size()||total>available_bytes-exact_bytes||
                !memory.permit_read_ahead(saved.size()+plan.groups.size(),exact_bytes+total))return {};
            plan.data.resize(total);return plan;
        }catch(...){return {};}// Optional allocation/planning never changes exact-read acceptance.
    }
};
struct Reader {
    const NativeOwnerMemory& memory;
    NativeOwnerLimits limits;
    U started;
    NativeOwnerObservationStats* stats;
    std::size_t calls=0,bytes=0;
    std::vector<NativeOwnerGuard> guards;
    Phase current_phase=Phase::image;
    Reader(const NativeOwnerMemory& m,const NativeOwnerLimits& l,NativeOwnerObservationStats* out):memory(m),limits(l),started(m.work_milliseconds()),stats(out){
        location(Location::validation);
        need(l.max_read&&l.max_read<=65536&&l.max_bytes&&l.max_bytes<=64*1024*1024&&l.max_calls&&l.max_calls<=100000&&l.max_milliseconds&&l.max_milliseconds<=30000,"invalid read budget");
    }
    void location(Location value)const noexcept{if(stats)stats->failure_location=value;}
    void phase(Phase value)noexcept{current_phase=value;if(stats){stats->phase=value;stats->failure_location=Location::validation;}}
    void elapsed(U now)const noexcept{if(stats){stats->clock_regressed=stats->clock_regressed||now<started;stats->elapsed_milliseconds=now>=started?now-started:0;}}
    void check(Location where=Location::precheck)const{const U now=memory.work_milliseconds();location(where);elapsed(now);
        need(now>=started&&now-started<limits.max_milliseconds,"owner observation deadline exceeded");location(Location::validation);}
    bool try_read(U address,void* dst,std::size_t count){
        check();location(Location::range);need(range(address,count)&&count<=limits.max_read,"invalid read range");
        location(Location::budget);
        need(calls<limits.max_calls&&count<=limits.max_bytes-bytes,"owner read budget exceeded");
        ++calls;bytes+=count;if(stats){stats->attempted_calls=calls;stats->attempted_bytes=bytes;}
        location(Location::read);const bool readable=memory.read(address,dst,count);
        if(readable&&stats){++stats->completed_calls;stats->completed_bytes+=count;}
        if(readable)check(Location::postcheck);
        return readable;
    }
    void read(U address,void* dst,std::size_t count){need(try_read(address,dst,count),"unreadable owner range");}
    Bytes span(U address,U count,bool guard=true){
        need(range(address,count)&&count<=limits.max_bytes,"invalid bounded span");Bytes result(static_cast<std::size_t>(count));
        for(std::size_t offset=0;offset<result.size();){const auto n=std::min(limits.max_read,result.size()-offset);read(add(address,offset),result.data()+offset,n);if(guard){guards.push_back({add(address,offset),Bytes(result.begin()+offset,result.begin()+offset+n),current_phase});if(stats)stats->guard_total=guards.size();}offset+=n;}
        return result;
    }
    template<class T=U> T scalar(U address){return get<T>(span(address,sizeof(T)),0);}
    void verify(const std::vector<NativeOwnerGuard>& saved){
        std::size_t ordinal=0;if(stats)stats->guard_total=saved.size();
        // Every retained range still compares in its original order. Batched
        // bytes are freshly read in this pass and destroyed on return.
        Bytes current(limits.max_read);
        auto batches=OwnerReadBatches::prepare(memory,limits,calls,bytes,saved);
        for(const auto& g:saved){if(stats)stats->guard_ordinal=ordinal;location(Location::validation);
            need(!g.bytes.empty()&&g.bytes.size()<=limits.max_read,"invalid owned guard");
            if(stats){stats->guard_capture_phase=g.capture_phase;stats->guard_span_bytes=g.bytes.size();stats->guard_difference_offset=0;}
            bool from_batch=false;
            const auto group=batches.lookup.empty()?OwnerReadBatches::absent:batches.lookup[ordinal];
            if(group!=OwnerReadBatches::absent){auto& batch=batches.groups[group];
                if(!batch.state){
                    const bool readable=try_read(batch.address,batches.data.data()+batch.offset,batch.size);
                    if(!readable)check(Location::postcheck);
                    batch.state=readable?1:2;
                }
                if(batch.state==1){check();std::memcpy(current.data(),batches.data.data()+batch.offset+std::size_t(g.address-batch.address),g.bytes.size());check(Location::postcheck);from_batch=true;}
            }
            if(!from_batch)read(g.address,current.data(),g.bytes.size());
            location(Location::compare);
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
    Image(Reader& r,const NativeOwnerImage& i,const NativeOwnerCachePrefix& verified):m(r),image(i),executable(verified.executable){}
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
using Descriptor=NativeOwnerCachePrefix::Descriptor;
using Registration=NativeOwnerCachePrefix::Registration;
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
Bytes entity_equipment(Reader& m,const std::vector<Registration>& registrations,NativeOwnerCandidate& c,bool allow_absent=false,bool* absent=nullptr){
    auto selected=[&](const char* name)->const Registration&{auto it=std::find_if(registrations.begin(),registrations.end(),[&](const auto& r){return r.storage.name==name;});need(it!=registrations.end()&&it->storage.structure,"selected reflected runtime struct missing");return *it;};
    const auto& input=selected("keen::ecs::ClientPlayerInput");const auto& equipment=selected("keen::ecs::RenderEquipment");need(input.size==1536&&equipment.size==788,"selected registration size mismatch");c.input_registration=input.index;c.equipment_registration=equipment.index;
    U capacity=m.scalar(c.entity_manager+e::entityManagerCount);need(capacity&&capacity<=e::maximumEntities&&!(capacity&(capacity-1)),"invalid entity table capacity");U table=m.scalar(c.entity_manager+e::entityManagerTable),occupancy=m.scalar(c.entity_manager+e::entityManagerOccupancy),keys=m.scalar(c.entity_manager+e::entityManagerKeys);
    need(range(table,mul(capacity,8))&&range(keys,mul(capacity,4))&&range(occupancy,((capacity+63)/64)*8),"invalid entity map ranges");
    // The original local-player predicate3bb660 compares the entity ID with
    // LocalPlayerData+4. The service and original bindings are in the verified
    // prefix; this mutable selector belongs to every fresh capture/submission.
    if(!c.account){need(range(c.local_player_data,8),"local player service unavailable");c.entity_id=m.scalar<std::uint32_t>(add(c.local_player_data,4));}
    need(c.entity_id,"local player selector absent");
    std::uint32_t hash=c.entity_id;for(unsigned i=0;i<2;++i)hash=((hash>>16)^hash)*0x45D9F3Bu;U start=(hash^(hash>>16))&(capacity-1);bool found=false;
    for(U probe=0;probe<std::min<U>(capacity,8192);++probe){const U slot=(start+probe)&(capacity-1);const auto occupied=m.scalar<std::uint8_t>(add(occupancy,slot/8));
        if(!((occupied>>(slot%8))&1)){
            if(allow_absent&&absent){*absent=true;return {};}
            need(false,"local entity outside original ID hash chain");
        }
        if(m.scalar<std::uint32_t>(add(keys,slot*4))!=c.entity_id)continue;
        c.entity=m.scalar(add(table,slot*8));c.entity_slot=static_cast<std::uint32_t>(slot);found=true;break;}
    need(found,"local entity hash probe bound exceeded");need(range(c.entity,52),"selected entity range invalid");const auto header=m.span(add(c.entity,16),36);
    need(get<std::uint32_t>(header,0)==c.entity_id,"local entity header ID mismatch");c.entity_generation=get<std::uint32_t>(header,4);c.entity_row=get<std::uint32_t>(header,32);c.entity_layout=get<U>(header,8);c.entity_storage=get<U>(header,16);
    need(range(c.entity_layout,1)&&range(c.entity_storage,1)&&c.entity_row<=1u<<24,"invalid selected entity layout/storage/row");
    const U bit_size=((registrations.size()+63)/64)*8;need(bit_size<=e::componentOffsets&&registrations.size()*2<=e::componentStrides-e::componentOffsets,"component layout arrays overlap");
    const auto presence=m.span(c.entity_layout,bit_size);need((presence[input.index/8]>>(input.index%8))&1,"local selector entity has no ClientPlayerInput");
    if(c.account){const auto& state=selected("keen::ecs::PlayerState");need(state.size==96&&((presence[state.index/8]>>(state.index%8))&1),"selected actor has no PlayerState");
        const U offset=m.scalar<std::uint16_t>(add(c.entity_layout,e::componentOffsets+state.index*2)),stride=m.scalar<std::uint16_t>(add(c.entity_layout,e::componentStrides+state.index*2));
        need(stride==96&&range(add(add(c.entity_storage,offset),mul(c.entity_row,stride)),stride),"selected PlayerState stride/address mismatch");}
    m.phase(Phase::equipment);
    need((presence[equipment.index/8]>>(equipment.index%8))&1,"RenderEquipment absent on local entity");
    for(const auto* entry:{&input,&equipment}){U offset=m.scalar<std::uint16_t>(add(c.entity_layout,e::componentOffsets+entry->index*2));U stride=m.scalar<std::uint16_t>(add(c.entity_layout,e::componentStrides+entry->index*2));need(stride==entry->size&&stride,"selected runtime component stride mismatch");U address=add(add(c.entity_storage,offset),mul(c.entity_row,stride));need(range(address,stride),"selected component address out of bounds");if(entry==&equipment)c.equipment=address;}
    // Pinned RenderEquipment modelHandles[24]@0 is the ownership input.
    // The remaining692 bytes are visual caches/colors/hide and pose state;
    // hideMask@768 changes normally near camera occlusion. Keep the complete
    //788-byte registered stride/address check above, but guard only consumed
    // model handles, each still validated against its allocation generation.
    return m.span(c.equipment,24u*sizeof(std::uint32_t));
}
void service_owners(Reader& m,Image& image,NativeOwnerCandidate& candidate,bool players=false){
    const U registry_address=candidate.registry;
    U rows=m.scalar(add(registry_address,s::recordsPointerOffset)),count=m.scalar(add(registry_address,s::recordCountOffset));need(count&&count<=s::maximumRecords,"invalid service count");
    U bits=m.scalar(add(registry_address,s::mapBitsOffset)),capacity=m.scalar(add(registry_address,s::mapCapacityOffset)),keys=m.scalar(add(registry_address,s::mapKeysOffset)),values=m.scalar(add(registry_address,s::mapValuesOffset));auto used=m.scalar<std::uint32_t>(add(registry_address,s::mapCountOffset));need(capacity&&capacity<=s::maximumMapCapacity&&!(capacity&(capacity-1))&&used&&used<=std::min(count,capacity),"invalid service map dimensions");
    auto occupancy=m.span(bits,((capacity+63)/64)*8),keydata=m.span(keys,capacity*4),valuedata=m.span(values,capacity*2);auto bit=[&](U index){return (occupancy[index/8]>>(index%8))&1;};U population=0;for(U i=0;i<occupancy.size()*8;++i){if(i>=capacity)need(!bit(i),"service occupancy exceeds capacity");else population+=bit(i);}need(population==used,"service occupancy/count disagreement");
    std::vector<std::uint32_t> hashes;std::set<std::string> names;std::set<std::uint32_t> unique_hashes;
    const char* required[]={"RenderScene","LocalPlayerData","BasicPlayerInfos"};U selected[3]={},resolved[3]={};unsigned selected_count=0;const unsigned required_count=players?3:2;
    for(U index=0;index<count;++index){U record=add(rows,mul(index,s::recordStride));auto view=m.span(record,16);std::string name=image.text(get<U>(view,0),get<U>(view,8),128,false);auto hash=m.scalar<std::uint32_t>(add(record,s::recordHashOffset));need(hash==fnv(name)&&names.insert(name).second&&unique_hashes.insert(hash).second,"duplicate service name/hash or hash mismatch");hashes.push_back(hash);
        for(unsigned i=0;i<required_count;++i)if(name==required[i]){selected[i]=index;++selected_count;}}
    need(selected_count==required_count,"required owner service missing or ambiguous");std::set<U> seen_indices;std::set<std::uint32_t> seen_keys;
    for(U slot=0;slot<capacity;++slot)if(bit(slot)){U index=get<std::uint16_t>(valuedata,slot*2);auto hash=get<std::uint32_t>(keydata,slot*4);need(index<count&&seen_indices.insert(index).second&&seen_keys.insert(hash).second&&hashes[index]==hash,"service map record disagreement");}
    for(unsigned i=0;i<required_count;++i){U slot=fnv(required[i])&(capacity-1);bool found=false;
        for(U probe=0;probe<capacity;++probe){need(bit(slot),"owner service outside original hash probe chain");if(get<std::uint32_t>(keydata,slot*4)==fnv(required[i])){need(get<std::uint16_t>(valuedata,slot*2)==selected[i],"owner service map selector mismatch");found=true;break;}slot=(slot+1)&(capacity-1);}need(found,"service hash probe bound exceeded");
        const U row=add(rows,mul(selected[i],s::recordStride));resolved[i]=m.scalar(add(row,s::recordNormalPointerOffset));need(range(resolved[i],i?8:0xA0)&&m.scalar(add(row,s::recordAlternatePointerOffset))==resolved[i],"owner service normal/alternate pointer mismatch");}
    const char* equipment_services[]={"BasicPlayerInfos","CharacterPresetRegistry","ColorGradientRegistry","MaterialFeedbackRegistry","g38_items::ItemRegistry","RenderScene"};
    const char* hide_services[]={"LocalPlayerCamera","LocalPlayerData"};
    const char* ui_services[]={"BasicPlayerInfos","CharacterPresetRegistry","LocalPlayerData","g38_items::ItemRegistry","G38UiPlayData"};
    struct System {U descriptor,function;const char* name;const char* const* names;unsigned reads,writes,ordinal,owner;};
    const System contracts[]={
        {0x1dfe7e0,0x3892c0,"render_equipment_models_ag2",equipment_services,5,1,5,0},
        {0x1dfe0f0,0x388d00,"local_player_hide",hide_services,2,0,1,1},
        {0x1dfe340,0x388e60,"local_player_ui_render_setup",ui_services,4,1,2,1}};
    for(const auto& contract:contracts){const U descriptor=add(image.image.base,contract.descriptor),function=add(image.image.base,contract.function);
        need(image.inside(descriptor,0xC0)&&image.code(function),"system metadata/function outside pinned image");const auto desc=m.span(descriptor,0xC0);
        need(image.text(get<U>(desc,0),get<U>(desc,8),128,true)==contract.name&&get<U>(desc,16)==function,"original system full name/function mismatch");
        for(unsigned access=0;access<2;++access){const U offset=access?0xB0:0x98,pointer=get<U>(desc,offset),length=get<U>(desc,offset+8);need(length==(access?contract.writes:contract.reads)&&bool(pointer)==bool(length),"original system service array mismatch");if(!length)continue;
            need(image.inside(pointer,length*16),"original system service names outside image");const auto views=m.span(pointer,length*16);for(U i=0;i<length;++i)need(image.text(get<U>(views,i*16),get<U>(views,i*16+8),128,true)==contract.names[(access?contract.reads:0)+i],"original system service ordinal mismatch");}}
    const U systems=m.scalar<std::uint16_t>(add(registry_address,s::systemCountOffset));need(systems&&systems<=s::maximumSystems,"invalid system count");U system_rows[3]={};
    for(U index=0;index<systems;++index){const U address=add(add(registry_address,s::systemsOffset),mul(index,s::systemStride)),descriptor=m.scalar(address);
        for(unsigned i=0;i<3;++i)if(descriptor==add(image.image.base,contracts[i].descriptor)){need(!system_rows[i],"original owner system ambiguous");system_rows[i]=address;}}
    for(unsigned i=0;i<3;++i){const auto& contract=contracts[i];const U row=system_rows[i];need(row,"original owner system missing");need(m.scalar(add(row,0x28))==add(image.image.base,contract.function),"system function mismatch");
        need(m.scalar<std::uint8_t>(add(row,s::systemNormalCountOffset))==contract.reads+contract.writes&&m.scalar<std::uint8_t>(add(row,s::systemAlternateCountOffset))==contract.reads+contract.writes,"system service counts mismatch");
        for(U bindings:{s::systemNormalBindingsOffset,s::systemAlternateBindingsOffset})need(m.scalar(add(row,bindings+contract.ordinal*8))==resolved[contract.owner],"original owner service binding mismatch");}
    constexpr std::uint8_t predicate[]={0x39,0x4a,0x04,0x0f,0x94,0xc0,0xc3};const U predicate_address=add(image.image.base,0x3bb740);need(image.code(predicate_address),"local predicate outside code");
    const auto code=m.span(predicate_address,sizeof predicate);need(std::memcmp(code.data(),predicate,sizeof predicate)==0,"original local predicate mismatch");
    candidate.render_scene=resolved[0];candidate.local_player_data=resolved[1];
    if(players){
        candidate.player_infos=resolved[2];need(range(candidate.player_infos,16*160),"player info service range invalid");
        // Both original RenderEquipment and local UI systems bind this same named service at ordinal zero.
        for(unsigned i:{0u,2u})for(U bindings:{s::systemNormalBindingsOffset,s::systemAlternateBindingsOffset})
            need(m.scalar(add(system_rows[i],bindings))==candidate.player_infos,"original player info service binding mismatch");
        for(const auto& pin:p::playerIdentity::code){const U at=add(image.image.base,pin.rva);need(image.code(at)&&image.inside(at,pin.size),"player identity code outside image");
            const auto bytes=m.span(at,pin.size);need(std::memcmp(bytes.data(),pin.bytes,pin.size)==0,"original player identity code mismatch");}
    }
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
    U device=m.scalar(add(owner,8));need(range(device,0x280),"resource GPU device invalid");U interface=m.scalar(add(device,0x278));need(range(interface,8),"GPU backend interface invalid");U vtable=m.scalar(interface);need(image.inside(add(image.image.base,0x1dc6cf0),0x68)&&vtable==add(image.image.base,0x1dc6cf0),"GPU backend vtable mismatch");
    U thunk=m.scalar(add(vtable,0x60));need(thunk==add(image.image.base,0xfd3190)&&image.code(thunk),"GPU buffer constructor thunk mismatch");c.resource_object=m.scalar(add(table,U(index)*8));auto dimension=m.span(add(dims,U(index)*16),16),prefix=m.span(c.resource_object,32);U size=get<U>(prefix,8),buffer=get<U>(prefix,24);auto usage=get<std::uint32_t>(prefix,16);
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
        // Exact-image player Character callbacks use this Main-owned
        // renderer. It owns its own manager, not the gameplay RenderEquipment.
        c.renderer=m.scalar(allocation.main+0x466b00);
        allocation.render_scene=m.scalar(add(c.renderer,0x20));
        // The original scene accessor forwards through its +0x98 manager.
        allocation.model_manager=m.scalar(add(allocation.render_scene,0x98));
        const U body=m.scalar(add(c.renderer,0x58)),count=m.scalar(add(c.renderer,0x60)),capacity=m.scalar(add(c.renderer,0x68));
        need(count<=capacity&&capacity&&capacity<=64&&range(body,mul(capacity,4)),"preview body allocation invalid");
        std::vector<std::uint32_t> handles;handles.reserve(112);
        if(count){const auto values=m.span(body,count*4);for(U i=0;i<count;++i)handles.push_back(get<std::uint32_t>(values,i*4));}
        //The original Character owns the body vector and two model handles in each
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
namespace {
void complete_owner_capture(Reader& m,Image& image,const std::vector<Registration>& registrations,NativeOwnerCandidate& candidate){
    m.phase(Phase::census);auto equipment=entity_equipment(m,registrations,candidate);
    m.phase(Phase::models);models_and_buffer(m,image,equipment,candidate);
    m.phase(Phase::capture_verify);m.verify(m.guards);
    candidate.read_calls=m.calls;candidate.read_bytes=m.bytes;const U now=m.memory.work_milliseconds();candidate.elapsed_milliseconds=now-m.started;m.elapsed(now);
    candidate.guards=std::move(m.guards);
}
bool capture_owner_complete(const NativeOwnerMemory& memory,const NativeOwnerImage& identity,NativeOwnerCandidate& out,std::string& why,
                            const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats,std::shared_ptr<const NativeOwnerCachePrefix>* prefix)noexcept{
    if(stats){*stats={};stats->phase=Phase::image;stats->failure_location=Location::validation;}
    // Copy identity first: callers may pass out.image by reference.
    const NativeOwnerImage identity_copy=identity;out={};why.clear();if(prefix)prefix->reset();
    try{Reader m(memory,limits,stats);NativeOwnerCandidate candidate;candidate.image=identity_copy;Image image(m,candidate.image);
        m.phase(Phase::owners);owners(m,candidate.image,candidate);m.phase(Phase::registry);auto registrations=registry(m,image,candidate.registry);
        m.phase(Phase::services);service_owners(m,image,candidate);
        const auto prefix_guards=m.guards.size();complete_owner_capture(m,image,registrations,candidate);
        if(prefix){auto decoded=std::make_shared<NativeOwnerCachePrefix>();decoded->registrations=std::move(registrations);
            decoded->executable=std::move(image.executable);decoded->guard_count=prefix_guards;*prefix=std::move(decoded);}
        out=std::move(candidate);return true;
    }catch(const Rejected& failure){reason_set(why,failure.reason);}catch(...){if(stats)stats->failure_location=Location::internal;reason_set(why,"owner observation allocation/internal failure");}out={};return false;
}
// Called only after cache verification compared every prefix guard successfully
// and first differed in census. Preserve that owned prefix in its original
// order, recapture the entire tail, then verify ALL prefix and tail bytes again.
bool capture_owner_after_verified_prefix(const NativeOwnerMemory& memory,const NativeOwnerCandidate& previous,const NativeOwnerCachePrefix& prefix,
                                        NativeOwnerCandidate& out,std::string& why,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats)noexcept{
    out={};why.clear();if(stats){*stats={};stats->phase=Phase::census;stats->failure_location=Location::validation;}
    try{
        need(prefix.guard_count&&prefix.guard_count<previous.guards.size()&&!prefix.registrations.empty()&&!prefix.executable.empty(),"invalid verified owner prefix");
        Reader m(memory,limits,stats);auto candidate=previous;candidate.guards={};candidate.models.clear();
        m.guards.assign(previous.guards.begin(),previous.guards.begin()+prefix.guard_count);
        Image image(m,candidate.image,prefix);complete_owner_capture(m,image,prefix.registrations,candidate);
        out=std::move(candidate);return true;
    }catch(const Rejected& failure){reason_set(why,failure.reason);}catch(...){if(stats)stats->failure_location=Location::internal;reason_set(why,"owner observation allocation/internal failure");}
    out={};return false;
}
}
bool capture_native_owner(const NativeOwnerMemory& memory,const NativeOwnerImage& identity,NativeOwnerCandidate& out,std::string& why,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats)noexcept{
    return capture_owner_complete(memory,identity,out,why,limits,stats,nullptr);
}
namespace {
template<class T> T guarded_value(const NativeOwnerCandidate& c,U address){
    for(const auto& g:c.guards)if(address>=g.address&&sizeof(T)<=g.bytes.size()&&address-g.address<=g.bytes.size()-sizeof(T)){
        T value;std::memcpy(&value,g.bytes.data()+std::size_t(address-g.address),sizeof value);return value;}
    need(false,"player owner metadata lacks consumed guard");return T{};
}
void player_candidate_metadata(const NativeOwnerCandidate& c){
    need(image_identity(c.image)&&c.entity&&c.entity_id&&c.entity_id<=16&&c.account&&c.connection&&c.player_infos&&
        c.player_record==add(c.player_infos,U(c.entity_id-1)*160)&&(c.connection&63)==c.entity_id-1&&
        !c.models.empty()&&c.models.size()<=24&&!c.guards.empty(),"invalid player owner association");
    need(guarded_value<std::uint32_t>(c,c.player_record)==c.entity_id&&guarded_value<std::uint32_t>(c,add(c.player_record,4))==c.connection&&
        guarded_value<U>(c,add(c.player_record,8))==c.account,"player owner metadata disagrees with retained association");
    need(guarded_value<std::uint32_t>(c,add(c.entity,16))==c.entity_id&&guarded_value<std::uint32_t>(c,add(c.entity,20))==c.entity_generation&&
        guarded_value<U>(c,add(c.entity,24))==c.entity_layout&&guarded_value<U>(c,add(c.entity,32))==c.entity_storage&&
        guarded_value<std::uint32_t>(c,add(c.entity,48))==c.entity_row,"player entity metadata disagrees with retained header");
    const U table=guarded_value<U>(c,add(c.entity_manager,e::entityManagerTable)),keys=guarded_value<U>(c,add(c.entity_manager,e::entityManagerKeys));
    need(guarded_value<U>(c,add(table,U(c.entity_slot)*8))==c.entity&&guarded_value<std::uint32_t>(c,add(keys,U(c.entity_slot)*4))==c.entity_id,"player entity hash slot metadata mismatch");
    const auto registrations=guarded_value<U>(c,add(c.registry,8));
    need(guarded_value<std::uint32_t>(c,add(registrations,U(c.input_registration)*256+32))==fnv("keen::ecs::ClientPlayerInput")&&
        guarded_value<std::uint32_t>(c,add(registrations,U(c.equipment_registration)*256+32))==fnv("keen::ecs::RenderEquipment"),"player component registration metadata mismatch");
    const auto offset=guarded_value<std::uint16_t>(c,add(c.entity_layout,e::componentOffsets+U(c.equipment_registration)*2));
    const auto stride=guarded_value<std::uint16_t>(c,add(c.entity_layout,e::componentStrides+U(c.equipment_registration)*2));
    need(stride==788&&c.equipment==add(add(c.entity_storage,offset),mul(c.entity_row,stride)),"player equipment metadata mismatch");
    need(guarded_value<U>(c,add(c.render_scene,0x98))==c.model_manager&&guarded_value<U>(c,add(c.model_manager,0x948))==c.writer&&
        guarded_value<U>(c,add(c.model_manager,0x1d0))==c.instance_capacity&&
        guarded_value<std::uint32_t>(c,add(c.writer,0xc0))==c.instance_capacity&&guarded_value<std::uint32_t>(c,add(c.writer,0xc4))==c.instance_extra&&
        c.native_buffer_bytes==(U(c.instance_capacity)+c.instance_extra)*128&&
        guarded_value<U>(c,add(c.resource_object,24))==c.native_buffer&&guarded_value<U>(c,add(c.resource_object,8))==c.native_buffer_bytes,
        "player native allocation metadata mismatch");
    const U records=guarded_value<U>(c,add(c.model_manager,0x1b0)),generations=guarded_value<U>(c,add(c.model_manager,0x1c8));
    unsigned handles=0;for(unsigned slot=0;slot<24;++slot)handles+=guarded_value<std::uint32_t>(c,add(c.equipment,U(slot)*4))!=0;
    need(handles==c.models.size(),"player model list is incomplete");std::set<std::uint32_t> slots,indices;
    for(const auto& model:c.models){need(model.equipment_slot<24&&model.index<c.instance_capacity&&model.generation<=255&&
        model.handle==((model.generation<<24)|model.index)&&model.handle==guarded_value<std::uint32_t>(c,add(c.equipment,U(model.equipment_slot)*4))&&
        model.record==add(records,mul(model.index,432))&&guarded_value<std::uint8_t>(c,add(generations,model.index))==model.generation&&
        slots.insert(model.equipment_slot).second&&indices.insert(model.index).second,"player model metadata disagrees with consumed equipment");}
}
}
bool revalidate_native_owner(const NativeOwnerMemory& memory,NativeOwnerCandidate& candidate,std::string& why,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats)noexcept{
    if(stats){*stats={};stats->phase=Phase::submission_verify;stats->failure_location=Location::validation;stats->guard_total=candidate.guards.size();}
    why.clear();try{need(image_identity(candidate.image)&&candidate.native_buffer&&candidate.native_buffer_bytes&&candidate.entity&&candidate.entity_id&&!candidate.models.empty()&&candidate.models.size()<=24&&!candidate.guards.empty(),"invalid native owner candidate");
        auto bounded=limits;
        if(candidate.account||candidate.connection||candidate.player_infos||candidate.player_record){
            const U started=memory.work_milliseconds();player_candidate_metadata(candidate);const U now=memory.work_milliseconds();
            bounded.max_milliseconds=std::min<U>(bounded.max_milliseconds,40);
            need(now>=started&&now-started<bounded.max_milliseconds,"player owner metadata deadline exceeded");
            bounded.max_milliseconds-=now-started;
        }
        Reader m(memory,bounded,stats);m.verify(candidate.guards.view());return true;
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
    OwnerCallBudget(const NativeOwnerMemory& m,const NativeOwnerLimits& l):source(m),limits(l),started(m.work_milliseconds()),last(started){}
    U milliseconds()const noexcept override {
        return source.milliseconds();
    }
    U work_milliseconds()const noexcept override {
        last=source.work_milliseconds();regressed=regressed||last<started;return last;
    }
    bool timed_out()const noexcept {return regressed||last<started||last-started>=limits.max_milliseconds;}
    bool available()const noexcept {return !timed_out()&&!read_budget&&calls<limits.max_calls&&bytes<limits.max_bytes;}
    bool permit_read_ahead(std::size_t wanted_calls,std::size_t wanted_bytes)const noexcept override {
        return available()&&wanted_calls<=limits.max_calls-calls&&wanted_bytes<=limits.max_bytes-bytes&&source.permit_read_ahead(wanted_calls,wanted_bytes);
    }
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
    bool accepted=false,hit=false,resumed=false;
    try{
        if(source_!=&memory||!same_owner_image(identity,cached_.image))clear();
        if(!cached_.guards.empty()) {
            const auto previous=cached_;
            accepted=revalidate_native_owner(budget,cached_,why,limits,&progress);hit=accepted;
            if(!accepted&&budget.available()&&prefix_&&progress.phase==Phase::submission_verify&&
                progress.failure_location==Location::compare&&progress.guard_capture_phase==Phase::census&&
                progress.guard_ordinal>=prefix_->guard_count){
                resumed=true;accepted=capture_owner_after_verified_prefix(budget,previous,*prefix_,cached_,why,limits,&progress);
            }
            progress.phase=Phase::capture_verify;
        }
        if(!accepted&&!resumed&&budget.available()) {
            clear();accepted=capture_owner_complete(budget,identity,cached_,why,limits,&progress,&prefix_);
        }
        if(accepted)out=cached_; // Caller mutations cannot change retained guards/identity.
    }catch(...){progress.failure_location=Location::internal;reason_set(why,"owner observation allocation/internal failure");accepted=false;}
    (void)budget.work_milliseconds();
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

namespace evrm { namespace {
void validate_player_targets(const std::vector<NativePlayerIdentity>& targets){
    need(!targets.empty()&&targets.size()<=16,"requested player count outside roster bound");
    std::set<U> accounts;std::set<std::uint32_t> actors,connections;unsigned locals=0;
    for(const auto& t:targets){
        need(t.account&&t.entity_id&&t.entity_id<=16&&t.connection&&(t.connection&63)==t.entity_id-1&&
            accounts.insert(t.account).second&&actors.insert(t.entity_id).second&&connections.insert(t.connection).second,
            "invalid or duplicate requested player association");
        locals+=t.local;need(locals<=1,"multiple requested local player associations");
    }
}
enum class PlayerGroupMode { Identity, Strict, Available };
bool capture_player_group(const NativeOwnerMemory& memory,const NativeOwnerImage& identity,NativePlayerRoster& out,
    std::vector<NativePlayerOwnerResult>& candidates,NativeOwnerGuards& proof,PlayerGroupMode mode,std::string& why,const NativeOwnerLimits& requested,NativeOwnerObservationStats* stats,
    NativeOwnerCandidate& saved,std::shared_ptr<const NativeOwnerCachePrefix>& decoded,const NativeOwnerMemory*& source)noexcept {
    const auto image_identity_copy=identity;out={};candidates.clear();proof={};why.clear();if(stats)*stats={};
    // A whole roster shares one Reader and one absolute 40ms work allowance.
    auto limits=requested;limits.max_milliseconds=std::min<U>(limits.max_milliseconds,40);
    try {
        Reader m(memory,limits,stats);const bool reuse=source==&memory&&decoded&&!saved.guards.empty()&&same_owner_image(saved.image,image_identity_copy);
        if(!reuse){saved={};decoded.reset();source=nullptr;}
        NativeOwnerCandidate prefix;
        if(reuse){m.phase(Phase::capture_verify);m.verify(saved.guards.view());prefix=saved;m.guards.assign(saved.guards.begin(),saved.guards.end());}
        else {
            prefix.image=image_identity_copy;Image image(m,prefix.image);m.phase(Phase::owners);owners(m,prefix.image,prefix);
            m.phase(Phase::registry);auto registrations=registry(m,image,prefix.registry);m.phase(Phase::services);service_owners(m,image,prefix,true);
            auto metadata=std::make_shared<NativeOwnerCachePrefix>();metadata->registrations=std::move(registrations);metadata->executable=std::move(image.executable);metadata->guard_count=m.guards.size();decoded=std::move(metadata);
        }
        Image image(m,prefix.image,*decoded);const auto prefix_count=m.guards.size();m.phase(Phase::census);
        const auto local=m.scalar<std::uint32_t>(add(prefix.local_player_data,4));need(local&&local<=16,"explicit local actor selector absent or outside roster");
        std::set<U> accounts;std::set<std::uint32_t> connections;NativePlayerRoster roster;unsigned local_count=0;
        // Guard scalar prefixes only. No display-name or other player payload reads.
        for(unsigned slot=0;slot<16;++slot){const auto bytes=m.span(add(prefix.player_infos,U(slot)*160),16);
            const auto actor=get<std::uint32_t>(bytes,0),connection=get<std::uint32_t>(bytes,4);const auto account=get<U>(bytes,8);
            if(!connection){need(!actor,"inactive player actor mismatch");continue;}
            need(actor==slot+1&&(connection&63)==slot&&account,"original connection/actor/slot mapping mismatch");
            need(accounts.insert(account).second&&connections.insert(connection).second,"ambiguous player account/connection association");
            roster.players.push_back({account,actor,connection,actor==local});local_count+=actor==local;
        }
        need(local_count==1,"local selector has no unique current player association");
        std::set<std::uint32_t> model_indices;std::set<U> entity_addresses;
        for(const auto& player:roster.players){if(mode==PlayerGroupMode::Identity)break;auto c=prefix;c.guards={};c.models.clear();c.account=player.account;c.connection=player.connection;c.entity_id=player.entity_id;c.player_record=add(prefix.player_infos,U(player.entity_id-1)*160);
            m.phase(Phase::census);bool absent=false;
            const auto equipment=entity_equipment(m,decoded->registrations,c,mode==PlayerGroupMode::Available&&!player.local,&absent);
            if(absent){candidates.push_back({player,NativePlayerOwnerStatus::EntityAbsent,{}});continue;}
            m.phase(Phase::models);models_and_buffer(m,image,equipment,c);
            need(entity_addresses.insert(c.entity).second,"overlapping player entities");
            for(const auto& model:c.models)need(model_indices.insert(model.index).second,"overlapping player equipment model ownership");
            candidates.push_back({player,NativePlayerOwnerStatus::Ready,std::move(c)});
        }
        m.phase(Phase::capture_verify);m.verify(m.guards);const U now=memory.work_milliseconds();m.check(Location::finalcheck);
        NativeOwnerGuards all(std::move(m.guards));for(auto& row:candidates)if(row.status==NativePlayerOwnerStatus::Ready){auto& c=row.candidate;c.guards=all;c.read_calls=m.calls;c.read_bytes=m.bytes;c.elapsed_milliseconds=now-m.started;}
        proof=all;
        if(!reuse){std::vector<NativeOwnerGuard> prefix_guards(all.begin(),all.begin()+prefix_count);prefix.guards=std::move(prefix_guards);saved=std::move(prefix);source=&memory;}
        m.check(Location::finalcheck);roster.observed_milliseconds=memory.milliseconds();out=std::move(roster);return true;
    }catch(const Rejected& error){reason_set(why,error.reason);}catch(...){if(stats)stats->failure_location=Location::internal;reason_set(why,"player ownership observation unavailable");}
    saved={};decoded.reset();source=nullptr;candidates.clear();proof={};out={};return false;
}
}
bool NativeOwnerCache::players(const NativeOwnerMemory& memory,const NativeOwnerImage& image,NativePlayerRoster& out,std::string& why,const NativeOwnerLimits& limits)noexcept {
    std::vector<NativePlayerOwnerResult> candidates;NativeOwnerGuards proof;return capture_player_group(memory,image,out,candidates,proof,PlayerGroupMode::Identity,why,limits,nullptr,player_prefix_,player_decoded_,player_source_);
}
bool NativeOwnerCache::capture_players(const NativeOwnerMemory& memory,const NativeOwnerImage& image,const std::vector<NativePlayerIdentity>& targets,std::vector<NativeOwnerCandidate>& out,std::string& why,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats)noexcept {
    const auto identity=image;out.clear();why.clear();if(stats)*stats={};const U started=memory.work_milliseconds();
    try{
        validate_player_targets(targets);
        NativePlayerRoster roster;std::vector<NativePlayerOwnerResult> candidates;NativeOwnerGuards proof;
        if(!capture_player_group(memory,identity,roster,candidates,proof,PlayerGroupMode::Strict,why,limits,stats,player_prefix_,player_decoded_,player_source_))return false;
        std::vector<NativeOwnerCandidate> selected;selected.reserve(targets.size());
        for(const auto& target:targets){bool found=false;
            for(std::size_t i=0;i<roster.players.size();++i){const auto& p=roster.players[i];
                if(p.account==target.account&&p.entity_id==target.entity_id&&p.connection==target.connection&&p.local==target.local){selected.push_back(std::move(candidates[i].candidate));found=true;break;}}
            need(found,"requested player association is no longer current");
        }
        const U finished=memory.work_milliseconds();
        if(stats){stats->elapsed_milliseconds=finished>=started?finished-started:0;stats->clock_regressed=stats->clock_regressed||finished<started;}
        need(finished>=started&&finished-started<std::min<U>(limits.max_milliseconds,40),"player owner batch deadline exceeded");
        out=std::move(selected);return true;
    }catch(const Rejected& error){reason_set(why,error.reason);}catch(...){reason_set(why,"player owner batch unavailable");}
    out.clear();return false;
}
NativePlayerOwnerBatch NativeOwnerCache::capture_available_players(const NativeOwnerMemory& memory,const NativeOwnerImage& image,const std::vector<NativePlayerIdentity>& targets,std::string& why,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats)noexcept {
    const auto identity=image;why.clear();if(stats)*stats={};const U started=memory.work_milliseconds();
    try{
        validate_player_targets(targets);
        NativePlayerRoster roster;std::vector<NativePlayerOwnerResult> current;NativeOwnerGuards proof;
        if(!capture_player_group(memory,identity,roster,current,proof,PlayerGroupMode::Available,why,limits,stats,player_prefix_,player_decoded_,player_source_))return {};
        NativePlayerOwnerBatch batch;batch.players.reserve(targets.size());
        for(const auto& target:targets){NativePlayerOwnerResult row;row.identity=target;
            for(auto& present:current){const auto& p=present.identity;
                if(p.account==target.account&&p.entity_id==target.entity_id&&p.connection==target.connection&&p.local==target.local){row=std::move(present);break;}}
            batch.players.push_back(std::move(row));
        }
        const U finished=memory.work_milliseconds();
        if(stats){stats->elapsed_milliseconds=finished>=started?finished-started:0;stats->clock_regressed=stats->clock_regressed||finished<started;}
        need(finished>=started&&finished-started<std::min<U>(limits.max_milliseconds,40),"player owner batch deadline exceeded");
        batch.proof=std::move(proof);batch.valid=true;return batch;
    }catch(const Rejected& error){reason_set(why,error.reason);}catch(...){reason_set(why,"available player owner batch unavailable");}
    return {};
}
bool NativeOwnerCache::capture_player(const NativeOwnerMemory& memory,const NativeOwnerImage& image,const NativePlayerIdentity& requested,NativeOwnerCandidate& out,std::string& why,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats)noexcept {
    const auto target=requested;const auto identity=image;out={};
    try{std::vector<NativeOwnerCandidate> candidates;
        if(!capture_players(memory,identity,std::vector<NativePlayerIdentity>{target},candidates,why,limits,stats))return false;
        out=std::move(candidates.front());return true;
    }catch(...){reason_set(why,"player owner capture unavailable");return false;}
}
bool capture_native_players(const NativeOwnerMemory& memory,const NativeOwnerImage& image,NativePlayerRoster& out,std::string& why,const NativeOwnerLimits& limits)noexcept {
    NativeOwnerCache cache;return cache.players(memory,image,out,why,limits);
}
bool capture_native_player_owner(const NativeOwnerMemory& memory,const NativeOwnerImage& image,const NativePlayerIdentity& player,NativeOwnerCandidate& out,std::string& why,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats)noexcept {
    NativeOwnerCache cache;return cache.capture_player(memory,image,player,out,why,limits,stats);
}
}

namespace evrm {
bool revalidate_native_player_owners(const NativeOwnerMemory& memory,std::vector<NativeOwnerCandidate>& candidates,std::string& why,const NativeOwnerLimits& requested,NativeOwnerObservationStats* stats)noexcept {
    const U started=memory.work_milliseconds();why.clear();if(stats){*stats={};stats->phase=Phase::submission_verify;stats->failure_location=Location::validation;}
    try{
        need(!candidates.empty()&&candidates.size()<=16,"player owner batch size invalid");
        const auto& first=candidates.front();need(!first.guards.empty(),"player owner batch has no guards");
        std::set<U> accounts,entities;std::set<std::uint32_t> indices;
        for(const auto& c:candidates){
            need(&c.guards.view()==&first.guards.view()&&same_owner_image(c.image,first.image)&&c.player_infos==first.player_infos&&
                c.model_manager==first.model_manager&&c.native_buffer==first.native_buffer,"heterogeneous player owner batch");
            player_candidate_metadata(c);need(accounts.insert(c.account).second&&entities.insert(c.entity).second,"duplicate player owner batch subject");
            for(const auto& model:c.models)need(indices.insert(model.index).second,"overlapping player owner batch model");
        }
        auto limits=requested;limits.max_milliseconds=std::min<U>(limits.max_milliseconds,40);const U now=memory.work_milliseconds();
        need(now>=started&&now-started<limits.max_milliseconds,"player owner batch deadline exceeded");limits.max_milliseconds-=now-started;
        Reader m(memory,limits,stats);m.phase(Phase::submission_verify);m.verify(first.guards.view());
        const U finished=memory.work_milliseconds();if(stats){stats->elapsed_milliseconds=finished>=started?finished-started:0;stats->clock_regressed=stats->clock_regressed||finished<started;}
        need(finished>=started&&finished-started<std::min<U>(requested.max_milliseconds,40),"player owner batch deadline exceeded");return true;
    }catch(const Rejected& error){reason_set(why,error.reason);}catch(...){if(stats)stats->failure_location=Location::internal;reason_set(why,"player owner batch revalidation unavailable");}
    candidates.clear();return false;
}
}
