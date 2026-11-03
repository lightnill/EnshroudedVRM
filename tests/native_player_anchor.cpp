#include <array>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <stdexcept>
#if __has_include("../src/native_prepare_player.hpp")
#include "../src/native_prepare_player.hpp"
#define HAS_REMOTE_PREPARE_CONTRACT 1
#endif
namespace shader_fixture {
struct uint3 {std::uint32_t x,y,z;uint3(std::uint32_t a,std::uint32_t b,std::uint32_t c):x(a),y(b),z(c){}};
struct float3 {float x,y,z;float3(float a,float b,float c):x(a),y(b),z(c){} explicit float3(uint3 a):x(float(a.x)),y(float(a.y)),z(float(a.z)){} float3& operator-=(float3 a){x-=a.x;y-=a.y;z-=a.z;return *this;}};
struct float4 {float x,y,z,w;};
float3 operator*(float3 a,float b){return {a.x*b,a.y*b,a.z*b};}
float3 operator/(float3 a,float b){return {a.x/b,a.y/b,a.z/b};}
float3 operator-(float3 a,float3 b){return {a.x-b.x,a.y-b.y,a.z-b.z};}
std::array<bool,3> operator!=(uint3 a,uint3 b){return {a.x!=b.x,a.y!=b.y,a.z!=b.z};}
std::array<bool,3> operator>(float3 a,float b){return {a.x>b,a.y>b,a.z>b};}
float3 abs(float3 a){return {std::abs(a.x),std::abs(a.y),std::abs(a.z)};}
float3 round(float3 a){return {std::round(a.x),std::round(a.y),std::round(a.z)};}
std::array<bool,3> isfinite(float3 a){return {std::isfinite(a.x),std::isfinite(a.y),std::isfinite(a.z)};}
std::array<bool,4> isfinite(float4 a){return {std::isfinite(a.x),std::isfinite(a.y),std::isfinite(a.z),std::isfinite(a.w)};}
template<std::size_t N>bool all(std::array<bool,N> a){for(bool v:a)if(!v)return false;return true;}
template<std::size_t N>bool any(std::array<bool,N> a){for(bool v:a)if(v)return true;return false;}
void world(uint3 root,float3 playerWorld,float mode,bool& accepted){
    struct {float4 config;} control{{1,1,0,mode}};
#include "anchor_config.inc"
#include "anchor_position.inc"
    accepted=true;
}
void menu(uint3 root,float3 playerWorld,float mode,bool& accepted){
    struct {float4 config;} control{{1,1,0,mode}};
#define EVRM_NATIVE_MENU_PREVIEW 1
#include "anchor_config.inc"
#include "anchor_position.inc"
#undef EVRM_NATIVE_MENU_PREVIEW
    (void)playerWorld;accepted=true;
}
bool qualifies(bool preview,uint3 root,float3 view,float mode){bool accepted=false;if(preview)menu(root,view,mode,accepted);else world(root,view,mode,accepted);return accepted;}
}
void need(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(){try{
    using namespace shader_fixture;
    const uint3 local(10*131072,20*131072,30*131072),remote(12*131072,20*131072,30*131072);
    const float3 local_view(10,20,30);
    need(qualifies(false,local,local_view,0),"legacy local position qualification changed");
    need(!qualifies(false,remote,local_view,0),"legacy path accepted an unrelated position");
    need(qualifies(false,remote,local_view,1),"proved remote actor still requires the local lighting position");
    need(!qualifies(false,local,local_view,2),"unknown preparation mode accepted");
    need(!qualifies(false,local,local_view,std::nanf("")),"non-finite mode accepted");
    need(qualifies(true,{0,0,0},local_view,0)&&!qualifies(true,remote,local_view,0)&&!qualifies(true,{0,0,0},local_view,1),"menu origin/legacy-only contract changed");
#ifdef HAS_REMOTE_PREPARE_CONTRACT
    using namespace evrm;NativeAvatarPublication p;p.subject="fixture-remote";p.player_identity={71,2,65,false};
    auto pose=std::make_shared<NativePoseOwner>();pose->candidate.account=71;pose->candidate.entity_id=2;pose->candidate.connection=65;p.pose_owner=pose;
    const auto owner=pose->candidate;
    need(native_prepare_remote_actor_mode(&p,owner,true,false)==1,"proved remote mode absent");
    need(native_prepare_remote_actor_mode(&p,owner,false,false)==0&&native_prepare_remote_actor_mode(&p,owner,true,true)==0,"declined/menu context enabled remote mode");
    auto wrong=owner;wrong.account=72;need(native_prepare_remote_actor_mode(&p,wrong,true,false)==0,"wrong account enabled remote mode");
    wrong=owner;wrong.connection=129;need(native_prepare_remote_actor_mode(&p,wrong,true,false)==0,"reused connection enabled remote mode");
    wrong=owner;wrong.entity_id=3;need(native_prepare_remote_actor_mode(&p,wrong,true,false)==0,"wrong actor enabled remote mode");
    p.player_identity.local=true;need(native_prepare_remote_actor_mode(&p,owner,true,false)==0,"local mode changed");p.player_identity.local=false;
    p.subject.clear();need(native_prepare_remote_actor_mode(&p,owner,true,false)==0,"anonymous legacy context enabled remote mode");p.subject="fixture-remote";
    p.pose_owner.reset();need(native_prepare_remote_actor_mode(&p,owner,true,false)==0,"missing pose proof enabled remote mode");
#else
    need(false,"explicit remote host contract missing");
#endif
    std::cout<<"PASS actual shader position predicates: local legacy, explicit remote, unknown modes and native menu; host account/actor/connection admission\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
