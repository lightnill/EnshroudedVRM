#include "../src/avatar_model.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
int wmain(int argc,wchar_t** argv){try{
    if(argc!=2)throw std::runtime_error("sample file required");
    const auto result=evrm::load_prepared_avatar(std::filesystem::path(argv[1]).u8string());
    if(!result)throw std::runtime_error(result.error);
    const auto& model=*result.model;
    if(model.fingerprint!="da1e68ca997415de3ff55ba1a8468257c523c6d5db65d193f4110e37861bc098")throw std::runtime_error("sample bytes differ from creator-authorized model");
    if(!model.asset||!model.skin||model.material_names.empty()||!model.asset->vertex_count)throw std::runtime_error("sample has no usable native model");
    std::cout<<"PASS bundled VRM1 sample imported/prepared through actual runtime model loader: vertices="<<model.asset->vertex_count<<" materials="<<model.material_names.size()<<'\n';
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
