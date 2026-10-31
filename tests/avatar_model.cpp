#include "avatar_fixture.hpp"
#include "../src/avatar_model.hpp"
#include "../src/sha256.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
void need(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
int wmain(int argc,wchar_t** argv){try{
    need(argc==2,"fixture folder required");const auto bytes=avatar_fixture_bytes();
    auto loaded=evrm::prepare_avatar_bytes(bytes,"C:\\fixture\\first.vrm");need(bool(loaded),loaded.error.c_str());
    need(loaded.model->asset&&loaded.model->skin&&bool(*loaded.model->skin)&&loaded.model->material_names==std::vector<std::string>{"Skin"},"complete prepared model missing");
    need(loaded.model->skin->vertices.size()==3&&loaded.model->fingerprint.size()==64,"compiled skin/hash missing");
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
