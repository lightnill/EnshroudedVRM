#pragma once
// Complete synthetic layout in owned CPU memory; no game, module or GPU use.
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../src/native_owner.hpp"
#include "../src/native_owner_profile.hpp"
#include "game_26404966_fixture.hpp"
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
    Bytes image=Bytes(game_26404966_fixture::image_size),client=Bytes(218000),controller=Bytes(448),main=Bytes(480),execution=Bytes(15000000),
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
        namespace game=game_26404966_fixture;
        identity.base=address(image);identity.size=game::image_size;identity.timestamp=game::image_timestamp;
        identity.sha256=game::image_sha256;memory.dos=identity.base;
        image[0]='M';image[1]='Z';put<std::uint32_t>(image,0x3c,128);put<std::uint32_t>(image,128,0x4550);
        put<std::uint16_t>(image,132,0x8664);put<std::uint16_t>(image,134,1);put(image,136,identity.timestamp);put<std::uint16_t>(image,148,64);
        put<std::uint16_t>(image,152,0x20b);put<std::uint32_t>(image,208,std::uint32_t(identity.size));
        put<std::uint32_t>(image,224,std::uint32_t(identity.size));put<std::uint32_t>(image,228,0);put<std::uint32_t>(image,252,0x20000000);
        put(image,game::root,identity.base+game::root_vtable);
        put(image,game::root+600,address(client));
        put(client,0,identity.base+game::client_vtable);put(client,game::controller_offset,address(controller));
        put(controller,0,identity.base+game::controller_vtable);put(controller,40,address(client));put(controller,440,address(main));put(main,464,address(execution));
        registry=address(execution)+2344;entity_manager=address(execution)+game::entity_manager_offset;
        put(execution,game::entity_manager_offset,registry);
        const auto em=game::entity_manager_offset;
        put(execution,em+p::ecsReadLayout::entityManagerCount,capacity);put(execution,em+p::ecsReadLayout::entityManagerTable,address(table));
        put(execution,em+p::ecsReadLayout::entityManagerKeys,address(keys));put(execution,em+p::ecsReadLayout::entityManagerOccupancy,address(occupancy));
        std::vector<const p::ReflectedType*> selected;
        for(const char* name:{"keen::ecs::ClientPlayerInput","keen::ecs::RenderEquipment","keen::ecs::PlayerState"}){
            auto it=std::find_if(std::begin(p::types),std::end(p::types),[&](const auto& t){return std::strcmp(t.name,name)==0;});need(it!=std::end(p::types),"fixture selected type");selected.push_back(it);
        }
        for(const auto& t:p::types)if(selected.size()<registrations&&t.structure&&t.size&&t.size<=65535&&std::strcmp(t.name,selected[0]->name)&&std::strcmp(t.name,selected[1]->name)&&std::strcmp(t.name,selected[2]->name))selected.push_back(&t);
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
        constexpr U systemCountOffset=game::system_count,systemsOffset=game::systems,systemStride=game::system_stride;
        constexpr U systemNormalCountOffset=game::normal_count,systemAlternateCountOffset=game::alternate_count;
        constexpr U systemNormalBindingsOffset=game::normal_bindings,systemAlternateBindingsOffset=game::alternate_bindings;
        reg(recordsPointerOffset,address(services));reg(recordCountOffset,U(2));reg(mapBitsOffset,address(service_bits));reg(mapCapacityOffset,U(2));
        reg(mapKeysOffset,address(service_keys));reg(mapValuesOffset,address(service_values));reg(mapCountOffset,std::uint32_t(2));
        const char* service_rows[]={"RenderScene","LocalPlayerData"};const U service_pointers[]={address(scene),address(local_player)};
        for(unsigned i=0;i<2;++i){U slot=hash(service_rows[i])&1;while((service_bits[slot/8]>>(slot%8))&1)slot=(slot+1)&1;
            service_bits[slot/8]|=std::uint8_t(1u<<(slot%8));put(service_keys,slot*4,hash(service_rows[i]));put<std::uint16_t>(service_values,slot*2,std::uint16_t(i));
            const U at=i*232;put(services,at,text(service_rows[i]));put<U>(services,at+8,std::strlen(service_rows[i]));put(services,at+160,hash(service_rows[i]));put(services,at+168,service_pointers[i]);put(services,at+176,service_pointers[i]);}
        put<std::uint32_t>(local_player,4,1);
        const U descriptor=identity.base+game::equipment_descriptor,function=identity.base+game::equipment_function;
        put(image,game::equipment_descriptor,text("render_equipment_models_ag2"));put<U>(image,game::equipment_descriptor+8,std::strlen("render_equipment_models_ag2"));put(image,game::equipment_descriptor+16,function);
        const char* service_names[]={"BasicPlayerInfos","CharacterPresetRegistry","ColorGradientRegistry","MaterialFeedbackRegistry","g38_items::ItemRegistry","RenderScene"};
        for(unsigned i=0;i<6;++i){put(image,0x180000+i*16,text(service_names[i]));put<U>(image,0x180008+i*16,std::strlen(service_names[i]));}
        put(image,game::equipment_descriptor+0x98,identity.base+0x180000);put<U>(image,game::equipment_descriptor+0xa0,5);put(image,game::equipment_descriptor+0xb0,identity.base+0x180050);put<U>(image,game::equipment_descriptor+0xb8,1);
        reg(systemCountOffset,std::uint16_t(3));reg(systemsOffset,descriptor);reg(systemsOffset+0x28,function);
        reg(systemsOffset+systemNormalCountOffset,std::uint8_t(6));reg(systemsOffset+systemAlternateCountOffset,std::uint8_t(6));
        reg(systemsOffset+systemNormalBindingsOffset+40,address(scene));reg(systemsOffset+systemAlternateBindingsOffset+40,address(scene));
        const char* local_names[]={"local_player_hide","local_player_ui_render_setup"};const U local_descriptors[]={game::hide_descriptor,game::ui_descriptor},local_functions[]={game::hide_function,game::ui_function};
        const char* hide_services[]={"LocalPlayerCamera","LocalPlayerData"};const char* ui_services[]={"BasicPlayerInfos","CharacterPresetRegistry","LocalPlayerData","g38_items::ItemRegistry","G38UiPlayData"};
        for(unsigned j=0;j<2;++j){const U desc=local_descriptors[j],fn=identity.base+local_functions[j],views=0x181000+j*0x100,system=systemsOffset+(j+1)*systemStride;const unsigned n=j?5:2;
            put(image,desc,text(local_names[j]));put<U>(image,desc+8,std::strlen(local_names[j]));put(image,desc+16,fn);
            for(unsigned i=0;i<n;++i){const auto name=j?ui_services[i]:hide_services[i];put(image,views+i*16,text(name));put<U>(image,views+i*16+8,std::strlen(name));}
            put(image,desc+0x98,identity.base+views);put<U>(image,desc+0xa0,j?4:2);if(j){put(image,desc+0xb0,identity.base+views+64);put<U>(image,desc+0xb8,1);}
            reg(system,identity.base+desc);reg(system+0x28,fn);reg(system+systemNormalCountOffset,std::uint8_t(n));reg(system+systemAlternateCountOffset,std::uint8_t(n));
            for(U binding:{systemNormalBindingsOffset,systemAlternateBindingsOffset})reg(system+binding+(j?2:1)*8,address(local_player));}
        const std::uint8_t predicate[]={0x39,0x4a,0x04,0x0f,0x94,0xc0,0xc3};std::copy(std::begin(predicate),std::end(predicate),image.begin()+game::predicate);
        put(scene,0x98,address(manager));put(manager,0x1b0,address(records));put<U>(manager,0x1b8,records.size());put(manager,0x1c8,address(generations));put<U>(manager,0x1d0,64);put<std::uint32_t>(manager,0x48,64);put(manager,0x948,address(writer));generations[7]=1;
        put(writer,8,address(resource_owner));put<std::uint32_t>(writer,16,0);put<std::uint32_t>(writer,0x48,64*128);put<std::uint32_t>(writer,0xc0,64);
        put(resource_owner,8,address(device));put(resource_owner,0x268,address(resource_table));put<U>(resource_owner,0x270,1);put(resource_owner,0x280,address(dimensions));put<U>(resource_owner,0x288,1);
        put(device,0x278,address(interface));put(interface,0,identity.base+game::gpu_vtable);put(image,game::gpu_vtable+0x60,identity.base+game::buffer_constructor);
        put(resource_table,0,address(resource));put<U>(dimensions,0,64*128);put<std::uint32_t>(dimensions,8,0x13);
        put<std::uint32_t>(resource,0,0x50001);put<U>(resource,8,64*128);put<std::uint32_t>(resource,16,0x13);put<U>(resource,24,0x12340000);
    }
    // Selected identity changes refresh the bounded tail. Changes to unrelated
    // entity slots are intentionally outside the explicit local-player proof.
    void change_census(){entities[20]^=1;}
};

}

inline void add_identity_pins(Fixture& f){
 {const unsigned char bytes[]={0x48,0x8d,0x81,0x68,0x2c,0x0,0x0,0xc3};std::memcpy(f.image.data()+0x3aa890,bytes,sizeof bytes);}
 {const unsigned char bytes[]={0x8b,0x11,0x8b,0xc2,0x83,0xe0,0x3f,0x48,0x8d,0x1c,0x80,0x48,0xc1,0xe3,0x5,0x85,0xd2,0xf,0x84,0xcd,0x0,0x0,0x0,0x89,0x94,0x33,0x6c,0x2c,0x0,0x0,0xe8,0xf4,0x76,0x0,0x0,0x8b,0x94,0x33,0x6c,0x2c,0x0,0x0,0x89,0x84,0x33,0x68,0x2c,0x0,0x0,0x49,0x8b,0x86,0xd4,0xfe,0xff,0xff,0x48,0x89,0x84,0x33,0x70,0x2c,0x0,0x0,0x41,0xf,0xb6,0x6,0x88,0x84,0x33,0x4,0x2d,0x0,0x0,0x41,0x8b,0x86,0xc0,0xfe,0xff,0xff,0x89,0x84,0x33,0x78,0x2c,0x0,0x0};std::memcpy(f.image.data()+0x3aeab9,bytes,sizeof bytes);}
 {const unsigned char bytes[]={0x8b,0x1,0x85,0xc0,0x75,0x7,0x8b,0x5,0x24,0x5b,0x17,0x1,0xc3,0x83,0xe0,0x3f,0xff,0xc0,0xc3};std::memcpy(f.image.data()+0x3b61d0,bytes,sizeof bytes);}
 {const unsigned char bytes[]={0xff,0xc8,0x83,0xf8,0x10};std::memcpy(f.image.data()+0x48124a,bytes,sizeof bytes);}
 {const unsigned char bytes[]={0x48,0x8d,0x14,0x80};std::memcpy(f.image.data()+0x481254,bytes,sizeof bytes);}
 {const unsigned char bytes[]={0x48,0xc1,0xe2,0x5};std::memcpy(f.image.data()+0x48125d,bytes,sizeof bytes);}
 {const unsigned char bytes[]={0x4a,0x8b,0x54,0x3a,0x8};std::memcpy(f.image.data()+0x48126e,bytes,sizeof bytes);}
 {const unsigned char bytes[]={0x48,0x8b,0xda};std::memcpy(f.image.data()+0xecc7f5,bytes,sizeof bytes);}
 {const unsigned char bytes[]={0x48,0x89,0x5c,0x24,0x30};std::memcpy(f.image.data()+0xecc810,bytes,sizeof bytes);}
 {const unsigned char bytes[]={0x4c,0x8d,0x44,0x24,0x30};std::memcpy(f.image.data()+0xecc81a,bytes,sizeof bytes);}
 {const unsigned char bytes[]={0x4d,0x8b,0xf8};std::memcpy(f.image.data()+0xecafc7,bytes,sizeof bytes);}
 {const unsigned char bytes[]={0xc7,0x0,0xb9,0x88,0xb1,0x6};std::memcpy(f.image.data()+0xecb00c,bytes,sizeof bytes);}
 {const unsigned char bytes[]={0x4d,0x8b,0xc7};std::memcpy(f.image.data()+0xecb105,bytes,sizeof bytes);}
}
