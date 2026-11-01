#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "../src/standalone_startup.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
namespace fs=std::filesystem;
namespace evrm {RuntimeStartupContext fixture;const RuntimeStartupContext& runtime_startup_context()noexcept{return fixture;}}
static std::wstring env(const wchar_t* name) {wchar_t value[32768]{};const auto count=GetEnvironmentVariableW(name,value,32768);return count && count<32768 ? std::wstring(value,count) : std::wstring{};}
static std::string read(const fs::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
int wmain(int argc,wchar_t** argv) {
    if(argc!=3)return 2;
    const fs::path root=argv[1];const std::wstring mode=argv[2];fs::create_directories(root);
    const auto original=root/L"untouched.log";std::ofstream(original)<<"do not touch";
    auto& context=evrm::fixture;context.version="standalone-test";
    if(mode!=L"declined") {context.status=evrm::RuntimeStartupStatus::Ready;context.state_directory=root/L"State";
        context.capture_directory=context.state_directory/L"capture-cache";context.log_path=context.state_directory/L"runtime.log";
        context.state_writable=mode==L"ready";fs::create_directories(context.state_directory);
        std::ofstream(context.log_path)<<std::string(300*1024,'x');
        if(mode==L"ready") {
            const auto history=context.state_directory/L"startup-history";fs::create_directories(history);
            for(int index=0;index<9;++index)std::ofstream(history/(L"startup-1-"+std::to_wstring(index)+L"-1.log"))<<std::string(48*1024,'h');
        }
    }
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",original.c_str());
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_CAPTURE_DIR",root.c_str());
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_CAPTURE_FRAMES",L"1,2,3");
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_MODEL",L"poison.vrm");
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_NATIVE_REPLACEMENT",L"1");
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_VERIFIED_BUILD",L"poison");
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_TRACE_COMMANDS",L"1");
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_DISABLE",L"preserve-user-value");
    evrm::initialize_runtime_startup_environment();evrm::initialize_runtime_startup_environment();
    if(read(original)!="do not touch" || env(L"ENSHROUDED_VRM_DISABLE")!=L"preserve-user-value")return 10;
    for(const auto name:{L"ENSHROUDED_VRM_MODEL",L"ENSHROUDED_VRM_NATIVE_REPLACEMENT",L"ENSHROUDED_VRM_VERIFIED_BUILD",L"ENSHROUDED_VRM_CAPTURE_FRAMES",L"ENSHROUDED_VRM_TRACE_COMMANDS"})if(!env(name).empty())return 11;
    if(mode==L"ready") {
        if(env(L"ENSHROUDED_VRM_LOG")!=context.log_path || env(L"ENSHROUDED_VRM_CAPTURE_DIR")!=context.capture_directory)return 12;
        const auto log=read(context.log_path);if(log.find("standalone.startup")==log.npos || log.find("standalone-test")==log.npos || log.size()>8192)return 13;
        const auto history=context.state_directory/L"startup-history";std::size_t count=0,total=0;
        for(const auto& file:fs::directory_iterator(history)){++count;total+=file.file_size();}
        if(count!=5 || total>256*1024)return 14;
    }else if(!env(L"ENSHROUDED_VRM_LOG").empty() || !env(L"ENSHROUDED_VRM_CAPTURE_DIR").empty())return 15;
    std::cout<<"checked startup environment, fresh diagnostic marker and bounded previous-session history passed\n";return 0;
}
