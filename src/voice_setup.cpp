#include "voice_setup.hpp"
namespace evrm {
VoiceSetup::VoiceSetup(Work fn,void* data)noexcept:work(fn),context(data){
    if(!work)return;
    event=CreateEventW(nullptr,FALSE,FALSE,nullptr);
    if(event)thread=CreateThread(nullptr,0,&run,this,0,nullptr);
}
VoiceSetup::~VoiceSetup(){stop.store(true,std::memory_order_release);request();if(thread){WaitForSingleObject(thread,INFINITE);CloseHandle(thread);}if(event)CloseHandle(event);}
DWORD WINAPI VoiceSetup::run(void* raw)noexcept {
    auto& state=*static_cast<VoiceSetup*>(raw);SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
    while(WaitForSingleObject(state.event,INFINITE)==WAIT_OBJECT_0){if(state.stop.load(std::memory_order_acquire))break;state.work(state.context);}return 0;
}
}
