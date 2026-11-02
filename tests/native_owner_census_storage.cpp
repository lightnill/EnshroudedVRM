// Reuse only the owned CPU layout builder; no game, DLL or GPU access.
#define main original_census_fixture_main
#include "native_owner_census.cpp"
#undef main
#include <cstdlib>
#include <new>
static bool count_new=false;
static std::size_t new_calls=0;
void* operator new(std::size_t size){if(void* value=std::malloc(size?size:1)){if(count_new)++new_calls;return value;}throw std::bad_alloc();}
void operator delete(void* value)noexcept{std::free(value);}
void operator delete(void* value,std::size_t)noexcept{std::free(value);}
template<class T>T load(const Bytes& bytes,std::size_t at){T value;need(at<=bytes.size()&&sizeof value<=bytes.size()-at,"owned load bounds");std::memcpy(&value,bytes.data()+at,sizeof value);return value;}
struct AliasedMemory:Memory{
    U alias_header=0;std::uint32_t second_id=0;mutable unsigned hits=0;
    bool read(U location,void* destination,std::size_t size)const noexcept override{
        if(!Memory::read(location,destination,size))return false;
        if(location==alias_header&&size==36&&++hits==2)std::memcpy(destination,&second_id,sizeof second_id);
        return true;
    }
};
int main(int argc,char** argv){try{
    SetErrorMode(GetErrorMode()|0x8003);need(argc==2,"scenario required");const std::string mode=argv[1];Fixture f;
    evrm::NativeOwnerCandidate selected;std::string why;evrm::NativeOwnerLimits limits;limits.max_milliseconds=40;
    if(mode=="allocations"){
        f.memory.batched=true;evrm::NativeOwnerCache cache;
        need(cache.capture(f.memory,f.identity,selected,why,limits),why.c_str());f.change_census();f.memory.reset();
        new_calls=0;count_new=true;
        const bool captured=cache.capture(f.memory,f.identity,selected,why,limits);
        count_new=false;
        need(captured,why.c_str());
        std::printf("observed census new calls=%zu entities=6500\n",new_calls);
        need(new_calls<18000,"6500-entity rebuild still allocates per-entity uniqueness tree nodes");
        evrm::NativeOwnerCandidate fresh;need(evrm::capture_native_owner(f.memory,f.identity,fresh,why,limits),why.c_str());
        need(same_guards(selected,fresh)&&selected.entity==fresh.entity&&selected.entity_id==fresh.entity_id&&
            selected.entity_generation==fresh.entity_generation&&selected.equipment==fresh.equipment&&selected.models[0].handle==fresh.models[0].handle,
            "compact census changed complete proof bytes/order/provenance or selected identity");
        auto copied=selected;need(evrm::revalidate_native_owner(f.memory,copied,why,limits),"complete proof no longer revalidates");
    }else{
        std::vector<std::size_t> slots;for(std::size_t slot=0;slot<f.capacity;++slot)if((f.occupancy[slot/8]>>(slot%8))&1&&load<U>(f.table,slot*8)==address(f.entities))slots.push_back(slot);
        need(slots.size()==1,"selected fixture entity slot missing");
        for(std::size_t slot=0;slot<f.capacity&&slots.size()<2;++slot)if((f.occupancy[slot/8]>>(slot%8))&1&&slot!=slots.front())slots.push_back(slot);
        need(slots.size()==2,"two occupied slots required");
        const auto first=load<U>(f.table,slots[0]*8),second=load<U>(f.table,slots[1]*8);
        const auto first_id=load<std::uint32_t>(f.keys,slots[0]*4);
        AliasedMemory memory;static_cast<Memory&>(memory)=f.memory;
        if(mode=="selected-header"){
            put(f.entities,std::size_t(first-address(f.entities))+16,load<std::uint32_t>(f.keys,slots[1]*4));
        }else if(mode=="selected-alias"){
            put(f.table,slots[0]*8,second);memory.alias_header=0;memory.second_id=load<std::uint32_t>(f.keys,slots[1]*4);
            need(memory.second_id!=first_id,"pointer alias needs distinct observed IDs");
        }else if(mode=="empty")std::fill(f.occupancy.begin(),f.occupancy.end(),0);
        else need(false,"unknown scenario");
        const bool captured=evrm::capture_native_owner(memory,f.identity,selected,why,limits);
        need(!captured&&selected.guards.empty()&&selected.models.empty()&&!selected.equipment,"invalid census exposed ownership/equipment evidence");
        need(why==(mode=="empty"?"local entity outside original ID hash chain":"local entity header ID mismatch"),
            "selected-chain proof was skipped or a later subsystem was consumed first");
    }
    std::printf("PASS native_owner_census_storage %s\n",mode.c_str());return 0;
}catch(const std::exception& error){count_new=false;std::fprintf(stderr,"FAIL census storage: %s\n",error.what());return 1;}}
