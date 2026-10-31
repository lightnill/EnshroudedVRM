#include "local_voice_observer.hpp"
#include "local_voice_observation.hpp"
#include "runtime_log.hpp"
#include "sha256.hpp"
#include "local_voice_pe_identity.hpp"
#include "voice_worker.hpp"
#include "voice_profile_io.hpp"
#include "voice_setup.hpp"
#include "../vendor/minhook/include/MinHook.h"
#include <cstring>
#include <limits>
#include <memory>
#include <vector>
#include <cmath>

namespace evrm {
namespace {
constexpr std::uint32_t encoder_rva=0x70fe90,call_rva=0xaa1bfc;
struct Handle {
    HANDLE value=INVALID_HANDLE_VALUE;
    ~Handle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
};
struct Observer {
    VoiceObservation observation;
    VoiceEncoderFunction original=nullptr;
    std::uintptr_t borrowed_return=0;
    std::atomic<bool> active{false};
    std::atomic<std::uint64_t> last_poll{0};
    std::atomic<VoiceAnalysisWorker*> worker{nullptr};
    bool diagnostic=false,worker_attempted=false;
};
// Published before enabling the detour. Deliberately process-owned: destroying a
// Vulkan device cannot retire the game's audio thread or its trampoline.
std::atomic<Observer*> observer{nullptr};
std::atomic<bool> requested{false},diagnostic_requested{false},declined{false};
std::atomic<std::uint64_t> requested_tuning{0};
std::once_flag install_once;
std::mutex request_mutex;
std::atomic<VoiceSetup*> setup_dispatcher{nullptr};
std::atomic<bool> setup_needed{false},setup_finished{false};

VoiceAnalysisTuning current_tuning()noexcept {
    const auto bits=requested_tuning.load(std::memory_order_acquire);if(!bits)return {};
    VoiceAnalysisTuning tuning;const auto low=std::uint32_t(bits);std::memcpy(&tuning.sensitivity,&low,4);
    tuning.smoothing_ms=std::uint32_t(bits>>32);return tuning;
}
std::unique_ptr<VoiceAnalysisWorker> make_worker(){
    VoiceProfileLoad profiles{default_voice_profiles(),false,nullptr};wchar_t capture[32768]{};
    const auto length=GetEnvironmentVariableW(L"ENSHROUDED_VRM_CAPTURE_DIR",capture,32768);
    if(length&&length<32768)profiles=load_voice_profiles(std::filesystem::path(capture).parent_path()/L"voice-profiles.json");
    if(profiles.diagnostic)log_event(profiles.diagnostic);
    log_event(profiles.custom?"voice-feature.classifier source=custom-mfcc":"voice-feature.classifier source=Oculus-LipSync-1.61.0 provider=Enhanced software=true");
    auto worker=std::make_unique<VoiceAnalysisWorker>(profiles.custom?&profiles.profiles:nullptr);
    if(!worker->ready()){declined.store(true);log_event_lazy([&]{return std::string("voice-feature.declined reason=worker-initialize detail=")+worker->diagnostic();});return {};}
    return worker;
}

bool consume_request(){
    wchar_t capture[32768]{};
    const auto count=GetEnvironmentVariableW(L"ENSHROUDED_VRM_CAPTURE_DIR",capture,32768);
    if(!count||count>=32768||std::wstring(capture).rfind(L"\\\\",0)==0)return false;
    const auto path=std::filesystem::path(capture)/L"voice-observer.request";
    Handle request{CreateFileW(path.c_str(),DELETE|FILE_READ_ATTRIBUTES,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    if(request.value==INVALID_HANDLE_VALUE)return false;
    BY_HANDLE_FILE_INFORMATION info{};
    if(!GetFileInformationByHandle(request.value,&info)||info.nNumberOfLinks!=1||
        (info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
        info.nFileSizeHigh||info.nFileSizeLow>64)return false;
    FILE_DISPOSITION_INFO disposition{TRUE};
    return SetFileInformationByHandle(request.value,FileDispositionInfo,&disposition,sizeof disposition)!=FALSE;
}

bool image_read(HMODULE module,std::size_t rva,void* output,std::size_t size,bool executable=false){
    const auto base=reinterpret_cast<std::uintptr_t>(module);
    if(rva>std::numeric_limits<std::uintptr_t>::max()-base||
        size>std::numeric_limits<std::uintptr_t>::max()-(base+rva))return false;
    const auto address=base+rva,end=address+size;
    for(auto at=address;at<end;){
        MEMORY_BASIC_INFORMATION memory{};
        if(!VirtualQuery(reinterpret_cast<const void*>(at),&memory,sizeof memory)||
            memory.AllocationBase!=module||memory.State!=MEM_COMMIT||memory.Type!=MEM_IMAGE||
            (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto protection=memory.Protect&0xff;
        const bool readable=protection==PAGE_READONLY||protection==PAGE_READWRITE||protection==PAGE_WRITECOPY||
            protection==PAGE_EXECUTE_READ||protection==PAGE_EXECUTE_READWRITE||protection==PAGE_EXECUTE_WRITECOPY;
        if(!readable||(executable&&protection!=PAGE_EXECUTE_READ&&protection!=PAGE_EXECUTE_READWRITE&&
            protection!=PAGE_EXECUTE_WRITECOPY))return false;
        const auto region_end=reinterpret_cast<std::uintptr_t>(memory.BaseAddress)+memory.RegionSize;
        if(region_end<=at)return false;
        at=std::min(end,region_end);
    }
    SIZE_T copied=0;
    return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(address),output,size,&copied)&&copied==size;
}
template<class T> bool disk_read(const std::vector<std::uint8_t>& bytes,std::size_t offset,T& value){
    if(offset>bytes.size()||sizeof value>bytes.size()-offset)return false;
    std::memcpy(&value,bytes.data()+offset,sizeof value);return true;
}
bool disk_anchor(const std::vector<std::uint8_t>& bytes,std::size_t nt_offset,const IMAGE_NT_HEADERS64& nt,
    std::uint32_t rva,const std::uint8_t* expected,std::size_t size){
    const auto sections=nt_offset+offsetof(IMAGE_NT_HEADERS64,OptionalHeader)+nt.FileHeader.SizeOfOptionalHeader;
    if(nt.FileHeader.NumberOfSections>96)return false;
    for(unsigned n=0;n<nt.FileHeader.NumberOfSections;++n){
        IMAGE_SECTION_HEADER section{};
        if(!disk_read(bytes,sections+n*sizeof section,section))return false;
        if(rva<section.VirtualAddress)continue;
        const auto relative=std::uint64_t(rva)-section.VirtualAddress;
        if(relative>section.SizeOfRawData||size>section.SizeOfRawData-relative)continue;
        const auto offset=std::uint64_t(section.PointerToRawData)+relative;
        return (section.Characteristics&IMAGE_SCN_MEM_EXECUTE)&&offset<=bytes.size()&&size<=bytes.size()-offset&&
            std::memcmp(bytes.data()+offset,expected,size)==0;
    }
    return false;
}
bool source_identity(HMODULE module){
    wchar_t executable[32768]{};
    const auto length=GetModuleFileNameW(module,executable,32768);
    if(!length||length>=32768)return false;
    Handle file{CreateFileW(executable,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr)};
    BY_HANDLE_FILE_INFORMATION file_info{};LARGE_INTEGER size{};
    if(file.value==INVALID_HANDLE_VALUE||!GetFileInformationByHandle(file.value,&file_info)||
        (file_info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))||
        !GetFileSizeEx(file.value,&size)||size.QuadPart<static_cast<LONGLONG>(sizeof(IMAGE_NT_HEADERS64))||size.QuadPart>128*1024*1024)return false;
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(size.QuadPart));
    DWORD read=0;
    if(!ReadFile(file.value,bytes.data(),static_cast<DWORD>(bytes.size()),&read,nullptr)||read!=bytes.size())return false;
    VoiceSourceIdentity identity;identity.file_sha256=detail::sha256(bytes.data(),bytes.size());
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{},disk_nt{};
    if(!image_read(module,0,&dos,sizeof dos)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>1024*1024||
        !image_read(module,dos.e_lfanew,&nt,sizeof nt)||nt.Signature!=IMAGE_NT_SIGNATURE||
        nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||
        nt.FileHeader.SizeOfOptionalHeader!=sizeof(IMAGE_OPTIONAL_HEADER64)||
        !disk_read(bytes,dos.e_lfanew,disk_nt)||!detail::voice_pe_headers_match(disk_nt,nt,reinterpret_cast<std::uintptr_t>(module)))return false;
    identity.timestamp=nt.FileHeader.TimeDateStamp;identity.image_bytes=nt.OptionalHeader.SizeOfImage;
    if(std::uint64_t(encoder_rva)+identity.encoder_prologue.size()>identity.image_bytes||
        std::uint64_t(call_rva)+identity.encoder_call.size()>identity.image_bytes||
        !image_read(module,encoder_rva,identity.encoder_prologue.data(),identity.encoder_prologue.size(),true)||
        !image_read(module,call_rva,identity.encoder_call.data(),identity.encoder_call.size(),true)||
        !local_voice_identity_supported(identity))return false;
    return disk_anchor(bytes,dos.e_lfanew,nt,encoder_rva,identity.encoder_prologue.data(),identity.encoder_prologue.size())&&
        disk_anchor(bytes,dos.e_lfanew,nt,call_rva,identity.encoder_call.data(),identity.encoder_call.size());
}
__attribute__((noinline)) std::int64_t encode_observed(void* encoder,VoiceByteSpan* output,const VoiceFloatSpan* input)noexcept {
    auto* state=observer.load(std::memory_order_acquire);
    // The trampoline and state are published before this function is reachable.
    const VoiceFrameSink sink{state,
        [](void* context,std::uint64_t source,std::uint64_t timestamp)noexcept -> std::uint64_t {
            auto* owner=static_cast<Observer*>(context);auto* worker=owner->worker.load(std::memory_order_acquire);
            return requested.load(std::memory_order_acquire)&&worker?worker->begin_capture(source,timestamp):0;
        },
        [](void* context,const std::array<float,480>& samples,std::uint64_t source,std::uint64_t timestamp,std::uint64_t token)noexcept {
            auto* owner=static_cast<Observer*>(context);auto* worker=owner->worker.load(std::memory_order_acquire);
            if(requested.load(std::memory_order_acquire)&&worker)worker->submit(samples,source,timestamp,token);
        }};
    const auto caller=reinterpret_cast<std::uintptr_t>(__builtin_extract_return_addr(__builtin_return_address(0)));
    const bool known_caller=caller==state->borrowed_return;
    const bool analysis=known_caller&&requested.load(std::memory_order_acquire)&&state->worker.load(std::memory_order_acquire);
    const auto contract=known_caller?VoiceReadContract::BorrowedThroughReturn:VoiceReadContract::UnrecognizedCaller;
    return observe_voice_encode(state->observation,encoder,output,input,state->original,
        state->active.load(std::memory_order_relaxed)&&(state->diagnostic||analysis),analysis?&sink:nullptr,contract);
}
void install(){
    const auto module=GetModuleHandleW(nullptr);
    if(!module||!source_identity(module)){log_event("voice-observer.declined reason=source-identity");return;}
    auto pending=std::make_unique<Observer>();
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&encode_observed),&pinned)){log_event("voice-observer.declined reason=module-lifetime");return;}
    auto worker=requested.load(std::memory_order_acquire)?make_worker():nullptr;
    if(requested.load(std::memory_order_acquire)&&!worker)return;
    const auto initialized=MH_Initialize();
    if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED){log_event("voice-observer.declined reason=hook-initialize");return;}
    auto* target=reinterpret_cast<std::uint8_t*>(module)+encoder_rva;
    void* trampoline=nullptr;
    const auto created=MH_CreateHook(target,reinterpret_cast<void*>(&encode_observed),&trampoline);
    if(created!=MH_OK||!trampoline){log_event("voice-observer.declined reason=hook-create");return;}
    pending->original=reinterpret_cast<VoiceEncoderFunction>(trampoline);
    pending->borrowed_return=reinterpret_cast<std::uintptr_t>(module)+call_rva+5;
    pending->diagnostic=diagnostic_requested.load(std::memory_order_acquire);
    pending->worker_attempted=bool(worker);
    pending->worker.store(worker.release(),std::memory_order_release);
    auto* state=pending.release();observer.store(state,std::memory_order_release);
    // Even a failed enable keeps the original trampoline and module alive. Never
    // tear down a potentially reachable callback or uninitialize other targets.
    if(MH_EnableHook(target)!=MH_OK){log_event("voice-observer.declined reason=hook-enable; relay-retained");return;}
    state->active.store(true,std::memory_order_release);
    declined.store(false,std::memory_order_release);
    {std::lock_guard<std::mutex> guard(request_mutex);
        if(auto* active_worker=state->worker.load(std::memory_order_acquire)){
            active_worker->set_tuning(current_tuning());active_worker->set_enabled(requested.load(std::memory_order_acquire));}}
    log_event("voice-observer.ready format=48000hz-mono-f32 block=480 source=game-outgoing; samples-not-recorded");
}
void ensure_installed()noexcept {
    try{std::call_once(install_once,[]{declined.store(true);try{install();}catch(...){log_event("voice-observer.declined reason=initialization");}});}catch(...){declined.store(true);}
}
void perform_setup(void*)noexcept {
    try{
        if(requested.load(std::memory_order_acquire)){
            ensure_installed();auto* state=observer.load(std::memory_order_acquire);
            if(state&&state->active.load(std::memory_order_acquire)){
                auto* worker=state->worker.load(std::memory_order_acquire);
                if(!worker&&!state->worker_attempted){
                    state->worker_attempted=true;auto pending=make_worker();
                    if(pending){worker=pending.release();state->worker.store(worker,std::memory_order_release);}
                }
                std::lock_guard<std::mutex> guard(request_mutex);
                if(worker){worker->set_tuning(current_tuning());worker->set_enabled(requested.load(std::memory_order_acquire));}
            }
        }
    }catch(...){declined.store(true);log_event("voice-feature.declined reason=initialization");}
    setup_finished.store(true,std::memory_order_release);
}
}
void initialize_local_voice_observer() noexcept {
    static std::once_flag once;
    try{std::call_once(once,[]{try{
        HMODULE pinned=nullptr;
        if(GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&perform_setup),&pinned)){
            auto pending=std::make_unique<VoiceSetup>(&perform_setup,nullptr);
            if(pending->ready())setup_dispatcher.store(pending.release(),std::memory_order_release);
        }
        if(consume_request()){diagnostic_requested.store(true);ensure_installed();}}
        catch(...){log_event("voice-observer.declined reason=initialization");}});}catch(...){}
}
void request_local_voice_feature(bool enabled,VoiceAnalysisTuning tuning)noexcept {
    std::lock_guard<std::mutex> guard(request_mutex);
    tuning.sensitivity=std::isfinite(tuning.sensitivity)?std::clamp(tuning.sensitivity,0.f,1.f):.5f;
    tuning.smoothing_ms=std::clamp(tuning.smoothing_ms,20u,140u);std::uint32_t bits=0;std::memcpy(&bits,&tuning.sensitivity,4);
    requested_tuning.store((std::uint64_t(tuning.smoothing_ms)<<32)|bits,std::memory_order_release);
    requested.store(enabled,std::memory_order_release);
    if(auto* state=observer.load(std::memory_order_acquire))if(auto* worker=state->worker.load(std::memory_order_acquire)){
        worker->set_tuning(tuning);worker->set_enabled(enabled&&state->active.load(std::memory_order_acquire));}
    if(enabled){setup_needed.store(true,std::memory_order_release);service_local_voice_feature_requests();}
}
void service_local_voice_feature_requests()noexcept {
    if(!requested.load(std::memory_order_acquire)||!setup_needed.load(std::memory_order_acquire))return;
    auto* setup=setup_dispatcher.load(std::memory_order_acquire);
    if(!setup){declined.store(true);return;}
    if(setup_needed.exchange(false,std::memory_order_acq_rel))setup->request();
}
bool local_voice_feature_starting()noexcept {return requested.load(std::memory_order_acquire)&&!setup_finished.load(std::memory_order_acquire)&&setup_dispatcher.load(std::memory_order_acquire);}
MouthSnapshot local_voice_mouth_snapshot()noexcept {
    if(!requested.load(std::memory_order_acquire))return {};
    MouthSnapshot result;result.activity=declined.load(std::memory_order_acquire)?VoiceActivity::UnsupportedSource:VoiceActivity::Waiting;
    auto* state=observer.load(std::memory_order_acquire);
    if(state&&state->active.load(std::memory_order_acquire))if(auto* worker=state->worker.load(std::memory_order_acquire))return worker->snapshot();
    return result;
}
void poll_local_voice_observer(std::uint64_t now_ms) noexcept {
    auto* state=observer.load(std::memory_order_acquire);
    if(!state||!state->active.load(std::memory_order_acquire)||(!state->diagnostic&&!requested.load(std::memory_order_acquire)))return;
    auto previous=state->last_poll.load(std::memory_order_relaxed);
    if(now_ms<previous||now_ms-previous<1000||
        !state->last_poll.compare_exchange_strong(previous,now_ms,std::memory_order_relaxed))return;
    const auto counts=state->observation.snapshot();
    log_event_lazy([&]{return "voice-observer.counts calls="+std::to_string(counts.calls)+" accepted="+std::to_string(counts.accepted)+
        " nonzero="+std::to_string(counts.nonzero)+" rejected="+std::to_string(counts.rejected)+" borrowed-calls="+std::to_string(counts.borrowed_calls)+
        " encode-failures="+std::to_string(counts.encode_failures)+" inspection-qpc-ns="+std::to_string(counts.inspection_nanoseconds)+
        " profiled-calls="+std::to_string(counts.profiled_calls)+" sampled-qpc-ns="+std::to_string(counts.sampled_qpc_nanoseconds)+
        " sampled-thread-cycles="+std::to_string(counts.sampled_thread_cycles)+
        " sampled-validation-ns="+std::to_string(counts.sampled_validation_ns)+" sampled-copy-ns="+std::to_string(counts.sampled_copy_ns)+
        " sampled-queries="+std::to_string(counts.sampled_queries)+" sampled-copies="+std::to_string(counts.sampled_copies)+
        " sampled-pre-ns="+std::to_string(counts.sampled_pre_ns)+" sampled-post-ns="+std::to_string(counts.sampled_post_ns);});
    if(auto* worker=state->worker.load(std::memory_order_acquire))log_event_lazy([&]{
        const auto stats=worker->stats();const auto mouth=worker->snapshot();const auto& levels=stats.diagnostics;
        std::string weights;for(float value:mouth.weights){if(!weights.empty())weights+=',';weights+=std::to_string(value);}
        return "voice-analysis.counts submitted="+std::to_string(stats.submitted)+" processed="+std::to_string(stats.processed)+
            " discarded="+std::to_string(stats.discarded)+" dropped="+std::to_string(stats.dropped)+" resets="+std::to_string(stats.resets)+
            " epoch="+std::to_string(stats.epoch)+" analysis-ns="+std::to_string(stats.analysis_nanoseconds)+" levels-valid="+std::to_string(levels.valid)+
            " rms-db="+std::to_string(levels.rms_db)+" flatness="+std::to_string(levels.flatness)+" activation="+std::to_string(levels.activation)+
            " activity="+std::to_string(unsigned(mouth.activity))+" weights="+weights;});
}
}
