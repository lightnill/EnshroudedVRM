// CPU-only bridge: actual importer and selection/constants feed the GPU packet.
// No Vulkan include, game memory, DLL loading, network or GPU calls.
#include "../src/native_avatar_material.hpp"
#include "../src/native_surface_material.hpp"
#include "../vendor/tinygltf/json.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <exception>
#ifdef _WIN32
#include <windows.h>
#endif
using Json=nlohmann::json;
namespace fs=std::filesystem;
static void require(bool ok,const char* text){if(!ok)throw std::runtime_error(text);}
template<class T>static void write(const fs::path& path,const T* data,std::size_t count){
    std::ofstream out(path,std::ios::binary);out.write(reinterpret_cast<const char*>(data),std::streamsize(count*sizeof(T)));require(bool(out),"packet write failed");
}
struct ProbeInput {float normal[4]{},tangent[4]{},uv[4]{},view[4]{},camera_up[4]{},base_uv[4]{};};
static_assert(sizeof(ProbeInput)==96,"Probe input ABI");
static std::array<float,2> uv(const vrm::SurfaceTexture& t,const Json& source){
    const float x=source.at(0).get<float>()*t.scale[0],y=source.at(1).get<float>()*t.scale[1];
    return {t.offset[0]+std::cos(t.rotation)*x-std::sin(t.rotation)*y,t.offset[1]+std::sin(t.rotation)*x+std::cos(t.rotation)*y};
}
int main(int argc,char** argv){
#ifdef _WIN32
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
#endif
    std::set_terminate([]()noexcept{std::fputs("FAIL unexpected terminate in CPU surface exporter\n",stderr);std::fflush(stderr);std::_Exit(3);});
    if(argc==2&&std::string(argv[1])=="--self-test-terminate")std::terminate();
    try{
        require(argc==4,"usage: native_surface_fixture_export fixture.vrm reference.json output-directory");
        const auto result=vrm::load_vrm(fs::path(argv[1]));require(bool(result),result.error.c_str());const auto& avatar=result.avatar;
        Json reference;std::ifstream reference_file(argv[2]);reference_file>>reference;require(bool(reference_file),"reference read failed");
        require(avatar.materials.size()>=1&&avatar.meshes.size()==1&&avatar.skins.size()==1,"generated skinned fixture shape changed");
        const auto& primitive=avatar.meshes[0].primitives[0];require(primitive.skinned&&primitive.vertices.size()==4&&primitive.indices.size()==6,"quad was not imported intact");
        const auto automatic=evrm::make_native_surface_selection(avatar);const auto expected=reference.at("expected_features").get<std::uint32_t>();
        require(automatic.features.at(0)==expected,"actual imported Auto selection differs from literal fixture oracle");
        Json policies;
        for(const auto preset:{evrm::AvatarShaderPreset::GameLighting,evrm::AvatarShaderPreset::Enhanced,evrm::AvatarShaderPreset::Fast,evrm::AvatarShaderPreset::Matte}){
            evrm::AvatarAppearance appearance;appearance.whole=preset;
            const auto selected=evrm::make_native_surface_selection(avatar,&appearance);
            require(selected.features.at(0)==(preset==evrm::AvatarShaderPreset::Fast||preset==evrm::AvatarShaderPreset::Matte?0:expected),"inherited whole preset mismatch");
            appearance.materials={evrm::AvatarShaderPreset::Enhanced};
            require(evrm::make_native_surface_selection(avatar,&appearance).features.at(0)==expected,"explicit Enhanced override mismatch");
            appearance.materials={evrm::AvatarShaderPreset::Fast};
            require(evrm::make_native_surface_selection(avatar,&appearance).features.at(0)==0,"explicit Fast override mismatch");
            policies[std::to_string(static_cast<int>(preset))]=selected.features;
        }
        require(!expected||!(expected&1)||primitive.surface_vertices.size()==primitive.vertices.size(),"required imported tangent stream absent");
        const auto output=fs::path(argv[3]);fs::create_directories(output);
        const auto material=evrm::native_avatar_material(avatar.materials[0],1,0,false);
        const auto surface=evrm::native_surface_material(avatar.materials[0],automatic.features[0]);
        std::vector<std::uint8_t> constants(sizeof(material)+sizeof(surface));
        std::memcpy(constants.data(),&material,sizeof(material));std::memcpy(constants.data()+sizeof(material),&surface,sizeof(surface));
        write(output/"constants.bin",constants.data(),constants.size());
        std::vector<ProbeInput> inputs;
        for(const auto& sample:reference.at("samples")){
            ProbeInput input;input.normal[2]=1;
            for(unsigned c=0;c<4;++c)input.tangent[c]=sample.at("tangent").at(c).get<float>();
            for(unsigned c=0;c<3;++c){input.view[c]=sample.at("view").at(c).get<float>();input.camera_up[c]=sample.at("camera_up").at(c).get<float>();}
            auto normal_uv=std::array<float,2>{sample["normal_uv"][0],sample["normal_uv"][1]};
            auto rim_uv=std::array<float,2>{sample["rim_source_uv"][0],sample["rim_source_uv"][1]};
            if(avatar.materials[0].surface){normal_uv=uv(avatar.materials[0].surface->normal,sample.at("normal_uv"));rim_uv=uv(avatar.materials[0].surface->rim,sample.at("rim_source_uv"));}
            for(unsigned c=0;c<2;++c){input.uv[c]=normal_uv[c];input.uv[c+2]=rim_uv[c];input.base_uv[c]=sample.at("base_uv").at(c).get<float>();}
            inputs.push_back(input);
        }
        require(inputs.size()==16,"bounded fixed probe sample count changed");write(output/"inputs.bin",inputs.data(),inputs.size());
        Json packet={{"format","EVRM surface packet v1"},{"fixture",fs::path(argv[1]).filename().string()},{"constants","constants.bin"},{"inputs","inputs.bin"},{"sample_count",inputs.size()},{"diffuse",reference.at("diffuse")},{"policies",policies},{"features",expected},{"surface_vertex_bytes",primitive.surface_vertices.size()*sizeof(vrm::SurfaceVertex)},{"textures",Json::array()},{"reference",reference}};
        // Include the exact original fixture/reference inputs in the packet's
        // local byte set, so independent review can bind all consumed/provenance
        // bytes. A Python-only pending refresh is never marked verified here.
        fs::copy_file(fs::path(argv[1]),output/"source.vrm",fs::copy_options::overwrite_existing);
        fs::copy_file(fs::path(argv[2]),output/"source.reference.json",fs::copy_options::overwrite_existing);
        packet["provenance"]={{"status","actual-import-verified"},{"fixture_file","source.vrm"},{"reference_file","source.reference.json"}};
        const auto& source=avatar.materials[0];
        const int textures[]={source.base_texture,source.surface?source.surface->normal.texture:-1,source.surface?source.surface->matcap.texture:-1,source.surface?source.surface->rim.texture:-1};
        for(unsigned slot=0;slot<4;++slot){
            const auto name="texture"+std::to_string(slot)+".rgba";
            Json entry={{"file",name},{"srgb",slot!=1},{"wrap_s",33071},{"wrap_t",33071},{"min_filter",9728},{"mag_filter",9728}};
            std::vector<std::uint8_t> pixels;int width=1,height=1;
            const bool active=slot==0||(slot==1&&(expected&1))||(slot==2&&(expected&2))||(slot==3&&(expected&8));
            if(textures[slot]>=0&&active){
                const auto& texture=avatar.textures.at(std::size_t(textures[slot]));const auto& image=avatar.images.at(std::size_t(texture.image));
                width=image.width;height=image.height;pixels=image.rgba;
                entry["wrap_s"]=texture.wrap_s;entry["wrap_t"]=texture.wrap_t;entry["min_filter"]=texture.min_filter;entry["mag_filter"]=texture.mag_filter;
            }else if(slot==1)pixels={128,128,255,255};else if(slot==2)pixels={0,0,0,255};else pixels={255,255,255,255};
            require(width>0&&height>0&&std::size_t(width)*height*4==pixels.size(),"decoded texture shape mismatch");
            entry["width"]=width;entry["height"]=height;entry["bytes"]=pixels.size();entry["authored_active"]=active&&textures[slot]>=0;
            packet["textures"].push_back(entry);write(output/name,pixels.data(),pixels.size());
        }
        std::ofstream manifest(output/"packet.json");manifest<<packet.dump(2)<<'\n';require(bool(manifest),"manifest write failed");
        std::cout<<"PASS actual import/selection/packet "<<packet["fixture"]<<" features="<<expected<<'\n';return 0;
    }catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}
    catch(...){std::cerr<<"FAIL nonstandard exception in CPU surface exporter\n";return 1;}
}
