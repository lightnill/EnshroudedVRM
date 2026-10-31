#include "../src/local_voice_observer.hpp"
#include "../src/local_voice_observation.hpp"
#include "../src/local_voice_pe_identity.hpp"
#include "../vendor/minhook/include/MinHook.h"
#include <array>
#include <cstdio>
#include <cstring>

namespace {
evrm::VoiceObservation observation;
evrm::VoiceEncoderFunction original=nullptr;
unsigned calls=0;
[[gnu::noinline,gnu::noipa]] std::int64_t encoder(void*,evrm::VoiceByteSpan* output,const evrm::VoiceFloatSpan* input){
    ++calls;
    if(output&&output->data&&output->capacity)output->data[0]=input&&input->count==480?42:19;
    return 7;
}
std::int64_t detour(void* state,evrm::VoiceByteSpan* output,const evrm::VoiceFloatSpan* input){
    return evrm::observe_voice_encode(observation,state,output,input,original,true);
}
int fail(const char* text){std::fprintf(stderr,"FAIL %s\n",text);return 1;}
}
int main(int argc,char** argv){
    if(argc==2&&std::strcmp(argv[1],"feature-request")==0){
        if(evrm::local_voice_mouth_snapshot().activity!=evrm::VoiceActivity::Off)return fail("feature must start Off");
        evrm::initialize_local_voice_observer();
        evrm::request_local_voice_feature(true,{});
        evrm::service_local_voice_feature_requests();evrm::service_local_voice_feature_requests();
        const auto deadline=GetTickCount64()+5000;
        while(evrm::local_voice_feature_starting()&&GetTickCount64()<deadline)Sleep(1);
        if(evrm::local_voice_feature_starting())return fail("background source admission did not complete");
        if(evrm::local_voice_mouth_snapshot().activity!=evrm::VoiceActivity::UnsupportedSource)return fail("unsupported source status missing");
        evrm::request_local_voice_feature(false,{});
        if(evrm::local_voice_mouth_snapshot().activity!=evrm::VoiceActivity::Off)return fail("disabled feature retained source error");
        std::puts("PASS deferred feature request and disabled/unsupported status");return 0;
    }
    if(argc==2&&std::strcmp(argv[1],"admission")==0){
        evrm::initialize_local_voice_observer();evrm::initialize_local_voice_observer();
        evrm::poll_local_voice_observer(0);evrm::poll_local_voice_observer(10000);
        std::puts("PASS observer admission returned without altering the process");return 0;
    }
    IMAGE_NT_HEADERS64 disk{};disk.Signature=IMAGE_NT_SIGNATURE;
    disk.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;disk.FileHeader.TimeDateStamp=1792507660;
    disk.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;disk.OptionalHeader.ImageBase=0x140000000ull;
    disk.OptionalHeader.SizeOfImage=44163072;
    constexpr std::uintptr_t actual_base=0x7ff6a0000000ull;
    auto loaded=disk;
    if(!evrm::detail::voice_pe_headers_match(disk,loaded,actual_base))return fail("preferred header base declined");
    loaded.OptionalHeader.ImageBase=actual_base;
    if(!evrm::detail::voice_pe_headers_match(disk,loaded,actual_base))return fail("legitimate relocated header base declined");
    ++loaded.FileHeader.TimeDateStamp;
    if(evrm::detail::voice_pe_headers_match(disk,loaded,actual_base))return fail("relocation hid a timestamp change");
    loaded=disk;loaded.OptionalHeader.ImageBase=actual_base;loaded.OptionalHeader.SizeOfImage+=4096;
    if(evrm::detail::voice_pe_headers_match(disk,loaded,actual_base))return fail("relocation hid an image extent change");
    loaded=disk;loaded.OptionalHeader.ImageBase=actual_base+65536;
    if(evrm::detail::voice_pe_headers_match(disk,loaded,actual_base))return fail("unrelated loaded header base accepted");
    loaded=disk;loaded.OptionalHeader.ImageBase=actual_base;
    const auto unchanged_header=loaded;
    evrm::detail::voice_pe_headers_match(disk,loaded,actual_base);
    if(std::memcmp(&loaded,&unchanged_header,sizeof loaded)!=0)return fail("identity check modified its loaded header input");
    std::array<std::uint8_t,16> state{};const std::uint32_t channels=1;
    std::memcpy(state.data()+8,&channels,4);
    std::array<float,480> samples{};samples.fill(0.5f);
    evrm::VoiceFloatSpan input{samples.data(),samples.size()};
    std::array<std::uint8_t,64> bytes{};evrm::VoiceByteSpan output{bytes.data(),bytes.size()};
    if(encoder(state.data(),&output,&input)!=7||calls!=1)return fail("baseline encoder");
    if(MH_Initialize()!=MH_OK)return fail("hook initialization");
    void* trampoline=nullptr;auto* target=reinterpret_cast<void*>(&encoder);
    if(MH_CreateHook(target,reinterpret_cast<void*>(&detour),&trampoline)!=MH_OK)return fail("owned hook creation");
    original=reinterpret_cast<evrm::VoiceEncoderFunction>(trampoline);
    if(MH_EnableHook(target)!=MH_OK)return fail("owned hook enable");
    if(encoder(state.data(),&output,&input)!=7||bytes[0]!=42||calls!=2)return fail("hooked call changed encoder");
    samples.fill(0);encoder(state.data(),&output,&input);
    const auto result=observation.snapshot();
    if(result.calls!=2||result.accepted!=2||result.nonzero!=1||calls!=3)return fail("actual hook bypassed observation");
    if(MH_DisableHook(target)!=MH_OK||MH_RemoveHook(target)!=MH_OK||MH_Uninitialize()!=MH_OK)return fail("fixture cleanup");
    if(encoder(state.data(),&output,&input)!=7||calls!=4)return fail("restored encoder");
    std::puts("PASS real x64 hook forwards synthetic voice ABI exactly once");
}
