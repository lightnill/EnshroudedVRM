#include "../src/voice_setup.hpp"
#include "../src/voice_worker.hpp"
#include <atomic>
#include <chrono>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
struct Work {
    HANDLE entered=CreateEventW(nullptr,TRUE,FALSE,nullptr),release=CreateEventW(nullptr,TRUE,FALSE,nullptr),finished=CreateEventW(nullptr,TRUE,FALSE,nullptr);
    std::atomic<unsigned> calls{0};std::atomic<bool> enabled{true};evrm::VoiceAnalysisWorker worker;
    ~Work(){CloseHandle(entered);CloseHandle(release);CloseHandle(finished);}
    static void run(void* context)noexcept{auto& w=*static_cast<Work*>(context);++w.calls;SetEvent(w.entered);WaitForSingleObject(w.release,INFINITE);
        w.worker.set_enabled(w.enabled.load());SetEvent(w.finished);}
};
}
int main(){try{
    Work work;require(work.worker.ready(),"analysis worker");
    {
        evrm::VoiceSetup setup(&Work::run,&work);require(setup.ready(),"setup thread");
        setup.request();require(WaitForSingleObject(work.entered,2000)==WAIT_OBJECT_0,"background entered");
        const auto start=std::chrono::steady_clock::now();for(unsigned i=0;i<1000;++i)setup.request();
        require(std::chrono::steady_clock::now()-start<std::chrono::milliseconds(50),"request waited for blocked setup");
        work.enabled=false;work.worker.set_enabled(false);SetEvent(work.release);
        require(WaitForSingleObject(work.finished,2000)==WAIT_OBJECT_0,"background completed");
        Sleep(10);require(work.calls<=2,"requests coalesced");
        require(work.worker.begin_capture(42,10000)==0&&work.worker.snapshot().activity==evrm::VoiceActivity::Off,"late setup did not reenable");
    }
    std::cout<<"PASS background setup returns immediately, coalesces requests and preserves disable\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
