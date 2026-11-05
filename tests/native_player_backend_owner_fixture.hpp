#pragma once
#include "native_player_identity_fixture.hpp"
struct BackendPlayersFixture:Fixture {
    Bytes infos=Bytes(16*160),player_storage=Bytes(16*1600);
    explicit BackendPlayersFixture(unsigned count=2){
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

struct BackendOwnerSource final:evrm::NativeBackendTestOwner {
    BackendPlayersFixture& fixture;mutable evrm::NativeOwnerCache cache;
    mutable unsigned captures=0,verifies=0,menu_verifies=0;mutable std::size_t verified_subjects=0;
    explicit BackendOwnerSource(BackendPlayersFixture& f):fixture(f){}
    bool capture(evrm::NativeOwnerCandidate&,std::string&,evrm::NativeOwnerObservationStats*)const noexcept override{return false;}
    bool revalidate(evrm::NativeOwnerCandidate&,std::string&,evrm::NativeOwnerObservationStats*)const noexcept override{return false;}
    bool revalidate_menu(evrm::NativeMenuOwnerCandidate&,std::string&)const noexcept override{++menu_verifies;return false;}
    bool capture_players(const std::vector<evrm::NativePlayerIdentity>& ids,std::vector<evrm::NativeOwnerCandidate>& out,std::string& why,const evrm::NativeOwnerLimits& limits,evrm::NativeOwnerObservationStats* stats)const noexcept override {
        ++captures;return cache.capture_players(fixture.memory,fixture.identity,ids,out,why,limits,stats);
    }
    evrm::NativePlayerOwnerBatch capture_available_players(const std::vector<evrm::NativePlayerIdentity>& ids,std::string& why,const evrm::NativeOwnerLimits& limits,evrm::NativeOwnerObservationStats* stats)const noexcept override {
        ++captures;return cache.capture_available_players(fixture.memory,fixture.identity,ids,why,limits,stats);
    }
    bool revalidate_players(std::vector<evrm::NativeOwnerCandidate>& out,std::string& why,const evrm::NativeOwnerLimits& limits,evrm::NativeOwnerObservationStats* stats)const noexcept override {
        ++verifies;verified_subjects=out.size();return evrm::revalidate_native_player_owners(fixture.memory,out,why,limits,stats);
    }
};
