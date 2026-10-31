#include "../src/voice_automatic.hpp"
#include <windows.h>
#include <array>
#include <fstream>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cmath>
#include <limits>
namespace {
void need(bool ok,const char* text){if(!ok){std::fprintf(stderr,"FAIL %s\n",text);std::exit(1);}}
template<class F>F symbol(HMODULE module,const char* name){const auto raw=GetProcAddress(module,name);F result{};static_assert(sizeof raw==sizeof result);std::memcpy(&result,&raw,sizeof result);need(result!=nullptr,"SDK export missing");return result;}
std::uint64_t process_cpu(){FILETIME created{},exited{},kernel{},user{};need(GetProcessTimes(GetCurrentProcess(),&created,&exited,&kernel,&user)!=FALSE,"process CPU clock");
    return (std::uint64_t(kernel.dwHighDateTime)<<32)+kernel.dwLowDateTime+(std::uint64_t(user.dwHighDateTime)<<32)+user.dwLowDateTime;}
}
int main(int argc,char** argv){
    evrm::AutomaticVoiceClassifier classifier;
    if(argc==2){need(!classifier.ready(),"untrusted/missing dependency loaded");need(!classifier.consume({}).ready,"unavailable detector emitted speech");
        if(std::strcmp(argv[1],"--bad-hash")==0)need(std::strcmp(classifier.diagnostic(),"sdk-hash")==0,"same-sized changed DLL bypassed hash validation");
        std::puts("PASS missing/tampered SDK declines safely");return 0;}
    need(argc==3,"PCM and official DLL fixture paths");need(classifier.ready(),classifier.diagnostic());
    const auto module=LoadLibraryExA(argv[2],nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);need(module!=nullptr,"official SDK fixture load");
    const auto create=symbol<int(*)(unsigned*,int,int,bool)>(module,"ovrLipSyncDll_CreateContextEx");
    const auto destroy=symbol<int(*)(unsigned)>(module,"ovrLipSyncDll_DestroyContext");
    const auto send=symbol<int(*)(unsigned,int,int,int)>(module,"ovrLipSyncDll_SendSignal");
    const auto process=symbol<int(*)(unsigned,const void*,unsigned,int,int*,int*,float*,int,float*,float*,int)>(module,"ovrLipSyncDll_ProcessFrameEx");
    unsigned context=0;need(create(&context,1,48000,false)==0&&context,"official context create");need(send(context,3,0,0)==0,"official smoothing signal");
    std::ifstream input(argv[1],std::ios::binary);need(bool(input),"public input fixture");unsigned blocks=0;double maximum_error=0;std::array<float,15> peaks{};
    const auto cpu_start=process_cpu();
    const auto start=std::chrono::steady_clock::now();std::array<float,480> pcm{};
    while(input.read(reinterpret_cast<char*>(pcm.data()),sizeof pcm)){const auto actual=classifier.consume(pcm);need(actual.ready,"automatic detector lost valid input");
        int number=0,delay=0;float laughter=0;std::array<float,15> expected{};
        need(process(context,pcm.data(),480,2,&number,&delay,expected.data(),15,&laughter,nullptr,0)==0,"official direct API frame");
        need(actual.frame_number==number&&actual.delay_ms==delay,"official frame metadata differs");
        for(unsigned n=0;n<15;++n){const auto value=actual.visemes[n];need(std::isfinite(value)&&value>=0&&value<=1,"nonfinite/unbounded SDK output");maximum_error=std::max(maximum_error,std::abs(double(value)-expected[n]));peaks[n]=std::max(peaks[n],value);}
        need(maximum_error<1e-6,"adapter differs from direct official export");++blocks;
    }
    const auto us=std::chrono::duration<double,std::micro>(std::chrono::steady_clock::now()-start).count()/blocks/2;
    const auto cpu_ms_per_second=double(process_cpu()-cpu_start)*.0001/(blocks*.01*2);
    need(cpu_ms_per_second<=20,"whole-process detector CPU exceeds isolated one-core budget");
    need(blocks>2000,"public reference duration");for(unsigned n=10;n<15;++n)need(peaks[n]>.05,"public speech did not reach a vowel");
    classifier.reset();pcm.fill(0);for(unsigned n=0;n<20;++n){const auto quiet=classifier.consume(pcm);need(quiet.ready,"reset context did not recover");}
    pcm[13]=std::numeric_limits<float>::quiet_NaN();need(!classifier.consume(pcm).ready,"invalid PCM entered SDK");pcm.fill(0);need(classifier.consume(pcm).ready,"valid PCM failed after invalid reset");
    need(destroy(context)==0,"fixture context destroy");FreeLibrary(module);
    std::printf("PASS official Enhanced SDK adapter, %u public blocks, direct ABI max-error %.9g, paired average %.3f us/block, whole-process %.3f ms/audio-second; not game performance\n",blocks,maximum_error,us,cpu_ms_per_second);
}
