#include "native_preview_handoff_calls.hpp"
#include "native_preview_subject_calls.hpp"
#include "native_relative_call.hpp"
#include "native_owner_process.hpp"
#include "runtime_log.hpp"
#include "../vendor/minhook/include/MinHook.h"
#include <memory>
#ifdef _MSC_VER
#include <intrin.h>
#endif

namespace evrm {namespace {
using U=std::uint64_t;
using Request=void(*)(U,U,U);
using Render=void(*)(U,U,U,U,U);
using Execute=void(*)(U,U,U,U);
using Allocate=U(*)(U,U,U);
using Copy=U(*)(U,U,U);
using MainCreate=U(*)(U);
using MainDestroy=U(*)(U,U);
struct Observer {
    NativeOwnerProcess memory;
    Request request=nullptr;Render render=nullptr;Execute execute=nullptr;
    Allocate allocate=nullptr;Copy copy=nullptr;NativeRelativeCall copy_call,publication_call;
    MainCreate main_create=nullptr;MainDestroy main_destroy=nullptr;
    std::atomic<bool> collecting{false};
};
// Relays and their trampolines are process-owned. Device retirement only clears
// the diagnostic source; it cannot retire an original game/worker call stack.
std::atomic<Observer*> observer{nullptr};
std::atomic<unsigned> setup{0};
bool copy(void* context,U pointer,void* output,std::size_t count)noexcept {
    return static_cast<Observer*>(context)->memory.read(pointer,output,count);
}
NativePreviewHandoffCalls calls(Observer& state)noexcept {
    return {native_preview_handoff_history(),{&state,&copy},
        native_preview_handoff_thread(),state.memory.image().base};
}
NativePreviewSubjectCalls subject_calls(Observer& state)noexcept {
    return {native_preview_subjects(),{&state,&copy},native_preview_subject_thread(),state.memory.image().base};
}
bool collecting(const Observer& state)noexcept {
    return state.collecting.load(std::memory_order_acquire)&&(native_preview_handoff_history().active()||native_preview_subjects().active());
}
#ifdef _MSC_VER
#define EVRM_PREVIEW_CALLER reinterpret_cast<std::uintptr_t>(_ReturnAddress())
#else
#define EVRM_PREVIEW_CALLER reinterpret_cast<std::uintptr_t>(__builtin_extract_return_addr(__builtin_return_address(0)))
#endif
void request_observed(U app,U payload,U color){
    auto& state=*observer.load(std::memory_order_acquire);
    if(!collecting(state)){state.request(app,payload,color);return;}
    const auto caller=EVRM_PREVIEW_CALLER;
    const auto forward=[&](U a,U p,U c){calls(state).request(state.request,a,p,c,caller,GetTickCount64());};
    subject_calls(state).request(forward,app,payload,color,caller);
}
void render_observed(U renderer,U view,U frame,U target,U payload){
    auto& state=*observer.load(std::memory_order_acquire);
    if(!collecting(state)){state.render(renderer,view,frame,target,payload);return;}
    const auto caller=EVRM_PREVIEW_CALLER;
    const auto forward=[&](U r,U v,U f,U t,U p){calls(state).render(state.render,r,v,f,t,p,caller,GetTickCount64());};
    subject_calls(state).render(forward,renderer,view,frame,target,payload,caller);
}
void execute_observed(U backend,U command,U task,U hints){
    auto& state=*observer.load(std::memory_order_acquire);
    if(!collecting(state)){state.execute(backend,command,task,hints);return;}
    const auto forward=[&](U b,U c,U t,U h){calls(state).execute(state.execute,b,c,t,h,GetTickCount64());};
    subject_calls(state).execute(forward,backend,command,task,hints);
}
U allocate_observed(U context,U flags,U label){
    auto& state=*observer.load(std::memory_order_acquire);
    if(!collecting(state))return state.allocate(context,flags,label);
    const auto rendering=native_preview_handoff_thread().rendering;
    const auto task=subject_calls(state).allocate(state.allocate,context,flags,label);
    native_preview_handoff_history().enqueue(task,rendering,GetTickCount64());return task;
}
U copy_observed(U destination,U source,U count){
    auto& state=*observer.load(std::memory_order_acquire);
    if(!collecting(state))return state.copy(destination,source,count);
    return subject_calls(state).copy(state.copy,destination,source,count);
}
U publication_observed(U destination,U source,U count){
    auto& state=*observer.load(std::memory_order_acquire);
    if(!collecting(state))return state.copy(destination,source,count);
    return subject_calls(state).accepted_copy(state.copy,destination,source,count);
}
U main_create_observed(U main){
    auto& state=*observer.load(std::memory_order_acquire);
    return subject_calls(state).main_create(state.main_create,main);
}
U main_destroy_observed(U main,U flags){
    auto& state=*observer.load(std::memory_order_acquire);
    return subject_calls(state).main_destroy(state.main_destroy,main,flags);
}
#undef EVRM_PREVIEW_CALLER
struct Hook {U rva;std::array<unsigned char,32> bytes;void* relay;};
const std::array<Hook,6> hooks{{
    {0xd00780,{0x48,0x89,0x5c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x60,0x0f,0x29,0x74,0x24,0x50,0x49,0x8b,0xd8,0x0f,0x57,0xf6},reinterpret_cast<void*>(&request_observed)},
    {0x243020,{0x48,0x8b,0xc4,0x4c,0x89,0x48,0x20,0x4c,0x89,0x40,0x18,0x48,0x89,0x50,0x10,0x55,0x41,0x56,0x41,0x57,0x48,0x8d,0xa8,0x88,0xf9,0xff,0xff,0x48,0x81,0xec,0x60,0x07},reinterpret_cast<void*>(&render_observed)},
    {0x10068f0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x74,0x24,0x10,0x48,0x89,0x7c,0x24,0x18,0x55,0x48,0x8d,0x6c,0x24,0xe0,0x48,0x81,0xec,0x20,0x01,0x00,0x00,0x8b,0x45,0x1c,0x0f},reinterpret_cast<void*>(&execute_observed)},
    {0xfd39c0,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x20,0x48,0x8d,0xa9,0x10,0x07,0x00},reinterpret_cast<void*>(&allocate_observed)},
    {0x837a50,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x57,0x48,0x83,0xec,0x20,0x33,0xed,0x33,0xc0,0x48,0x89,0x29,0x48,0x8b,0xf1,0x48,0x89},reinterpret_cast<void*>(&main_create_observed)},
    {0x83e100,{0x48,0x89,0x5c,0x24,0x08,0x48,0x89,0x6c,0x24,0x10,0x48,0x89,0x74,0x24,0x18,0x48,0x89,0x7c,0x24,0x20,0x41,0x56,0x48,0x83,0xec,0x20,0x8b,0xea,0x48,0x8b,0xf9,0x33},reinterpret_cast<void*>(&main_destroy_observed)}
}};
DWORD WINAPI install(void*)noexcept {
    try{
        auto pending=std::make_unique<Observer>();
        if(!pending->memory.supported()){setup.store(3,std::memory_order_release);log_event("preview-handoff.declined reason=unsupported-main");return 0;}
        const auto base=pending->memory.image().base;
        for(const auto& hook:hooks){std::array<unsigned char,32> bytes{};
            if(!pending->memory.read(base+hook.rva,bytes.data(),bytes.size())||bytes!=hook.bytes){
                setup.store(3,std::memory_order_release);log_event("preview-handoff.declined reason=original-code-mismatch");return 0;}
        }
        const std::array<unsigned char,5> copy_before{0xe8,0x76,0x89,0xb6,0x00};std::array<unsigned char,5> copy_actual{};
        if(!pending->memory.read(base+0x861c55,copy_actual.data(),copy_actual.size())||copy_actual!=copy_before){
            setup.store(3,std::memory_order_release);log_event("preview-handoff.declined reason=original-copy-mismatch");return 0;}
        const std::array<unsigned char,5> publication_before{0xe8,0xb7,0x9d,0x6c,0x00};
        if(!pending->memory.read(base+0xd00814,copy_actual.data(),copy_actual.size())||copy_actual!=publication_before){
            setup.store(3,std::memory_order_release);log_event("preview-handoff.declined reason=original-publication-mismatch");return 0;}
        const auto initialized=MH_Initialize();
        if(initialized!=MH_OK&&initialized!=MH_ERROR_ALREADY_INITIALIZED){setup.store(4,std::memory_order_release);return 0;}
        std::array<void*,6> targets{},originals{};std::size_t created=0;
        for(std::size_t i=0;i<hooks.size();++i){targets[i]=reinterpret_cast<void*>(static_cast<std::uintptr_t>(base+hooks[i].rva));
            const auto status=MH_CreateHook(targets[i],hooks[i].relay,&originals[i]);
            if(status==MH_OK)++created;
            if(status!=MH_OK||!originals[i]){
                while(created)MH_RemoveHook(targets[--created]);
                setup.store(4,std::memory_order_release);log_event("preview-handoff.declined reason=hook-create");return 0;}
        }
        pending->request=reinterpret_cast<Request>(originals[0]);pending->render=reinterpret_cast<Render>(originals[1]);
        pending->execute=reinterpret_cast<Execute>(originals[2]);pending->allocate=reinterpret_cast<Allocate>(originals[3]);
        pending->copy=reinterpret_cast<Copy>(static_cast<std::uintptr_t>(base+0x13ca5d0));
        pending->main_create=reinterpret_cast<MainCreate>(originals[4]);pending->main_destroy=reinterpret_cast<MainDestroy>(originals[5]);
        auto* retained=pending.release();observer.store(retained,std::memory_order_release);
        // Every relay can already forward before any enable can take effect.
        // A failed/partial enable retains inactive relays and their originals.
        for(auto target:targets)if(MH_QueueEnableHook(target)!=MH_OK){setup.store(4,std::memory_order_release);log_event("preview-handoff.declined reason=hook-queue; relays-retained");return 0;}
        if(MH_ApplyQueued()!=MH_OK){setup.store(4,std::memory_order_release);log_event("preview-handoff.declined reason=hook-enable; relays-retained");return 0;}
        if(!retained->copy_call.install(base+0x861c55,copy_before,base+0x13ca5d0,reinterpret_cast<std::uintptr_t>(&copy_observed))){
            setup.store(4,std::memory_order_release);log_event("preview-handoff.declined reason=copy-call; relays-retained");return 0;}
        if(!retained->publication_call.install(base+0xd00814,publication_before,base+0x13ca5d0,reinterpret_cast<std::uintptr_t>(&publication_observed))){
            setup.store(4,std::memory_order_release);log_event("preview-handoff.declined reason=publication-call; relays-retained");return 0;}
        retained->collecting.store(true,std::memory_order_release);setup.store(2,std::memory_order_release);
        log_event("preview-handoff.ready local-subject-copy-and-task-observation");
    }catch(...){setup.store(4,std::memory_order_release);log_event("preview-handoff.declined reason=setup-failure");}
    return 0;
}
} // namespace
void initialize_native_preview_handoff()noexcept {
    unsigned idle=0;if(!setup.compare_exchange_strong(idle,1,std::memory_order_acq_rel))return;
    HMODULE pinned=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<LPCWSTR>(&initialize_native_preview_handoff),&pinned)){setup.store(4,std::memory_order_release);return;}
    const auto thread=CreateThread(nullptr,0,&install,nullptr,0,nullptr);
    if(thread)CloseHandle(thread);else setup.store(4,std::memory_order_release);
}
unsigned native_preview_handoff_setup_status()noexcept{return setup.load(std::memory_order_acquire);}
} // namespace evrm
