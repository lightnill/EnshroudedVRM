#include "native_player_identity_fixture.hpp"
#include "../src/native_player_identity.hpp"
#include <string>
struct PlayersFixture:Fixture {
    Bytes infos=Bytes(16*160),player_storage=Bytes(16*1600);
    explicit PlayersFixture(unsigned count=2){
        add_identity_pins(*this);memory.owned.push_back(&infos);memory.owned.push_back(&player_storage);
        services.resize(3*232);service_keys.resize(4*4);service_values.resize(4*2);service_bits.assign(8,0);
        reg(p::serviceLayout::recordsPointerOffset,address(services));reg(p::serviceLayout::recordCountOffset,U(3));
        reg(p::serviceLayout::mapKeysOffset,address(service_keys));reg(p::serviceLayout::mapValuesOffset,address(service_values));
        reg(p::serviceLayout::mapCapacityOffset,U(4));reg(p::serviceLayout::mapCountOffset,std::uint32_t(3));
        const char* names[]={"RenderScene","LocalPlayerData","BasicPlayerInfos"};const U pointers[]={address(scene),address(local_player),address(infos)};
        for(unsigned i=0;i<3;++i){U slot=hash(names[i])&3;while((service_bits[slot/8]>>(slot%8))&1)slot=(slot+1)&3;
            service_bits[slot/8]|=1u<<(slot%8);put(service_keys,slot*4,hash(names[i]));put<std::uint16_t>(service_values,slot*2,static_cast<std::uint16_t>(i));
            put(services,i*232,text(names[i]));put<U>(services,i*232+8,std::strlen(names[i]));put(services,i*232+160,hash(names[i]));put(services,i*232+168,pointers[i]);put(services,i*232+176,pointers[i]);}
        for(U system:{game_26430625_fixture::systems,game_26430625_fixture::systems+2*game_26430625_fixture::system_stride})
            for(U binding:{game_26430625_fixture::normal_bindings,game_26430625_fixture::alternate_bindings})reg(system+binding,address(infos));
        player[0]=7;put<std::uint16_t>(player,p::ecsReadLayout::componentOffsets+4,0);put<std::uint16_t>(player,p::ecsReadLayout::componentStrides+4,96);
        for(unsigned i=0;i<count;++i){
            put<std::uint32_t>(infos,i*160,i+1);put<std::uint32_t>(infos,i*160+4,64+i);put<U>(infos,i*160+8,0xf00d0000ULL+i+1);
            put(entities,i*64+24,address(player));put(entities,i*64+32,address(player_storage)+i*1600);
            put<std::uint32_t>(player_storage,i*1600,0x01000000+i+1);generations[i+1]=1;
        }
    }
};
int main(int argc,char** argv){try{
    const std::string mode=argc>1?argv[1]:"two";PlayersFixture f(mode.find("sixteen")!=std::string::npos?16:mode=="batch-one"?1:2);
    evrm::NativeOwnerLimits limits;limits.max_milliseconds=40;evrm::NativePlayerRoster roster;std::string why;
    evrm::NativeOwnerCache cache;need(cache.players(f.memory,f.identity,roster,why,limits),why.empty()?"roster capture rejected":why.c_str());
    need(roster.players.size()==(mode.find("sixteen")!=std::string::npos?16:mode=="batch-one"?1:2)&&roster.observed_milliseconds==100,"wrong roster");
    need(roster.players[0].local&&roster.players[0].entity_id==1,"wrong local association");
    if(roster.players.size()>1)need(!roster.players[1].local&&roster.players[1].connection==65,"wrong remote slot association");
    if(mode.compare(0,6,"batch-")==0){
        auto targets=roster.players;std::vector<evrm::NativeOwnerCandidate> owners;evrm::NativeOwnerObservationStats batch_stats;
        bool expected=true;
        if(mode=="batch-subset")targets.erase(targets.begin());
        if(mode=="batch-reverse")std::reverse(targets.begin(),targets.end());
        if(mode=="batch-empty"){targets.clear();expected=false;}
        if(mode=="batch-overbound"){targets.assign(17,targets[0]);expected=false;}
        if(mode=="batch-reuse"){put<U>(f.infos,168,targets[1].account+100);expected=false;}
        if(mode=="batch-stale"){++targets[1].account;expected=false;}
        if(mode=="batch-duplicate"){targets[1]=targets[0];expected=false;}
        f.memory.reset();f.memory.batched=true;
        if(mode=="batch-deadline"){f.memory.advance_after=3;f.memory.advanced=140;expected=false;}
        const auto start=std::chrono::steady_clock::now();
        need(cache.capture_players(f.memory,f.identity,targets,owners,why,limits,&batch_stats)==expected,"batch result mismatch");
        const double elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
        if(expected){need(owners.size()==targets.size(),"batch lost subject");for(std::size_t i=0;i<owners.size();++i)need(owners[i].account==targets[i].account&&owners[i].models[0].index==targets[i].entity_id,"batch misdirected subject");
            need(f.memory.calls<5000&&f.memory.prefix_reads==2,"batch repeated registry proof per subject");
            std::printf("batch players=%zu calls=%zu bytes=%zu elapsed_ms=%.3f\n",owners.size(),f.memory.calls,f.memory.bytes,elapsed);
        }else need(owners.empty(),"failed batch retained partial owners");
        need(batch_stats.attempted_calls==f.memory.calls,"batch count mismatch");
        if(mode.compare(0,17,"batch-revalidate-")==0){
            bool valid=mode=="batch-revalidate-valid"||mode=="batch-revalidate-sixteen";
            if(mode=="batch-revalidate-account-memory")f.infos[168]^=1;
            if(mode=="batch-revalidate-model-memory")f.player_storage[1600]^=1;
            if(mode=="batch-revalidate-model-metadata")owners[1].models[0].record+=432;
            if(mode=="batch-revalidate-record-metadata")owners[1].player_record-=160;
            if(mode=="batch-revalidate-entity-metadata")owners[1].entity=owners[0].entity;
            if(mode=="batch-revalidate-duplicate")owners[1]=owners[0];
            if(mode=="batch-revalidate-heterogeneous")owners[1].guards=std::vector<evrm::NativeOwnerGuard>(owners[1].guards.begin(),owners[1].guards.end());
            if(mode=="batch-revalidate-empty")owners.clear();
            // Exact reads prove one shared guard pass independently of where
            // owned vectors happen to straddle optional read-ahead pages.
            const auto shared_guards=owners.empty()?0:owners.front().guards.size();
            f.memory.reset();f.memory.batched=false;
            if(mode=="batch-revalidate-deadline"){f.memory.advance_after=3;f.memory.advanced=140;}
            const auto verify_started=std::chrono::steady_clock::now();
            need(evrm::revalidate_native_player_owners(f.memory,owners,why,limits,&batch_stats)==valid,"bulk revalidation result mismatch");
            const double verify_ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-verify_started).count();
            need(batch_stats.attempted_calls==f.memory.calls,"bulk verification statistics disagree");
            if(valid){need(f.memory.prefix_reads==1&&f.memory.calls==shared_guards,"bulk guards reread for each player");
                std::printf("bulk subjects=%zu calls=%zu bytes=%zu elapsed_ms=%.3f\n",owners.size(),f.memory.calls,f.memory.bytes,verify_ms);}
            else need(owners.empty(),"failed bulk verification retained subjects");
        }
        std::printf("PASS %s\n",mode.c_str());return 0;
    }
    auto target=roster.players[1];evrm::NativeOwnerCandidate a,b;evrm::NativeOwnerObservationStats stats;
    need(cache.capture_player(f.memory,f.identity,roster.players[0],a,why,limits,&stats),why.c_str());
    need(cache.capture_player(f.memory,f.identity,target,b,why,limits,&stats),why.c_str());
    need(a.entity!=b.entity&&a.models[0].index==1&&b.models[0].index==2&&a.native_buffer==b.native_buffer,"target owner equipment/allocation join failed");
    need(b.account==target.account&&b.connection==target.connection&&b.player_record==address(f.infos)+160,"candidate lost identity");
    if(mode=="alias-image"){
        need(evrm::capture_native_player_owner(f.memory,b.image,target,b,why,limits,&stats),"aliased output image was cleared before capture");
        need(b.account==target.account,"aliased capture lost target");std::puts("PASS alias-image");return 0;
    }
    if(mode.compare(0,10,"candidate-")==0){
        if(mode=="candidate-account")++b.account;else if(mode=="candidate-connection")++b.connection;
        else if(mode=="candidate-record")b.player_record-=160;else if(mode=="candidate-clear")b.account=0;
        need(!evrm::revalidate_native_owner(f.memory,b,why,limits)&&b.guards.empty(),"candidate association metadata escaped guard binding");
        std::printf("PASS %s\n",mode.c_str());return 0;
    }
    if(mode=="two"||mode=="sixteen"){
        f.memory.reset();f.memory.batched=true;
        for(const auto& player:roster.players)need(cache.capture_player(f.memory,f.identity,player,b,why,limits,&stats),why.c_str());
        std::printf("players=%zu calls=%zu bytes=%zu prefix_reads=%zu work_ms=%llu\n",roster.players.size(),f.memory.calls,f.memory.bytes,f.memory.prefix_reads,(unsigned long long)stats.elapsed_milliseconds);
        need(f.memory.prefix_reads==roster.players.size()*2,"target capture rebuilt registry prefix");
    }else if(mode=="wrong-account")++target.account;
    else if(mode=="wrong-connection")++target.connection;
    else if(mode=="wrong-local")target.local=true;
    else if(mode=="wrong-slot")put<std::uint32_t>(f.infos,160,3);
    else if(mode=="duplicate")put<U>(f.infos,168,roster.players[0].account);
    else if(mode=="connection-slot")put<std::uint32_t>(f.infos,164,66);
    else if(mode=="reuse")put<U>(f.infos,168,target.account+100);
    else if(mode=="overlap")put<std::uint32_t>(f.player_storage,1600,0x01000001);
    else if(mode=="inactive")put<std::uint32_t>(f.infos,164,0);
    else if(mode=="missing-selector")put<std::uint32_t>(f.local_player,4,0);
    else if(mode=="missing-state")f.player[0]=3;
    else if(mode=="unsupported")f.identity.sha256[0]^=1;
    else if(mode=="producer")f.image[0x3aeb99]^=1;
    else if(mode=="service-binding")f.reg(game_26430625_fixture::systems+game_26430625_fixture::normal_bindings,address(f.local_player));
    else if(mode=="guard-account"||mode=="guard-connection"||mode=="guard-generation"||mode=="guard-handle"){
        if(mode=="guard-account")f.infos[168]^=1;else if(mode=="guard-connection")f.infos[164]^=64;else if(mode=="guard-generation")f.entities[84]^=1;else f.player_storage[1600]^=1;
        need(!evrm::revalidate_native_owner(f.memory,b,why,limits)&&b.guards.empty(),"mutable association escaped submission guard");
        std::printf("PASS %s\n",mode.c_str());return 0;
    }else if(mode=="deadline"||mode=="regressed"){
        f.memory.reset();f.memory.advance_after=3;f.memory.advanced=mode=="deadline"?140:99;
    }else if(mode!="two"&&mode!="sixteen")need(false,"unknown scenario");
    if(mode!="two"&&mode!="sixteen"){
        need(!cache.capture_player(f.memory,f.identity,target,b,why,limits,&stats)&&b.models.empty()&&b.guards.empty(),"unsafe targeted capture accepted");
        if(mode=="overlap"||mode=="missing-state")need(evrm::capture_native_players(f.memory,f.identity,roster,why,limits)&&roster.players.size()==2,"identity-only roster traversed render components");
        else if(mode!="wrong-account"&&mode!="wrong-connection"&&mode!="wrong-local"&&mode!="reuse"&&mode!="deadline"&&mode!="regressed")need(!evrm::capture_native_players(f.memory,f.identity,roster,why,limits)&&roster.players.empty(),"unsafe roster accepted");
    }
    std::printf("PASS %s\n",mode.c_str());return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
