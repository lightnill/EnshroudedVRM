#include "native_player_available_fixture.hpp"
#include <type_traits>
template<class T,class=void>struct AvailableTest {static void run(T&,PlayersFixture&,const std::string&){need(false,"typed available capture is missing");}};
template<class T>struct AvailableTest<T,std::void_t<decltype(&T::capture_available_players)>> {
static void run(T& cache,PlayersFixture& f,const std::string& mode){
 evrm::NativeOwnerLimits limits;limits.max_milliseconds=40;std::string why;
 evrm::NativePlayerRoster roster;need(cache.players(f.memory,f.identity,roster,why,limits),"valid identity roster rejected");auto ids=roster.players;
 // Give the remote its own valid layout so remote-only corruption cannot be
 // masked by an earlier local failure in a shared synthetic layout.
 Bytes remote_layout=f.player;f.memory.owned.push_back(&remote_layout);put(f.entities,64+24,address(remote_layout));
 if(mode=="reverse")std::reverse(ids.begin(),ids.end());
 if(mode=="subset")ids.erase(ids.begin());
 if(mode=="gone"){put<std::uint32_t>(f.infos,160,0);put<std::uint32_t>(f.infos,164,0);}
 if(mode=="recycled")put<U>(f.infos,168,0xf00d9999ULL);
 if(mode=="duplicate")ids[1]=ids[0];
 if(mode=="duplicate-entity"){ids[1].entity_id=1;ids[1].connection=128;}
 if(mode=="duplicate-connection")ids[1].connection=64;
 if(mode=="bad-slot")++ids[1].connection;
 if(mode=="two-local")ids[1].local=true;
 if(mode=="empty")ids.clear();
 if(mode=="overbound")ids.assign(17,ids[0]);
 if(mode=="overlap")put<std::uint32_t>(f.player_storage,1600,0x01000001);
 if(mode=="local-absent")absent_actor(f,1);
 if(mode=="header")put<std::uint32_t>(f.entities,80,9);
 if(mode=="local-header")put<std::uint32_t>(f.entities,16,9);
 if(mode=="local-pointer")put<U>(f.table,actor_slot(f,1)*8,1);
 if(mode=="probe-bound"){std::fill(f.occupancy.begin(),f.occupancy.end(),std::uint8_t(255));std::fill(f.keys.begin(),f.keys.end(),std::uint8_t(255));}
 if(mode=="absent-corrupt-third"){absent_actor(f,2);put<std::uint32_t>(f.entities,144,9);}
 if(mode=="pointer")put<U>(f.table,actor_slot(f,2)*8,1);
 if(mode=="capacity")put<U>(f.execution,game_26430625_fixture::entity_manager_offset+p::ecsReadLayout::entityManagerCount,3);
 if(mode=="stride")put<std::uint16_t>(remote_layout,p::ecsReadLayout::componentStrides+2,787);
 if(mode=="component")remote_layout[0]=3;
 if(mode=="missing-input")remote_layout[0]=6;
 if(mode=="missing-equipment")remote_layout[0]=5;
 if(mode=="state-stride")put<std::uint16_t>(remote_layout,p::ecsReadLayout::componentStrides+4,95);
 if(mode=="local-stride")put<std::uint16_t>(f.player,p::ecsReadLayout::componentStrides+2,787);
 if(mode=="remote-layout")put<U>(f.entities,88,1);
 if(mode=="zero-models")put<std::uint32_t>(f.player_storage,1600,0);
 if(mode=="account-conflict")put<U>(f.infos,168,ids[0].account);
 if(mode=="connection-conflict")put<std::uint32_t>(f.infos,164,64);
 if(mode=="unreadable")f.memory.unreadable=address(f.occupancy)+actor_slot(f,2)/8;
 if(mode=="read-budget")limits.max_calls=3;
 if(mode=="byte-budget")limits.max_bytes=32;
 if(mode=="deadline"||mode=="regressed"){f.memory.reset();f.memory.advance_after=3;f.memory.advanced=mode=="deadline"?140:99;}
 const bool hard=mode=="missing-input"||mode=="missing-equipment"||mode=="state-stride"||mode=="local-stride"||mode=="remote-layout"||mode=="local-header"||mode=="local-pointer"||mode=="probe-bound"||mode=="absent-corrupt-third"||mode=="duplicate-entity"||mode=="duplicate-connection"||mode=="bad-slot"||mode=="two-local"||mode=="duplicate"||mode=="empty"||mode=="overbound"||mode=="overlap"||mode=="local-absent"||mode=="header"||mode=="pointer"||mode=="capacity"||mode=="stride"||mode=="component"||mode=="zero-models"||mode=="account-conflict"||mode=="connection-conflict"||mode=="unreadable"||mode=="read-budget"||mode=="byte-budget"||mode=="deadline"||mode=="regressed";
 const bool absent=mode=="absent"||mode=="negative-guard"||mode=="strict"||mode=="spawn"||mode=="local-subset"||mode=="sixteen-absent";
 if(absent)absent_actor(f,2);
 if(mode=="local-subset"||mode=="overlap")ids.resize(1);
 auto batch=cache.capture_available_players(f.memory,f.identity,ids,why,limits);
 if(hard){need(!batch.valid&&batch.players.empty()&&batch.proof.empty(),"hard malformed/read/budget failure became availability");return;}
 need(batch.valid,why.empty()?"available batch refused":why.c_str());need(batch.players.size()==ids.size()&&!batch.proof.empty(),"batch dropped requested rows/proof");
 for(std::size_t i=0;i<ids.size();++i){const auto& row=batch.players[i];need(row.identity.account==ids[i].account&&row.identity.entity_id==ids[i].entity_id&&row.identity.connection==ids[i].connection&&row.identity.local==ids[i].local,"requested row misdirected");
  const auto status=(absent&&ids[i].entity_id==2)?decltype(row.status)::EntityAbsent:(mode=="gone"||mode=="recycled")&&ids[i].entity_id==2?decltype(row.status)::AssociationUnavailable:decltype(row.status)::Ready;
  need(row.status==status,"availability classification incorrect");
  if(status==decltype(row.status)::Ready){need(row.candidate.account==ids[i].account&&row.candidate.models.size()==1&&row.candidate.models[0].index==ids[i].entity_id,"ready ownership misdirected");need(&row.candidate.guards.view()==&batch.proof.view(),"ready row lost full shared proof");}
  else need(row.candidate.models.empty()&&row.candidate.guards.empty()&&!row.candidate.entity,"unavailable row granted candidate authority");
 }
 if(mode=="negative-guard"||mode=="spawn") {auto candidate=batch.players[0].candidate;auto slot=actor_slot(f,2);f.occupancy[slot/8]|=1u<<(slot%8);need(!evrm::revalidate_native_owner(f.memory,candidate,why,limits)&&candidate.guards.empty(),"spawn failed to revoke negative guard");if(mode=="spawn"){auto ready=cache.capture_available_players(f.memory,f.identity,ids,why,limits);need(ready.valid&&ready.players[1].status==decltype(ready.players[1].status)::Ready,"spawn did not restore ready owner");}}
 if(mode=="strict"||mode=="local-subset"){std::vector<evrm::NativeOwnerCandidate> strict;need(!cache.capture_players(f.memory,f.identity,ids,strict,why,limits)&&strict.empty(),"strict API accepted absent actor");}
}
};
int main(int argc,char** argv){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);try{std::string mode=argc>1?argv[1]:"absent";PlayersFixture f(mode.find("sixteen")!=std::string::npos?16:mode=="absent-corrupt-third"?3:2);evrm::NativeOwnerCache cache;
 if(mode=="roster"){absent_actor(f,2);evrm::NativePlayerRoster roster;std::string why;need(cache.players(f.memory,f.identity,roster,why)&&roster.players.size()==2,"unrelated absent entity erased identity roster");}
 else {AvailableTest<evrm::NativeOwnerCache>::run(cache,f,mode);}
 std::printf("PASS %s\n",mode.c_str());return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL %s\n",e.what());return 1;}}
