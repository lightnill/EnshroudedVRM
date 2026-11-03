#include "../src/native_surface_material.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>
#include <cstdio>
#include <cstdlib>
#include <exception>
#ifdef _WIN32
#include <windows.h>
#endif
using namespace evrm;
void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
void close(float got,float want,const char* why){need(std::abs(got-want)<1e-6f,why);}
int main(){
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    std::set_terminate([]()noexcept{std::fputs("FAIL unexpected surface policy termination\n",stderr);std::fflush(stderr);std::_Exit(3);});
    try{
    vrm::Material ordinary;need(native_surface_features(ordinary)==0,"plain material gained effects");
    auto normal=std::make_shared<vrm::MaterialSurface>();normal->normal.texture=3;
    vrm::Material n;n.surface=normal;
    need(native_surface_features(n)==surface_normal,"normal map missing from Auto");
    normal->normal_scale=0;need(native_surface_features(n)==0,"neutral normal scale paid for sampling");normal->normal_scale=1;
    auto cap=std::make_shared<vrm::MaterialSurface>();cap->kind=vrm::SurfaceKind::MToon0;
    cap->matcap.texture=4;cap->rim.texture=5;vrm::Material c;c.surface=cap;
    need(native_surface_features(c)==surface_matcap,"legacy mask incorrectly masks standalone matcap");
    cap->kind=vrm::SurfaceKind::MToon1;
    need(native_surface_features(c)==(surface_matcap|surface_mask),"new combined matcap mask missing");
    auto rim=std::make_shared<vrm::MaterialSurface>();rim->kind=vrm::SurfaceKind::MToon1;rim->rim_color={.2f,.3f,.4f};
    vrm::Material r;r.surface=rim;need(native_surface_features(r)==surface_rim,"factor-only rim missed");
    auto unused=std::make_shared<vrm::MaterialSurface>();unused->kind=vrm::SurfaceKind::MToon1;unused->rim.texture=4;
    vrm::Material u;u.surface=unused;need(native_surface_features(u)==0,"unused mask enabled enhanced shading");
    vrm::Avatar a;a.materials={n,c,r};AvatarAppearance appearance;
    auto selection=make_native_surface_selection(a,&appearance);
    need(selection.features==std::vector<std::uint32_t>({1,10,4,0}),"Auto feature mapping/fallback wrong");
    need(selection.any()&&selection.needs_vertices()&&selection.needs_tangents(),"requested optional resources lost");
    need(native_surface_valid_selection(a,selection),"real feature selection rejected");
    const auto desired=selection;
    AvatarAppearance fast;fast.whole=AvatarShaderPreset::Fast;
    const auto pending=make_native_surface_selection(a,&fast);
    need(!native_surface_active_selection(a,desired,pending).any(),"pending enhanced upload disabled Basic rendering");
    need(native_surface_active_selection(a,desired,desired)==desired,"ready enhancement lost selected effects");
    need(!native_surface_active_selection(a,pending,desired).any(),"Fast waited for resource retirement before disabling effects");
    auto partial=desired;partial.features[1]=0;partial.combined=5;
    need(native_surface_active_selection(a,desired,partial)==partial,"partial ready feature intersection wrong");
    partial.features.pop_back();
    need(!native_surface_active_selection(a,desired,partial).any(),"malformed ready selection entered Enhanced shader");
    appearance.materials={AvatarShaderPreset::Matte,AvatarShaderPreset::Fast,AvatarShaderPreset::Enhanced};
    selection=make_native_surface_selection(a,&appearance);
    need(selection.features==std::vector<std::uint32_t>({0,0,4,0}),"material overrides ignored");
    need(selection.any()&&!selection.needs_vertices()&&!selection.needs_tangents(),"factor-only rim allocated vertex streams");
    appearance.materials.clear();appearance.whole=AvatarShaderPreset::Fast;
    selection=make_native_surface_selection(a,&appearance);need(!selection.any(),"whole Fast retained effects");
    appearance.whole=AvatarShaderPreset::Matte;need(!make_native_surface_selection(a,&appearance).any(),"Matte retained shine maps");
    appearance.whole=AvatarShaderPreset::Enhanced;
    need(make_native_surface_selection(a,&appearance).features==std::vector<std::uint32_t>({1,10,4,0}),"explicit Enhanced missed authored effects");
    vrm::Avatar plain;plain.materials={ordinary};auto p=make_native_surface_selection(plain,&appearance);
    need(!p.any()&&!p.needs_vertices(),"forced Enhanced on plain model allocated effects");
    auto bad=p;bad.features[0]=surface_normal;bad.combined=surface_normal;
    need(!native_surface_valid_selection(plain,bad),"unavailable map selection accepted");
    bad=p;bad.features.push_back(0);need(!native_surface_valid_selection(plain,bad),"wrong feature row count accepted");
    bad=p;bad.combined=surface_rim;need(!native_surface_valid_selection(plain,bad),"incoherent feature key accepted");
    cap->matcap_factor={2,1,.5f};cap->rim_color={.2f,.4f,1};cap->rim_lighting_mix=.25f;
    cap->rim_power=3;cap->rim_lift=.2f;cap->matcap.offset={.1f,.2f};cap->matcap.scale={2,3};
    auto material=native_surface_material(c,14);
    close(material.matcap[0],2,"matcap factor lost");close(material.params[2],.25f,"mix lost");
    close(material.cap_uv0[2],.1f,"matcap UV offset lost");need(material.flags[0]==14&&material.flags[1]==2,"GPU kind/feature packing wrong");
    auto bounds=native_surface_bounds(c,14);
    close(bounds.lit[0],.55f,"VRM1 combined lit bound wrong");close(bounds.unlit[2],1.125f,"VRM1 combined unlit bound wrong");
    cap->kind=vrm::SurfaceKind::MToon0;bounds=native_surface_bounds(c,14);
    close(bounds.lit[0],.05f,"legacy matcap incorrectly lit");close(bounds.unlit[0],2.15f,"legacy unlit matcap or rim lost");
    close(bounds.unlit[2],1.25f,"legacy mask/mix bound wrong");
    std::cout<<"PASS Auto, effective map selection, overrides, optional storage, GPU constants and generation-specific bounds\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
