#include "avatar_fixture.hpp"
#include "../src/avatar_model.hpp"
#include "../src/sha256.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include "texture_depth_pngs.hpp"
void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
template<class T>auto retained_cpu_bytes(const T& value,int)->decltype(value.retained_cpu_buffer_bytes){return value.retained_cpu_buffer_bytes;}
template<class T>std::uint64_t retained_cpu_bytes(const T&,long){return 0;}
std::vector<std::uint8_t> textured_fixture(){
    auto bytes=avatar_fixture_bytes();std::uint32_t size=0;std::memcpy(&size,bytes.data()+12,4);
    auto doc=nlohmann::json::parse(bytes.begin()+20,bytes.begin()+20+size);
    std::vector<std::uint8_t> bin(bytes.begin()+28+size,bytes.end());
    const float uv[]={0,0,1,0,0,1};const auto uv_offset=bin.size();const auto* uv_bytes=reinterpret_cast<const std::uint8_t*>(uv);
    bin.insert(bin.end(),uv_bytes,uv_bytes+sizeof uv);const auto uv_view=doc["bufferViews"].size();
    doc["bufferViews"].push_back({{"buffer",0},{"byteOffset",uv_offset},{"byteLength",sizeof uv}});
    const auto uv_accessor=doc["accessors"].size();doc["accessors"].push_back({{"bufferView",uv_view},{"componentType",5126},{"count",3},{"type","VEC2"}});
    doc["meshes"][0]["primitives"][0]["attributes"]["TEXCOORD_0"]=uv_accessor;
    const auto image_offset=bin.size();bin.insert(bin.end(),png_rgba8.begin(),png_rgba8.end());while(bin.size()%4)bin.push_back(0);
    const auto image_view=doc["bufferViews"].size();doc["bufferViews"].push_back({{"buffer",0},{"byteOffset",image_offset},{"byteLength",png_rgba8.size()}});
    doc["images"]=nlohmann::json::array({{{"bufferView",image_view},{"mimeType","image/png"}}});
    doc["textures"]=nlohmann::json::array({{{"source",0}},{{"source",0}}});
    doc["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"]={{"index",0}};
    doc["materials"][0]["normalTexture"]={{"index",1}};doc["buffers"][0]["byteLength"]=bin.size();
    auto text=doc.dump();while(text.size()%4)text+=' ';bytes.clear();
    const auto word=[&](std::uint32_t value){const auto* first=reinterpret_cast<const std::uint8_t*>(&value);bytes.insert(bytes.end(),first,first+4);};
    word(0x46546c67);word(2);word(std::uint32_t(28+text.size()+bin.size()));word(std::uint32_t(text.size()));word(0x4e4f534a);
    bytes.insert(bytes.end(),text.begin(),text.end());word(std::uint32_t(bin.size()));word(0x004e4942);bytes.insert(bytes.end(),bin.begin(),bin.end());return bytes;
}
int wmain(int argc,wchar_t** argv){try{
    need(argc==2,"fixture folder required");const auto bytes=avatar_fixture_bytes();
    auto loaded=evrm::prepare_avatar_bytes(bytes,"C:\\fixture\\first.vrm");need(bool(loaded),loaded.error.c_str());
    need(loaded.model->asset&&loaded.model->skin&&bool(*loaded.model->skin)&&loaded.model->material_names==std::vector<std::string>{"Skin"},"complete prepared model missing");
    need(loaded.model->skin->vertices.size()==3&&loaded.model->fingerprint.size()==64,"compiled skin/hash missing");
    need(retained_cpu_bytes(*loaded.model,0)>=528,"prepared model does not cache retained CPU buffer usage");
    const auto textured=evrm::prepare_avatar_bytes(textured_fixture(),"C:\\fixture\\textured.vrm");
    need(bool(textured),textured.error.c_str());
    need(textured.model->decoded_texture_bytes==8&&textured.model->retained_cpu_buffer_bytes>8,
         "prepared memory usage duplicates shared texture references or omits CPU mesh buffers");
    const auto again=evrm::prepare_avatar_bytes(bytes,"C:\\fixture\\other.vrm");need(bool(again)&&again.model->fingerprint==loaded.model->fingerprint,"same contents changed identity with path");
    const auto below=evrm::prepare_avatar_bytes(avatar_fixture_bytes("Below origin",-1.f),"C:\\fixture\\below.vrm");need(!below&&!below.error.empty(),"runtime-incompatible head height accepted by worker");
    const auto changed=evrm::prepare_avatar_bytes(avatar_fixture_bytes("Changed avatar"),"C:\\fixture\\first.vrm");
    need(bool(changed)&&changed.model->fingerprint!=loaded.model->fingerprint,"same path reused changed content identity");
    const auto path=std::filesystem::path(argv[1])/L"sample.vrm";{std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(bytes.data()),bytes.size());}
    const auto disk=evrm::load_prepared_avatar(path.u8string());need(bool(disk)&&disk.model->fingerprint==loaded.model->fingerprint,"file read/hash/import disagreed");
    need(!evrm::prepare_avatar_bytes({0,1,2},"C:\\fixture\\bad.vrm")&&!evrm::load_prepared_avatar("\\\\host\\avatar.vrm"),"invalid/remote avatar accepted");
    need(!evrm::load_prepared_avatar((std::filesystem::path(argv[1])/L"missing.vrm").u8string()),"missing avatar accepted");
    std::cout<<"PASS single-buffer avatar identity/import, prepared static skin, path/content changes and invalid file rejection\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
