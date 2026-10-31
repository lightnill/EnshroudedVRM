#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <atomic>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <thread>
#include <vector>

namespace {
std::atomic<unsigned> opens{0},writes{0};bool fail_write=false,partial_then_fail=false;
HANDLE WINAPI measured_open(LPCWSTR path,DWORD access,DWORD share,LPSECURITY_ATTRIBUTES security,DWORD creation,DWORD flags,HANDLE template_file){
    ++opens;return CreateFileW(path,access,share,security,creation,flags,template_file);
}
BOOL WINAPI measured_write(HANDLE file,LPCVOID data,DWORD bytes,LPDWORD written,LPOVERLAPPED overlap){
    if(fail_write){fail_write=false;*written=0;SetLastError(ERROR_DISK_FULL);return FALSE;}
    if(partial_then_fail){partial_then_fail=false;fail_write=true;return WriteFile(file,data,1,written,overlap);}
    const auto result=WriteFile(file,data,bytes,written,overlap);if(result&&*written==bytes)++writes;return result;
}
std::vector<std::string> lines(const std::filesystem::path& path){std::ifstream in(path);std::vector<std::string> rows;std::string row;while(std::getline(in,row))rows.push_back(row);return rows;}
}
#define CreateFileW measured_open
#define WriteFile measured_write
#include "../src/runtime_log.hpp"
#undef WriteFile
#undef CreateFileW
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAILED: %s at %d\n",#x,__LINE__);return 1;}}while(false)

int wmain(int argc,wchar_t** argv){
    (void)&measured_open;(void)&measured_write;
    SetErrorMode(GetErrorMode()|0x8003);CHECK(argc==2);const std::filesystem::path root=argv[1],first=root/L"first.log",second=root/L"second.log";
    CHECK(SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",first.c_str()));
    for(unsigned n=0;n<100;++n)evrm::log_event("visible");
    CHECK(lines(first).size()==100);CHECK(opens==1); // Reuse the actual append handle; no per-line reopen.
    auto reader=CreateFileW(first.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,0,nullptr);
    CHECK(reader!=INVALID_HANDLE_VALUE);CloseHandle(reader);
    auto renamed=root/L"renamed.log";std::filesystem::rename(first,renamed);evrm::log_event("after rename");
    CHECK(lines(renamed).size()==100&&lines(first).size()==1); // Rotation follows the configured pathname.
    std::filesystem::remove(first);evrm::log_event("after delete");CHECK(lines(first).size()==1);
    CHECK(SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",second.c_str()));evrm::log_event(std::string(5000,'X'));
    const auto long_rows=lines(second);CHECK(long_rows.size()==1&&long_rows[0].size()<4200&&long_rows[0].substr(long_rows[0].size()-4096)==std::string(4096,'X'));
    CHECK(SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",nullptr));evrm::log_event("disabled");CHECK(lines(second).size()==1);
    CHECK(SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",first.c_str()));fail_write=true;evrm::log_event("failed");
    const auto attempts=opens.load();evrm::log_event("recovered");CHECK(opens==attempts+1&&lines(first).size()==2);
    partial_then_fail=true;evrm::log_event("partially written then failed");evrm::log_event("after partial failure");
    auto replacement=root/L"replacement.log";{std::ofstream output(replacement);output<<"replacement\n";}
    if(!ReplaceFileW(first.c_str(),replacement.c_str(),nullptr,0,nullptr,nullptr)){std::fprintf(stderr,"replacement error=%lu\n",GetLastError());return 1;}
    evrm::log_event("after replacement");CHECK(lines(first).size()==2);
    const auto unavailable=root/L"later/created.log";CHECK(SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",unavailable.c_str()));evrm::log_event("missing parent");
    std::filesystem::create_directory(unavailable.parent_path());evrm::log_event("available");CHECK(lines(unavailable).size()==1);
    wchar_t cwd[32768];CHECK(GetCurrentDirectoryW(32768,cwd));
    const auto a=root/L"a",b=root/L"b";std::filesystem::create_directory(a);std::filesystem::create_directory(b);
    CHECK(SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",L"relative.log"));CHECK(SetCurrentDirectoryW(a.c_str()));evrm::log_event("a");
    CHECK(SetCurrentDirectoryW(b.c_str()));evrm::log_event("b");CHECK(lines(a/L"relative.log").size()==1&&lines(b/L"relative.log").size()==1);
    CHECK(SetCurrentDirectoryW(cwd));
    CHECK(SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",second.c_str()));
    std::vector<std::thread> threads;for(unsigned t=0;t<4;++t)threads.emplace_back([t]{for(unsigned i=0;i<100;++i)evrm::log_event("thread="+std::to_string(t)+" item="+std::to_string(i));});
    for(auto& thread:threads)thread.join();
    CHECK(lines(second).size()==401);
    for(unsigned i=0;i<10010;++i)evrm::log_event("bounded");
    CHECK(writes==9998); // Zero-byte and partial failures also consume their attempted-event budget.
    evrm::log_event_lazy([]()->std::string{throw 1;});evrm::log_event(static_cast<const char*>(nullptr));
    CHECK(writes==9998);CHECK(SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",nullptr));evrm::log_event("close");
    std::puts("persistent diagnostics: immediate visibility, reuse, rotation, deletion, switching, recovery, threads and bounds passed");return 0;
}
