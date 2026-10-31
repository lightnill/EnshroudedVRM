#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <atomic>
namespace evrm {
// The standalone owner can join in tests; production pins and retains its owner
// for the process lifetime. No render/audio thread waits for this callback.
class VoiceSetup {
public:
    using Work=void(*)(void*)noexcept;
    VoiceSetup(Work,void*)noexcept;
    ~VoiceSetup();
    VoiceSetup(const VoiceSetup&)=delete;
    VoiceSetup& operator=(const VoiceSetup&)=delete;
    bool ready()const noexcept{return thread!=nullptr;}
    void request()noexcept{if(event)SetEvent(event);}
private:
    static DWORD WINAPI run(void*)noexcept;
    Work work;void* context;HANDLE event=nullptr,thread=nullptr;std::atomic<bool> stop{false};
};
}
