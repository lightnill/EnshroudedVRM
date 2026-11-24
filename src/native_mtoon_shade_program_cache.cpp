#include "native_mtoon_shade_program_cache.hpp"
#include <chrono>
#include <future>
#include <stdexcept>
namespace evrm {
struct NativeMToonShadeProgramCache::Impl {
    VkDevice device{};PFN_vkGetDeviceProcAddr get{};NativeMToonShadeCapabilities caps{};
    const bool* parent=nullptr;bool started=false,failed=false,stopped=false,uncertain=false;
    std::shared_ptr<const std::vector<std::uint32_t>> words;
    std::shared_ptr<const NativeMToonShadeProgram> program;
    std::future<std::shared_ptr<const NativeMToonShadeProgram>> pending;
    void receive()noexcept{try{if(pending.valid()){program=pending.get();failed=!program;}}catch(...){failed=true;}
        if(uncertain&&program)program->quarantine();}
};
NativeMToonShadeProgramCache::NativeMToonShadeProgramCache(VkDevice device,PFN_vkGetDeviceProcAddr get,
    const NativeMToonShadeCapabilities& caps,std::vector<std::uint32_t> words,const bool* parent){
    if(!device||!get||!parent||words.size()<5||words.size()>262144||words[0]!=0x07230203)
        throw std::runtime_error("MToon shade preparation inputs");
    impl=std::make_unique<Impl>();impl->device=device;impl->get=get;impl->caps=caps;impl->parent=parent;
    impl->words=std::make_shared<const std::vector<std::uint32_t>>(std::move(words));
}
NativeMToonShadeProgramCache::~NativeMToonShadeProgramCache(){shutdown(impl&&impl->parent&&*impl->parent);}
NativeMToonShadePreparedProgram NativeMToonShadeProgramCache::request()noexcept{
    if(!impl||impl->stopped||!impl->caps.descriptor_binding_partially_bound_enabled)return {};
    auto& p=*impl;
    if(*p.parent){p.uncertain=true;if(p.program)p.program->quarantine();return {};}
    if(p.uncertain)return {};
    try{
        if(!p.started){p.started=true;const auto device=p.device;const auto get=p.get;const auto caps=p.caps;
            const auto words=p.words;const auto parent=p.parent;
            p.pending=std::async(std::launch::async,[device,get,caps,words,parent]()->std::shared_ptr<const NativeMToonShadeProgram>{
                try{return std::make_shared<NativeMToonShadeProgram>(device,get,caps,
                    NativeMToonShadeShader{words->data(),words->size()},parent);}catch(...){return {};}
            });
        }
        if(p.pending.valid()&&p.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready)p.receive();
        if(p.program){if(!p.program->reusable()){p.failed=true;return {NativeMToonShadeProgramStatus::failed,{}};}return {NativeMToonShadeProgramStatus::ready,p.program};}
        return {p.failed?NativeMToonShadeProgramStatus::failed:NativeMToonShadeProgramStatus::pending,{}};
    }catch(...){p.failed=true;return {NativeMToonShadeProgramStatus::failed,{}};}
}
void NativeMToonShadeProgramCache::shutdown(bool quarantine)noexcept{
    if(!impl)return;auto& p=*impl;p.stopped=true;p.uncertain=p.uncertain||quarantine;
    p.receive();if(p.uncertain&&p.program)p.program->quarantine();
}
}
