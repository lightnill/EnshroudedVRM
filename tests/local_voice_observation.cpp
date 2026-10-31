#include "../src/local_voice_observation.hpp"
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
namespace {
using ReadMemory=BOOL(WINAPI*)(HANDLE,LPCVOID,LPVOID,SIZE_T,SIZE_T*);
ReadMemory real_read=nullptr;unsigned process_reads=0;
using QueryMemory=SIZE_T(WINAPI*)(LPCVOID,PMEMORY_BASIC_INFORMATION,SIZE_T);
QueryMemory real_query=nullptr;void* flip_after_query=nullptr;bool flipped=false;
BOOL WINAPI observed_read(HANDLE process,LPCVOID source,LPVOID output,SIZE_T size,SIZE_T* copied){++process_reads;return real_read(process,source,output,size,copied);}
SIZE_T WINAPI observed_query(LPCVOID source,PMEMORY_BASIC_INFORMATION output,SIZE_T size){const auto result=real_query(source,output,size);
    if(result&&source==flip_after_query){DWORD old=0;flipped=VirtualProtect(flip_after_query,4096,PAGE_NOACCESS,&old)!=FALSE;flip_after_query=nullptr;}return result;}
}
extern "C" {ReadMemory __wrap___imp_ReadProcessMemory=&observed_read;QueryMemory __wrap___imp_VirtualQuery=&observed_query;}
#endif

namespace {
void need(bool value,const char* message){if(!value){std::fprintf(stderr,"FAIL %s\n",message);std::exit(1);}}
unsigned forwarded=0;
void* expected_encoder=nullptr;
evrm::VoiceByteSpan* expected_output=nullptr;
const evrm::VoiceFloatSpan* expected_input=nullptr;
std::int64_t returned=7;
std::int64_t fault_encoder(void*,evrm::VoiceByteSpan*,const evrm::VoiceFloatSpan*){RaiseException(EXCEPTION_ACCESS_VIOLATION,0,0,nullptr);return -99;}
bool preserve_thread_error=false;
bool mutate_samples=false;
std::int64_t encode(void* encoder,evrm::VoiceByteSpan* output,const evrm::VoiceFloatSpan* input){
#ifdef _WIN32
    if(preserve_thread_error){need(GetLastError()==111,"diagnostics changed the encoder's incoming thread error");SetLastError(222);}
#endif
    need(encoder==expected_encoder&&output==expected_output&&input==expected_input,"relay changed native arguments");
    ++forwarded;
    if(mutate_samples&&input&&input->data)const_cast<float*>(input->data)[0]=.75f;
    if(output&&output->data&&output->capacity>=2){output->data[0]=0xa5;output->data[1]=0x3c;}
    return returned;
}
struct SinkProof {unsigned begins=0,accepts=0,forwarded_before=0;bool admitted=true;std::array<float,480> received{};};
std::uint64_t begin_sink(void* context,std::uint64_t source,std::uint64_t timestamp)noexcept {
    auto& proof=*static_cast<SinkProof*>(context);++proof.begins;
    need(source==reinterpret_cast<std::uint64_t>(expected_encoder)&&timestamp>0,"sink source/time metadata");
    need(forwarded==proof.forwarded_before,"capture token obtained after encoding");SetLastError(333);
    return proof.admitted?91:0;
}
void accept_sink(void* context,const std::array<float,480>& samples,std::uint64_t source,std::uint64_t timestamp,std::uint64_t token)noexcept {
    auto& proof=*static_cast<SinkProof*>(context);++proof.accepts;proof.received=samples;
    need(source==reinterpret_cast<std::uint64_t>(expected_encoder)&&timestamp>0&&token==91,"delivered sink token/source/time");
    need(forwarded==proof.forwarded_before+1,"samples delivered before successful encoding");SetLastError(444);
}
}

int main(int argc,char** argv){
#ifdef _WIN32
    SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
    const auto reader=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"ReadProcessMemory");
    static_assert(sizeof reader==sizeof real_read);std::memcpy(&real_read,&reader,sizeof real_read);
    need(real_read!=nullptr,"fixture reader import");
    const auto query=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"VirtualQuery");std::memcpy(&real_query,&query,sizeof real_query);need(real_query!=nullptr,"fixture validator import");
    if(argc==2&&std::strcmp(argv[1],"--encoder-fault")==0){std::array<std::uint64_t,3> state{0,1,0};std::array<float,480> pcm{};
        evrm::VoiceFloatSpan span{pcm.data(),pcm.size()};evrm::VoiceObservation observed;evrm::observe_voice_encode(observed,state.data(),nullptr,&span,fault_encoder,true);return 97;}
#endif
    evrm::VoiceSourceIdentity identity;
    identity.file_sha256={0xd6,0x49,0x66,0xe2,0x3b,0x1a,0x45,0x8e,0xe6,0xf1,0x47,0xbc,0xbc,0xea,0xc0,0xb5,
        0x77,0xfa,0x7e,0x39,0xa2,0x12,0xf3,0x71,0x28,0x7d,0x8c,0xc1,0x19,0x81,0x78,0x82};
    identity.timestamp=1792507660;identity.image_bytes=44163072;
    identity.encoder_prologue={0x48,0x83,0xec,0x38,0x44,0x8b,0x49,0x08,0x4c,0x8b,0xd2,0x49,0x8b,0x40,0x08,0x33};
    identity.encoder_call={0xe8,0x8f,0xe2,0xc6,0xff};
    need(evrm::local_voice_identity_supported(identity),"supported voice source identity declined");
    auto changed=identity;changed.file_sha256[31]^=1;
    need(!evrm::local_voice_identity_supported(changed),"different executable hash accepted");
    changed=identity;++changed.timestamp;
    need(!evrm::local_voice_identity_supported(changed),"different loaded timestamp accepted");
    changed=identity;++changed.image_bytes;
    need(!evrm::local_voice_identity_supported(changed),"different image extent accepted");
    changed=identity;changed.encoder_prologue[0]=0xe9;
    need(!evrm::local_voice_identity_supported(changed),"already-patched encoder accepted");
    changed=identity;changed.encoder_call[1]^=1;
    need(!evrm::local_voice_identity_supported(changed),"redirected encoder caller accepted");
    std::array<float,480> samples{};samples.fill(.5f);
    const auto half=evrm::summarize_voice_frame(1,samples.data(),480);
    need(half.valid&&half.nonzero&&half.peak==.5f&&half.mean_square==.25,"literal half-volume mono frame");
    samples.fill(0);
    const auto silence=evrm::summarize_voice_frame(1,samples.data(),480);
    need(silence.valid&&!silence.nonzero&&silence.peak==0&&silence.mean_square==0,"silence reported as speech");
    need(!evrm::summarize_voice_frame(1,nullptr,480).valid,"null input accepted");
    need(!evrm::summarize_voice_frame(2,samples.data(),480).valid,"stereo input accepted");
    need(!evrm::summarize_voice_frame(1,samples.data(),479).valid,"short input accepted");
    need(!evrm::summarize_voice_frame(1,samples.data(),std::numeric_limits<std::size_t>::max()).valid,"unbounded input accepted");
    samples.back()=std::numeric_limits<float>::quiet_NaN();
    need(!evrm::summarize_voice_frame(1,samples.data(),480).valid,"last NaN sample was ignored");
    samples.back()=std::numeric_limits<float>::infinity();
    need(!evrm::summarize_voice_frame(1,samples.data(),480).valid,"infinite input accepted");
    samples.back()=17;
    need(!evrm::summarize_voice_frame(1,samples.data(),480).valid,"unbounded diagnostic amplitude accepted");

    // The real consumer produces bytes; observation must preserve that behavior.
    samples.fill(.5f);const auto unchanged=samples;
    std::array<std::uint64_t,3> encoder{0,1,0};
    std::array<std::uint8_t,64> bytes{};
    evrm::VoiceByteSpan output{bytes.data(),bytes.size()};
    evrm::VoiceFloatSpan input{samples.data(),samples.size()};
    expected_encoder=encoder.data();expected_output=&output;expected_input=&input;
    evrm::VoiceObservation observation;
    auto value=evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true);
    auto stats=observation.snapshot();
    need(value==7&&forwarded==1&&bytes[0]==0xa5&&bytes[1]==0x3c,"original result/output not preserved");
    need(samples==unchanged,"observation changed audio samples");
    need(stats.calls==1&&stats.accepted==1&&stats.nonzero==1&&stats.rejected==0,"valid block counters incorrect");
#ifdef _WIN32
    need(process_reads==0,"audio callback uses generic process-memory reads");
#endif

    // Disabled observation must not even dereference an encoder/input pointer.
    expected_encoder=nullptr;expected_input=nullptr;
    value=evrm::observe_voice_encode(observation,nullptr,&output,nullptr,encode,false);
    stats=observation.snapshot();
    need(value==7&&forwarded==2&&stats.calls==1,"disabled observation inspected/counted input or skipped forwarding");

    expected_encoder=encoder.data();expected_input=&input;input.count=479;
    value=evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true);
    stats=observation.snapshot();
    need(value==7&&forwarded==3&&stats.calls==2&&stats.rejected==1&&stats.accepted==1,"invalid observation altered real encode behavior");
    input.count=480;returned=-3;
    value=evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true);
    stats=observation.snapshot();
    need(value==-3&&forwarded==4&&stats.encode_failures==1&&stats.accepted==1,"encoder failure was hidden or counted as accepted audio");
    returned=7;samples.fill(0);
    evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true);
    stats=observation.snapshot();
    need(stats.accepted==2&&stats.nonzero==1,"silent encoded block inflated voice activity");

#ifdef _WIN32
    // The original is allowed to handle an argument diagnostics cannot read.
    SYSTEM_INFO system{};GetSystemInfo(&system);const auto page=system.dwPageSize;
    auto* memory=static_cast<std::uint8_t*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    need(memory!=nullptr,"fault fixture allocation");DWORD old=0;
    need(VirtualProtect(memory+page,page,PAGE_NOACCESS,&old)!=FALSE,"fault fixture protection");
    const auto reject_unreadable=[&](void* state,const evrm::VoiceFloatSpan* span){
        const auto before=observation.snapshot();const auto forwarded_before=forwarded;
        expected_encoder=state;expected_input=span;bytes.fill(0);
        const auto result=evrm::observe_voice_encode(observation,state,&output,span,encode,true);
        const auto after=observation.snapshot();
        need(result==7&&forwarded==forwarded_before+1&&bytes[0]==0xa5&&bytes[1]==0x3c,
            "unreadable diagnostic input prevented exact original forwarding");
        need(after.calls==before.calls+1&&after.rejected==before.rejected+1&&after.accepted==before.accepted,
            "unreadable diagnostic input was accepted");
    };
    evrm::VoiceFloatSpan partial{reinterpret_cast<float*>(memory+page-64),480};
    reject_unreadable(encoder.data(),&partial);
    reject_unreadable(encoder.data(),reinterpret_cast<const evrm::VoiceFloatSpan*>(memory+page));
    reject_unreadable(memory+page,&input);
    flip_after_query=memory;partial.data=reinterpret_cast<float*>(memory);reject_unreadable(encoder.data(),&partial);
    need(flipped,"read protection changed after validation");need(VirtualProtect(memory,page,PAGE_READWRITE,&old)!=FALSE,"fault race fixture restored");
    preserve_thread_error=true;SetLastError(111);
    reject_unreadable(reinterpret_cast<void*>(std::uintptr_t(0xffffffffffff0000ull)),&input);
    need(GetLastError()==222,"diagnostics changed the original encoder's outgoing thread error");
    preserve_thread_error=false;
    need(VirtualProtect(memory+page,page,PAGE_READWRITE|PAGE_GUARD,&old)!=FALSE,"guard fixture protection");
    partial.data=reinterpret_cast<float*>(memory+page);
    reject_unreadable(encoder.data(),&partial);
    MEMORY_BASIC_INFORMATION guarded{};VirtualQuery(memory+page,&guarded,sizeof guarded);
    need((guarded.Protect&PAGE_GUARD)!=0,"inspection consumed the caller's page guard");
    need(VirtualFree(memory,0,MEM_RELEASE)!=FALSE,"fault fixture release");
    expected_encoder=encoder.data();expected_input=&input;
#endif

    // The sink sees the checked pre-encode copy, once and only after success.
    SinkProof proof;evrm::VoiceFrameSink sink{&proof,begin_sink,accept_sink};
    preserve_thread_error=true;mutate_samples=true;samples.fill(.5f);proof.forwarded_before=forwarded;SetLastError(111);
    value=evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true,&sink);
    need(value==7&&proof.begins==1&&proof.accepts==1&&proof.received==unchanged,"sink lost exact pre-encode samples");
    need(GetLastError()==222,"post-encode sink changed original thread error");mutate_samples=false;
    for(auto result:{std::int64_t(-3),std::int64_t(0)}){returned=result;proof.forwarded_before=forwarded;SetLastError(111);
        evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true,&sink);
        need(proof.accepts==1&&GetLastError()==222,"failed/empty encoding delivered samples or changed thread error");}
    returned=7;proof.admitted=false;proof.forwarded_before=forwarded;SetLastError(111);
    evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true,&sink);
    need(proof.accepts==1,"invalid pre-encode token delivered samples");
    const auto begins=proof.begins;input.count=479;proof.forwarded_before=forwarded;SetLastError(111);
    evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true,&sink);
    need(proof.begins==begins+1&&proof.accepts==1,"invalid block delivered PCM or bypassed generation admission");input.count=480;
    SetLastError(111);evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,false,&sink);
    need(proof.begins==begins+1&&proof.accepts==1,"disabled relay called sink");preserve_thread_error=false;

    samples.fill(.25f);
    const auto start=std::chrono::steady_clock::now();
    constexpr unsigned blocks=50000;
    for(unsigned n=0;n<blocks;++n)evrm::observe_voice_encode(observation,encoder.data(),&output,&input,encode,true);
    const double ns=std::chrono::duration<double,std::nano>(std::chrono::steady_clock::now()-start).count()/blocks;
    const auto timed=observation.snapshot();
    need(timed.profiled_calls>0&&timed.sampled_qpc_nanoseconds>0&&timed.sampled_thread_cycles>0,"monotonic/cycle diagnostics missing");
    need(timed.sampled_queries>=timed.sampled_copies&&timed.sampled_copies>0&&timed.sampled_validation_ns>0&&timed.sampled_copy_ns>0,
        "validation/copy component diagnostics missing");
    need(timed.sampled_pre_ns+timed.sampled_post_ns==timed.sampled_qpc_nanoseconds&&process_reads==0,"sampled total or generic-read regression");
    std::printf("PASS local voice format/forwarding/counters; synthetic observer ns-per-block=%.1f (not game performance)\n",ns);
}
