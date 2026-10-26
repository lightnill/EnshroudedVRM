#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <filesystem>
#include <map>
#include <string>
#include <vector>

namespace evrm::portable {
namespace fs=std::filesystem;
struct IgnoreCase {bool operator()(const std::wstring&a,const std::wstring&b)const{return _wcsicmp(a.c_str(),b.c_str())<0;}};
using Environment=std::map<std::wstring,std::wstring,IgnoreCase>;
struct Selection {fs::path root,game,model;bool springs=true;};
struct LaunchPlan {Selection selection;Environment environment;fs::path log,cache;std::wstring command;std::string runtime_hash,publication_status;};
std::string utf8(const std::wstring&);
std::wstring wide(const std::string&);
std::wstring quote_argument(const std::wstring&);
bool safe_member(const std::string&);
bool valid_glb_header(const std::vector<unsigned char>&,std::uint64_t size);
std::vector<fs::path> steam_libraries(const std::string&);
std::string sha256(const fs::path&);
Environment launch_environment(const Environment&,const Selection&,const std::string&game_hash,const fs::path&log,const fs::path&cache);
std::vector<wchar_t> environment_block(const Environment&);
LaunchPlan prepare_launch(const Selection&,const Environment&,const std::wstring&session);
}
