#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
namespace evrm {
#include "standalone_log_commit.inc"
}
static std::string read(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 2;
    const auto root=std::filesystem::path(argv[1]);const auto original=root/L"original.txt",target=root/L"late-replacement.log";
    std::ofstream(original)<<"preserve original";std::filesystem::create_hard_link(original,target);
    // This is the final open/write stage, after any earlier pathname check.
    if(evrm::write_new(target,"must not write",CREATE_ALWAYS) || read(original)!="preserve original")return 10;
    std::filesystem::remove(target);
    if(!evrm::write_new(target,"fresh log",CREATE_ALWAYS) || read(target)!="fresh log")return 11;
    std::cout<<"final diagnostic open rejects late hard-link replacement without truncating its target\n";return 0;
}
