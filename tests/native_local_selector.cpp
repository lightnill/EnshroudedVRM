// Tests real owner discovery using the complete owned CPU metadata fixture.
#define main original_census_fixture_main
#include "native_owner_census.cpp"
#undef main
int main(int argc,char** argv){try{
    SetErrorMode(GetErrorMode()|0x8003);need(argc==2,"selector scenario required");const std::string mode=argv[1];Fixture f;
    evrm::NativeOwnerCandidate c;std::string why;evrm::NativeOwnerLimits limits;limits.max_milliseconds=40;
    if(mode=="zero"){
        put<std::uint32_t>(f.local_player,4,0);
        need(!evrm::capture_native_owner(f.memory,f.identity,c,why,limits),"absent game local selector authorized an input entity");
        need(c.guards.empty()&&c.models.empty(),"absent selector retained a proposal");
    }else if(mode=="selected"){
        put<std::uint32_t>(f.local_player,4,2);put(f.entities,64+24,address(f.player));
        need(evrm::capture_native_owner(f.memory,f.identity,c,why,limits),"explicit game selection was replaced by input-marker ambiguity");
        need(c.entity_id==2&&c.entity==address(f.entities)+64&&c.entity_generation==1&&c.models.size()==1,"did not select the game's current local entity");
        auto copy=c;need(evrm::revalidate_native_owner(f.memory,copy,why,limits),"selected entity did not revalidate");
    }else if(mode=="changed"||mode=="stale-selector"||mode=="generation"){
        evrm::NativeOwnerCache cache;bool reused=false;need(cache.capture(f.memory,f.identity,c,why,limits),why.c_str());auto old=c;
        if(mode=="generation")put<std::uint32_t>(f.entities,20,2);
        else {put<std::uint32_t>(f.local_player,4,2);put(f.entities,64+24,address(f.player));}
        need(!evrm::revalidate_native_owner(f.memory,old,why,limits)&&old.guards.empty(),"stale actor proposal survived its selector/generation change");
        need(cache.capture(f.memory,f.identity,c,why,limits,nullptr,&reused)&&!reused,"changed selector did not refresh its actor proof");
        need(c.entity_id==(mode=="generation"?1u:2u)&&c.entity_generation==(mode=="generation"?2u:1u),"changed actor identity was not adopted");
    }else if(mode=="unrelated"){
        f.memory.unreadable=address(f.entities)+64+16;f.ordinary[0]=1;
        need(evrm::capture_native_owner(f.memory,f.identity,c,why,limits)&&c.entity_id==1,"unrelated entity changed explicit local selection");
    }else{
        if(mode=="missing-input")put<std::uint32_t>(f.local_player,4,2);
        else if(mode=="missing-equipment")f.player[0]=1;
        else if(mode=="header")put<std::uint32_t>(f.entities,16,99);
        else if(mode=="binding")f.reg(p::serviceLayout::systemsOffset+p::serviceLayout::systemStride+p::serviceLayout::systemNormalBindingsOffset+8,address(f.scene));
        else if(mode=="predicate")f.image[0x3bb660]^=1;
        else if(mode=="service-alias")put(f.services,232+176,address(f.scene));
        else if(mode=="empty")std::fill(f.occupancy.begin(),f.occupancy.end(),0);
        else if(mode=="missing-key")put<std::uint32_t>(f.local_player,4,100000);
        else if(mode=="racing"){f.memory.mutate_on=address(f.local_player)+4;f.memory.mutation=f.local_player.data()+4;}
        else need(false,"unknown selector scenario");
        need(!evrm::capture_native_owner(f.memory,f.identity,c,why,limits),"invalid selector/binding/selected entity authorized replacement");
        need(c.guards.empty()&&c.models.empty()&&!c.equipment,"invalid selector retained render evidence");
    }
    std::printf("PASS native_local_selector %s\n",mode.c_str());return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL native_local_selector: %s\n",e.what());return 1;}}
