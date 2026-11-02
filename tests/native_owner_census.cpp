// Complete synthetic layout in owned CPU memory; no game, module or GPU use.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/native_owner.hpp"
#include "../src/native_owner_profile.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace {
using U=std::uint64_t;using Bytes=std::vector<std::uint8_t>;
namespace p=evrm::native_owner_profile;
U address(const Bytes& v){return reinterpret_cast<std::uintptr_t>(v.data());}
template<class T>void put(Bytes& v,std::size_t at,T x){if(at>v.size()||sizeof x>v.size()-at)throw std::runtime_error("fixture write bounds");std::memcpy(v.data()+at,&x,sizeof x);}
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
std::uint32_t hash(const char* text){std::uint32_t h=2166136261u;for(;*text;++text)h=(h^std::uint8_t(*text))*16777619u;return h;}
struct Memory:evrm::NativeOwnerMemory{
    std::vector<const Bytes*> owned;
    mutable std::size_t calls=0,bytes=0,prefix_reads=0;
    U dos=0,unreadable=0,mutate_on=0;std::uint8_t* mutation=nullptr;
    mutable unsigned mutation_matches=0;
    mutable U now=100;std::size_t advance_after=std::numeric_limits<std::size_t>::max();U advanced=140;
    bool rpm=false,batched=false,split_clock=false;
    bool permit_read_ahead(std::size_t,std::size_t)const noexcept{return batched;}
    bool read(U a,void* out,std::size_t n)const noexcept override{
        ++calls;bytes+=n;if(a==dos&&n>=64)++prefix_reads;
        if(a==mutate_on&&mutation&&++mutation_matches==2)*mutation^=1;
        if(calls==advance_after)now=advanced;
        if(a==unreadable)return false;
        bool valid=false;for(auto v:owned)if(a>=address(*v)&&n<=v->size()&&a-address(*v)<=v->size()-n){valid=true;break;}
        if(!valid)return false;
        if(rpm){SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(a),out,n,&got)&&got==n;}
        std::memcpy(out,reinterpret_cast<const void*>(a),n);return true;
    }
    U milliseconds()const noexcept override{return split_clock?1000000000+(now/16)*16:now;}
    U work_milliseconds()const noexcept{return now;}
    void reset(){calls=bytes=prefix_reads=0;now=100;}
};
struct Fixture{
    static constexpr U capacity=8192,population=6500,registrations=600;
    Bytes image=Bytes(p::image_size),client=Bytes(218000),controller=Bytes(448),main=Bytes(480),execution=Bytes(15000000),
        rows=Bytes(registrations*256),sizes=Bytes(registrations*2),types=Bytes(registrations*8),callbacks=Bytes(registrations*40),
        table=Bytes(capacity*8),keys=Bytes(capacity*4),occupancy=Bytes(capacity/8),entities=Bytes(population*64),
        ordinary=Bytes(5500),player=Bytes(5500),storage=Bytes(1600),services=Bytes(464),service_bits=Bytes(8),service_keys=Bytes(8),service_values=Bytes(4),local_player=Bytes(8),
        scene=Bytes(160),manager=Bytes(0x950),records=Bytes(64*432),generations=Bytes(64),writer=Bytes(208),
        resource_owner=Bytes(0x290),device=Bytes(0x280),interface=Bytes(8),resource_table=Bytes(8),dimensions=Bytes(16),resource=Bytes(32);
    Memory memory;evrm::NativeOwnerImage identity;U registry=0,entity_manager=0;std::size_t unused_slot=0,name_at=0x100000;
    U text(const char* value){const auto n=std::strlen(value);need(name_at+n<image.size(),"fixture names bound");const U result=address(image)+name_at;std::memcpy(image.data()+name_at,value,n);name_at+=n+1;return result;}
    template<class T>void reg(U offset,T value){put(execution,2344+offset,value);}
    Fixture(){
        memory.owned={&image,&client,&controller,&main,&execution,&rows,&sizes,&types,&callbacks,&table,&keys,&occupancy,&entities,
            &ordinary,&player,&storage,&services,&service_bits,&service_keys,&service_values,&local_player,&scene,&manager,&records,&generations,&writer,
            &resource_owner,&device,&interface,&resource_table,&dimensions,&resource};
        identity.base=address(image);identity.size=p::image_size;identity.timestamp=p::image_timestamp;
        std::copy(std::begin(p::image_sha256),std::end(p::image_sha256),identity.sha256.begin());memory.dos=identity.base;
        image[0]='M';image[1]='Z';put<std::uint32_t>(image,0x3c,128);put<std::uint32_t>(image,128,0x4550);
        put<std::uint16_t>(image,132,0x8664);put<std::uint16_t>(image,134,1);put(image,136,identity.timestamp);put<std::uint16_t>(image,148,64);
        put<std::uint16_t>(image,152,0x20b);put<std::uint32_t>(image,208,std::uint32_t(identity.size));
        put<std::uint32_t>(image,224,std::uint32_t(identity.size));put<std::uint32_t>(image,228,0);put<std::uint32_t>(image,252,0x20000000);
        put(image,p::ownerLayout::rootObjectRva,identity.base+p::ownerLayout::rootVtableRva);
        put(image,p::ownerLayout::rootObjectRva+p::ownerLayout::clientPointerOffset,address(client));
        put(client,0,identity.base+p::ownerLayout::clientVtableRva);put(client,p::ownerLayout::controllerPointerOffset,address(controller));
        put(controller,0,identity.base+p::ownerLayout::controllerVtableRva);put(controller,40,address(client));put(controller,440,address(main));put(main,464,address(execution));
        registry=address(execution)+2344;entity_manager=address(execution)+p::ecsReadLayout::executionEntityManagerOffset;
        put(execution,p::ecsReadLayout::executionEntityManagerOffset,registry);
        const auto em=p::ecsReadLayout::executionEntityManagerOffset;
        put(execution,em+p::ecsReadLayout::entityManagerCount,capacity);put(execution,em+p::ecsReadLayout::entityManagerTable,address(table));
        put(execution,em+p::ecsReadLayout::entityManagerKeys,address(keys));put(execution,em+p::ecsReadLayout::entityManagerOccupancy,address(occupancy));
        std::vector<const p::ReflectedType*> selected;
        for(const char* name:{"keen::ecs::ClientPlayerInput","keen::ecs::RenderEquipment"}){
            auto it=std::find_if(std::begin(p::types),std::end(p::types),[&](const auto& t){return std::strcmp(t.name,name)==0;});need(it!=std::end(p::types),"fixture selected type");selected.push_back(it);
        }
        for(const auto& t:p::types)if(selected.size()<registrations&&t.structure&&t.size&&t.size<=65535&&std::strcmp(t.name,selected[0]->name)&&std::strcmp(t.name,selected[1]->name))selected.push_back(&t);
        need(selected.size()==registrations,"fixture reflected type count");
        reg(8,address(rows));reg(16,registrations);reg(24,registrations);
        reg(232,address(sizes));reg(240,registrations);reg(256,address(types));reg(264,registrations);reg(280,address(callbacks));reg(288,registrations);
        for(U i=0;i<registrations;++i){
            const auto& t=*selected[i];const auto name=text(t.name),descriptor=identity.base+0x20000+i*0x100;
            put(image,std::size_t(descriptor-identity.base)+0x20,name);put<U>(image,std::size_t(descriptor-identity.base)+0x28,std::strlen(t.name));
            put(image,std::size_t(descriptor-identity.base)+0x40,t.size);put(image,std::size_t(descriptor-identity.base)+0x50,t.hash);
            put(rows,i*256+16,name);put<U>(rows,i*256+24,std::strlen(t.name));put(rows,i*256+32,t.hash);put(rows,i*256+40,descriptor);put(rows,i*256+56,t.size);
            put<std::uint16_t>(sizes,i*2,std::uint16_t(t.size));put(types,i*8,descriptor);
        }
        player[0]=3;put<std::uint16_t>(player,p::ecsReadLayout::componentOffsets,0);put<std::uint16_t>(player,p::ecsReadLayout::componentOffsets+2,0);
        put<std::uint16_t>(player,p::ecsReadLayout::componentStrides,1536);put<std::uint16_t>(player,p::ecsReadLayout::componentStrides+2,788);
        put<std::uint32_t>(storage,0,0x01000007);
        for(U i=0;i<population;++i){
            const auto id=std::uint32_t(i+1);auto h=id;for(int j=0;j<2;++j)h=((h>>16)^h)*0x45D9F3Bu;
            U slot=(h^(h>>16))&(capacity-1);while((occupancy[slot/8]>>(slot%8))&1)slot=(slot+1)&(capacity-1);
            occupancy[slot/8]|=std::uint8_t(1u<<(slot%8));put(table,slot*8,address(entities)+i*64);put(keys,slot*4,id);
            put(entities,i*64+16,id);put<std::uint32_t>(entities,i*64+20,1);put(entities,i*64+24,address(i?ordinary:player));put(entities,i*64+32,address(storage));
        }
        while((occupancy[unused_slot/8]>>(unused_slot%8))&1)++unused_slot;
        using namespace p::serviceLayout;
        reg(recordsPointerOffset,address(services));reg(recordCountOffset,U(2));reg(mapBitsOffset,address(service_bits));reg(mapCapacityOffset,U(2));
        reg(mapKeysOffset,address(service_keys));reg(mapValuesOffset,address(service_values));reg(mapCountOffset,std::uint32_t(2));
        const char* service_rows[]={"RenderScene","LocalPlayerData"};const U service_pointers[]={address(scene),address(local_player)};
        for(unsigned i=0;i<2;++i){U slot=hash(service_rows[i])&1;while((service_bits[slot/8]>>(slot%8))&1)slot=(slot+1)&1;
            service_bits[slot/8]|=std::uint8_t(1u<<(slot%8));put(service_keys,slot*4,hash(service_rows[i]));put<std::uint16_t>(service_values,slot*2,std::uint16_t(i));
            const U at=i*232;put(services,at,text(service_rows[i]));put<U>(services,at+8,std::strlen(service_rows[i]));put(services,at+160,hash(service_rows[i]));put(services,at+168,service_pointers[i]);put(services,at+176,service_pointers[i]);}
        put<std::uint32_t>(local_player,4,1);
        const U descriptor=identity.base+0x1ddde80,function=identity.base+0x383030;
        put(image,0x1ddde80,text("render_equipment_models_ag2"));put<U>(image,0x1ddde88,std::strlen("render_equipment_models_ag2"));put(image,0x1ddde90,function);
        const char* service_names[]={"BasicPlayerInfos","CharacterPresetRegistry","ColorGradientRegistry","MaterialFeedbackRegistry","g38_items::ItemRegistry","RenderScene"};
        for(unsigned i=0;i<6;++i){put(image,0x180000+i*16,text(service_names[i]));put<U>(image,0x180008+i*16,std::strlen(service_names[i]));}
        put(image,0x1ddde80+0x98,identity.base+0x180000);put<U>(image,0x1ddde80+0xa0,5);put(image,0x1ddde80+0xb0,identity.base+0x180050);put<U>(image,0x1ddde80+0xb8,1);
        reg(systemCountOffset,std::uint16_t(3));reg(systemsOffset,descriptor);reg(systemsOffset+0x28,function);
        reg(systemsOffset+systemNormalCountOffset,std::uint8_t(6));reg(systemsOffset+systemAlternateCountOffset,std::uint8_t(6));
        reg(systemsOffset+systemNormalBindingsOffset+40,address(scene));reg(systemsOffset+systemAlternateBindingsOffset+40,address(scene));
        const char* local_names[]={"local_player_hide","local_player_ui_render_setup"};const U local_descriptors[]={0x1ddd790,0x1ddd9e0},local_functions[]={0x382ac0,0x382c20};
        const char* hide_services[]={"LocalPlayerCamera","LocalPlayerData"};const char* ui_services[]={"BasicPlayerInfos","CharacterPresetRegistry","LocalPlayerData","g38_items::ItemRegistry","G38UiPlayData"};
        for(unsigned j=0;j<2;++j){const U desc=local_descriptors[j],fn=identity.base+local_functions[j],views=0x181000+j*0x100,system=systemsOffset+(j+1)*systemStride;const unsigned n=j?5:2;
            put(image,desc,text(local_names[j]));put<U>(image,desc+8,std::strlen(local_names[j]));put(image,desc+16,fn);
            for(unsigned i=0;i<n;++i){const auto name=j?ui_services[i]:hide_services[i];put(image,views+i*16,text(name));put<U>(image,views+i*16+8,std::strlen(name));}
            put(image,desc+0x98,identity.base+views);put<U>(image,desc+0xa0,j?4:2);if(j){put(image,desc+0xb0,identity.base+views+64);put<U>(image,desc+0xb8,1);}
            reg(system,identity.base+desc);reg(system+0x28,fn);reg(system+systemNormalCountOffset,std::uint8_t(n));reg(system+systemAlternateCountOffset,std::uint8_t(n));
            for(U binding:{systemNormalBindingsOffset,systemAlternateBindingsOffset})reg(system+binding+(j?2:1)*8,address(local_player));}
        const std::uint8_t predicate[]={0x39,0x4a,0x04,0x0f,0x94,0xc0,0xc3};std::copy(std::begin(predicate),std::end(predicate),image.begin()+0x3b4fe0);
        put(scene,0x98,address(manager));put(manager,0x1b0,address(records));put<U>(manager,0x1b8,records.size());put(manager,0x1c8,address(generations));put<U>(manager,0x1d0,64);put<std::uint32_t>(manager,0x48,64);put(manager,0x948,address(writer));generations[7]=1;
        put(writer,8,address(resource_owner));put<std::uint32_t>(writer,16,0);put<std::uint32_t>(writer,0x48,64*128);put<std::uint32_t>(writer,0xc0,64);
        put(resource_owner,8,address(device));put(resource_owner,0x268,address(resource_table));put<U>(resource_owner,0x270,1);put(resource_owner,0x280,address(dimensions));put<U>(resource_owner,0x288,1);
        put(device,0x278,address(interface));put(interface,0,identity.base+0x1da69e0);put(image,0x1da69e0+0x60,identity.base+0xfba100);
        put(resource_table,0,address(resource));put<U>(dimensions,0,64*128);put<std::uint32_t>(dimensions,8,0x13);
        put<std::uint32_t>(resource,0,0x50001);put<U>(resource,8,64*128);put<std::uint32_t>(resource,16,0x13);put<U>(resource,24,0x12340000);
    }
    // Selected identity changes refresh the bounded tail. Changes to unrelated
    // entity slots are intentionally outside the explicit local-player proof.
    void change_census(){entities[20]^=1;}
};
bool same_guards(const evrm::NativeOwnerCandidate& a,const evrm::NativeOwnerCandidate& b){
    if(a.guards.size()!=b.guards.size())return false;
    for(std::size_t i=0;i<a.guards.size();++i){const auto& x=a.guards[i];const auto& y=b.guards[i];if(x.address!=y.address||x.bytes!=y.bytes||x.capture_phase!=y.capture_phase)return false;}
    return true;
}
}
int main(int argc,char** argv){try{
    SetErrorMode(GetErrorMode()|0x8003);const std::string mode=argc>1?argv[1]:"reuse";Fixture f;
    evrm::NativeOwnerCache cache;evrm::NativeOwnerCandidate c;std::string why;evrm::NativeOwnerObservationStats stats;evrm::NativeOwnerLimits limits;limits.max_milliseconds=40;bool reused=false;
    const bool cold=cache.capture(f.memory,f.identity,c,why,limits,&stats,&reused);need(cold,why.c_str());need(c.entity_id==1&&c.models.size()==1&&c.models[0].index==7&&!reused,"bad cold capture");
    need(f.memory.prefix_reads==2,"fresh capture did not fully read and verify image");
    const auto old=c;f.memory.reset();f.change_census();
    f.memory.batched=mode.find("batched")!=std::string::npos;
    const auto census=std::find_if(c.guards.begin(),c.guards.end(),[](const auto& g){return g.capture_phase==evrm::NativeOwnerObservationPhase::census;});
    const std::size_t prefix=std::size_t(census-c.guards.begin());need(prefix>2000,"fixture lacks full registry prefix");
    if(mode=="adapter"){
        auto other=f.memory;const bool accepted=cache.capture(other,f.identity,c,why,limits,&stats,&reused);need(accepted,why.c_str());
        need(!reused&&other.prefix_reads==2&&!f.memory.calls,"different adapter reused decoded prefix");
        evrm::NativeOwnerCandidate fresh;const bool full=evrm::capture_native_owner(other,f.identity,fresh,why);need(full&&same_guards(c,fresh),"new adapter capture differs from full current proof");
        std::printf("PASS native_owner_census %s\n",mode.c_str());return 0;
    }
    auto requested=f.identity;
    bool expected=true;
    if(mode=="split-clock"||mode=="split-deadline"){
        f.memory.split_clock=true;f.memory.now=1007;f.memory.advance_after=1;f.memory.advanced=mode=="split-clock"?1041:1049;
        expected=mode=="split-clock";
    }
    else if(mode=="unchanged")f.change_census();
    else if(mode=="cleared")cache.clear();
    else if(mode=="image"){requested.timestamp^=1;expected=false;}
    else if(mode=="consumer"){c.entity_id=99;c.models[0].handle=0;}
    else if(mode=="prefix-change")f.image[6]^=1;
    else if(mode=="equipment"){put<std::uint32_t>(f.storage,0,0x02000007);f.generations[7]=2;f.change_census();}
    else if(mode=="ambiguous")f.ordinary[0]=1;
    else if(mode=="prefix-race"){f.memory.mutate_on=f.entity_manager+p::ecsReadLayout::entityManagerCount;f.memory.mutation=f.image.data()+6;expected=false;}
    else if(mode=="read-fault"){f.memory.unreadable=address(f.sizes);expected=false;}
    else if(mode=="calls"){limits.max_calls=prefix+12;expected=false;}
    else if(mode=="batched-budget"){limits.max_calls=prefix+4;expected=false;}
    else if(mode=="bytes"){std::size_t n=0;for(std::size_t i=0;i<prefix;++i)n+=old.guards[i].bytes.size();limits.max_bytes=n+32;expected=false;}
    else if(mode=="deadline"||mode=="clock-regressed"){f.memory.advance_after=prefix+8;f.memory.advanced=mode=="deadline"?140:99;expected=false;}
    else if(mode=="identity")put<std::uint32_t>(f.entities,20,2);
    else need(mode=="reuse"||mode=="batched"||mode.compare(0,9,"benchmark")==0,"unknown scenario");
    const auto result=cache.capture(f.memory,requested,c,why,limits,&stats,&reused);
    need(result==expected,"capture acceptance changed");
    need(stats.attempted_calls==f.memory.calls&&stats.attempted_bytes==f.memory.bytes,"aggregate progress mismatch");
    if(mode=="split-clock")need(stats.elapsed_milliseconds==34&&c.elapsed_milliseconds<40,"aggregate cache mixed presentation and work clocks");
    if(mode=="split-deadline")need(!result&&stats.elapsed_milliseconds==42,"aggregate cache relaxed actual work deadline");
    if(!expected){need(c.guards.empty()&&c.models.empty()&&!reused,"failed cache exposed a candidate");need(f.memory.calls<=limits.max_calls&&f.memory.bytes<=limits.max_bytes,"cache reset aggregate read budget");
        if(mode=="prefix-race")need(stats.guard_ordinal==0&&stats.guard_capture_phase==evrm::NativeOwnerObservationPhase::image&&stats.failure_location==evrm::NativeOwnerFailureLocation::compare,"changed prefix escaped final full verification");
        if(mode=="calls"||mode=="bytes"||mode=="deadline"||mode=="clock-regressed")need(f.memory.prefix_reads==1,"resumed failure restarted its read/deadline budget");
    }
    else {
        if(mode=="reuse"||mode=="identity"||mode=="benchmark")need(f.memory.prefix_reads==2,"census refresh rebuilt the already verified registry prefix");
        if(mode=="unchanged")need(reused&&f.memory.prefix_reads==1,"unchanged cache did not verify once");
        if(mode=="cleared")need(!reused&&f.memory.prefix_reads==2,"cold cache reused a prefix");
        if(mode=="prefix-change"||mode=="equipment")need(!reused&&f.memory.prefix_reads==3,"non-census change bypassed full capture");
        evrm::NativeOwnerCandidate fresh;const bool captured=evrm::capture_native_owner(f.memory,f.identity,fresh,why);need(captured,why.c_str());
        need(same_guards(c,fresh),"resumed guard bytes/order/provenance differ from a full fresh capture");
        need(c.entity==fresh.entity&&c.entity_id==fresh.entity_id&&c.entity_generation==fresh.entity_generation&&c.equipment==fresh.equipment&&c.native_buffer==fresh.native_buffer&&c.models[0].handle==fresh.models[0].handle,"resumed identity differs from full fresh census");
        auto copy=c;need(evrm::revalidate_native_owner(f.memory,copy,why,limits),"fresh result failed complete validation");
    }
    if(mode.compare(0,9,"benchmark")==0){
        const bool cold_run=mode.find("cold")!=std::string::npos,warm_run=mode.find("warm")!=std::string::npos;
        f.memory.rpm=true;double sum=0,worst=0;std::size_t reads=0,bytes=0;std::vector<double> times;times.reserve(25);
        for(unsigned run=0;run<30;++run){if(cold_run)cache.clear();else if(!warm_run)f.change_census();f.memory.reset();const auto started=std::chrono::steady_clock::now();need(cache.capture(f.memory,f.identity,c,why,limits,&stats),why.c_str());const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();if(run>=5){times.push_back(ms);sum+=ms;worst=std::max(worst,ms);reads+=f.memory.calls;bytes+=f.memory.bytes;}}
        std::sort(times.begin(),times.end());std::printf("benchmark kind=%s batched=%u mean-ms=%.6f p50-ms=%.6f p95-ms=%.6f worst-ms=%.6f reads=%zu bytes=%zu registry=600 entities=6500\n",cold_run?"cold":warm_run?"warm":"selected-refresh",f.memory.batched?1:0,sum/25,times[12],times[23],worst,reads/25,bytes/25);
    }
    std::printf("PASS native_owner_census %s\n",mode.c_str());return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL native_owner_census: %s\n",e.what());return 1;}}
