#include "../src/local_voice_observation.hpp"
#include <windows.h>
#include <array>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
namespace {
void need(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
unsigned queries=0,encodes=0,begins=0,accepts=0;std::int64_t result=7;
SIZE_T WINAPI no_query(LPCVOID,PMEMORY_BASIC_INFORMATION,SIZE_T){++queries;return 0;}
std::int64_t encode(void*,evrm::VoiceByteSpan*,const evrm::VoiceFloatSpan* input){
    need(GetLastError()==111,"borrowed path changed incoming error");++encodes;
    if(result>0&&input&&input->data)const_cast<float*>(input->data)[0]=.75f;
    SetLastError(222);return result;
}
std::uint64_t begin(void*,std::uint64_t,std::uint64_t)noexcept {need(encodes==begins,"admission must precede original");++begins;SetLastError(333);return 19;}
void accept(void*,const std::array<float,480>& samples,std::uint64_t,std::uint64_t,std::uint64_t token)noexcept {
    need(encodes==begins&&samples[0]==.75f&&token==19,"borrowed snapshot must follow successful original");++accepts;SetLastError(444);
}
std::int64_t fault(void*,evrm::VoiceByteSpan*,const evrm::VoiceFloatSpan*){RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return -99;}
}
extern "C" {SIZE_T(WINAPI* __wrap___imp_VirtualQuery)(LPCVOID,PMEMORY_BASIC_INFORMATION,SIZE_T)=&no_query;}
int main(int argc,char**){
    SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
    evrm::VoiceObservation observed;std::array<std::uint64_t,3> encoder{0,1,0};std::array<float,480> samples{};
    evrm::VoiceFloatSpan input{samples.data(),samples.size()};const evrm::VoiceFrameSink sink{nullptr,begin,accept};
    if(argc==2){evrm::observe_voice_encode(observed,encoder.data(),nullptr,&input,fault,true,nullptr,evrm::VoiceReadContract::BorrowedThroughReturn);return 97;}
    const auto started=std::chrono::steady_clock::now();
    for(unsigned n=0;n<10000;++n){SetLastError(111);need(evrm::observe_voice_encode(observed,encoder.data(),nullptr,&input,encode,true,&sink,evrm::VoiceReadContract::BorrowedThroughReturn)==7,"exact original result");need(GetLastError()==222,"exact original error");}
    const auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-started).count()/10000;
    need(queries==0&&accepts==10000&&observed.snapshot().accepted==10000,"borrowed production path must accept PCM without page queries");
    need(observed.snapshot().borrowed_calls==10000,"borrowed attribution missing");
    for(auto failure:{0,-3}){result=failure;input.data=reinterpret_cast<float*>(1);SetLastError(111);
        need(evrm::observe_voice_encode(observed,reinterpret_cast<void*>(1),nullptr,&input,encode,true,&sink,evrm::VoiceReadContract::BorrowedThroughReturn)==failure,"failed encode forwarding");}
    need(accepts==10000&&queries==0,"unsuccessful original must not read PCM");
    result=7;input.data=samples.data();input.count=479;SetLastError(111);
    evrm::observe_voice_encode(observed,encoder.data(),nullptr,&input,encode,true,&sink,evrm::VoiceReadContract::BorrowedThroughReturn);
    need(accepts==10000&&observed.snapshot().rejected>=1,"wrong native shape rejected");
    std::printf("PASS borrowed post-encode PCM, zero page queries, original result/error/fault boundary; %.3f us/block\n",us);
}
