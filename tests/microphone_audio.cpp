#include "../src/microphone_audio.hpp"
#include <vector>
#include <limits>
#include <cstdio>
#include <cstdlib>
namespace {
void need(bool yes,const char* why){if(!yes){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
struct Output {std::vector<std::array<float,480>> audio;std::vector<std::uint64_t> timestamps;unsigned resets=0;};
}
int main(){
    evrm::MicrophoneAudioAssembler assembler;Output output;
    evrm::MicrophoneAudioSink sink{&output,[](void* p)noexcept{++static_cast<Output*>(p)->resets;},
        [](void* p,const std::array<float,480>& samples,std::uint64_t timestamp)noexcept{auto& out=*static_cast<Output*>(p);out.audio.push_back(samples);out.timestamps.push_back(timestamp);}};
    std::vector<float> data(960,.25f);
    assembler.push({data.data(),200,100000,false,false,false},110000,sink);need(output.audio.empty(),"partial block emitted");
    assembler.push({data.data()+200,280,104166,false,false,false},110000,sink);
    need(output.audio.size()==1&&output.timestamps[0]>=109999&&output.timestamps[0]<=110000,"fragmented block clock/framing");
    assembler.push({data.data(),960,110000,false,false,false},130000,sink);need(output.audio.size()==3,"multi-block packet lost samples");
    need(output.timestamps[1]==120000&&output.timestamps[2]==130000,"packet timestamps lost sample duration");
    assembler.push({nullptr,480,130000,true,false,false},140000,sink);need(output.audio.size()==4,"silent packet missing");
    for(float v:output.audio.back())need(v==0,"silent null packet read memory");
    data[30]=std::numeric_limits<float>::quiet_NaN();assembler.push({data.data(),480,140000,false,false,false},150000,sink);
    need(output.audio.size()==4&&output.resets>0,"invalid samples admitted");data[30]=.25f;
    assembler.push({data.data(),200,150000,false,false,false},160000,sink);data.assign(960,.75f);
    assembler.push({data.data(),480,160000,false,true,false},170000,sink);
    need(output.audio.size()==5,"discontinuity failed to recover");for(float v:output.audio.back())need(v==.75f,"old partial audio crossed discontinuity");
    assembler.push({data.data(),480,170000,false,false,false},300000,sink);need(output.audio.size()==5,"old microphone packet was admitted");
    assembler.push({data.data(),480,310000,false,false,false},300000,sink);need(output.audio.size()==5,"future microphone packet admitted");
    assembler.push({data.data(),480,300000,false,false,true},310000,sink);need(output.audio.size()==6,"uncertain timestamp did not recover fresh data");
    need(assembler.discarded_blocks()>=2,"stale/future packet accounting missing");
    std::puts("PASS microphone framing, fragmented/multiple/silent packets, invalid samples, discontinuity, freshness and recovery");
}
