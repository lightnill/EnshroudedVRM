#include "../src/native_preview_handoff.hpp"
#include "../vendor/minhook/include/MinHook.h"
#include <atomic>
#include <iostream>
#include <stdexcept>
static std::atomic<unsigned> hook_calls{0};
extern "C" MH_STATUS WINAPI MH_Initialize(){++hook_calls;return MH_ERROR_NOT_INITIALIZED;}
extern "C" MH_STATUS WINAPI MH_CreateHook(LPVOID,LPVOID,LPVOID*){++hook_calls;return MH_ERROR_NOT_INITIALIZED;}
extern "C" MH_STATUS WINAPI MH_QueueEnableHook(LPVOID){++hook_calls;return MH_ERROR_NOT_INITIALIZED;}
extern "C" MH_STATUS WINAPI MH_ApplyQueued(){++hook_calls;return MH_ERROR_NOT_INITIALIZED;}
extern "C" MH_STATUS WINAPI MH_RemoveHook(LPVOID){++hook_calls;return MH_ERROR_NOT_INITIALIZED;}
int main(){try{
    evrm::initialize_native_preview_handoff();const auto started=GetTickCount64();
    while(evrm::native_preview_handoff_setup_status()==1&&GetTickCount64()-started<5000)Sleep(1);
    if(evrm::native_preview_handoff_setup_status()!=3)throw std::runtime_error("unsupported host admission did not decline");
    evrm::initialize_native_preview_handoff();Sleep(10);
    if(hook_calls.load()||evrm::native_preview_handoff_setup_status()!=3)throw std::runtime_error("unsupported host reached hook APIs or repeated installation");
    std::cout<<"actual asynchronous setup rejects unsupported current process before every hook API\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;}
