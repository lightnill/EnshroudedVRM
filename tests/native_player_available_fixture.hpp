#pragma once
#include "native_player_identity_fixture.hpp"
namespace {
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
            service_bits[slot/8]|=1u<<(slot%8);put(service_keys,slot*4,hash(names[i]));put<std::uint16_t>(service_values,slot*2,i);
            put(services,i*232,text(names[i]));put<U>(services,i*232+8,std::strlen(names[i]));put(services,i*232+160,hash(names[i]));put(services,i*232+168,pointers[i]);put(services,i*232+176,pointers[i]);}
        for(U system:{game_26404966_fixture::systems,game_26404966_fixture::systems+2*game_26404966_fixture::system_stride})
            for(U binding:{game_26404966_fixture::normal_bindings,game_26404966_fixture::alternate_bindings})reg(system+binding,address(infos));
        player[0]=7;put<std::uint16_t>(player,p::ecsReadLayout::componentOffsets+4,0);put<std::uint16_t>(player,p::ecsReadLayout::componentStrides+4,96);
        for(unsigned i=0;i<count;++i){
            put<std::uint32_t>(infos,i*160,i+1);put<std::uint32_t>(infos,i*160+4,64+i);put<U>(infos,i*160+8,0xf00d0000ULL+i+1);
            put(entities,i*64+24,address(player));put(entities,i*64+32,address(player_storage)+i*1600);
            put<std::uint32_t>(player_storage,i*1600,0x01000000+i+1);generations[i+1]=1;
        }
    }
};

inline std::size_t actor_slot(const Fixture& f,unsigned actor){for(std::size_t i=0;i<Fixture::capacity;++i){std::uint32_t id;std::memcpy(&id,f.keys.data()+i*4,4);if(id==actor)return i;}throw std::runtime_error("actor slot missing");}
inline void absent_actor(Fixture& f,unsigned actor){auto slot=actor_slot(f,actor);f.occupancy[slot/8]&=std::uint8_t(~(1u<<(slot%8)));}

}
