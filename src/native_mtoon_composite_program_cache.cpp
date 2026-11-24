#include "native_mtoon_composite_program_cache.hpp"
#include <chrono>
#include <future>
#include <stdexcept>
namespace evrm {
namespace {
bool composite_formats_supported(const NativeMToonCompositeCapabilities& c)noexcept{
    if(!c.storage_image_extended_formats)return false;
    for(auto f:c.format_features)if(!(f&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT))return false;
    return true;
}
}
struct NativeMToonCompositeProgramCache::Impl {
    VkDevice device{};PFN_vkGetDeviceProcAddr get{};NativeMToonCompositeCapabilities caps{};
    const bool* parent=nullptr;bool started=false,failed=false,stopped=false,uncertain=false;
    std::shared_ptr<const std::vector<std::uint32_t>> words;
    std::shared_ptr<const NativeMToonCompositeProgram> program;
    std::future<std::shared_ptr<const NativeMToonCompositeProgram>> pending;
    void receive()noexcept{try{if(pending.valid()){program=pending.get();failed=!program;}}catch(...){failed=true;}
        if(uncertain&&program)program->quarantine();}
};
NativeMToonCompositeProgramCache::NativeMToonCompositeProgramCache(VkDevice device,PFN_vkGetDeviceProcAddr get,
    const NativeMToonCompositeCapabilities& caps,std::vector<std::uint32_t> words,const bool* parent){
    if(!device||!get||!parent||words.size()<5||words.size()>262144||words[0]!=0x07230203)
        throw std::runtime_error("MToon composite preparation inputs");
    impl=std::make_unique<Impl>();impl->device=device;impl->get=get;impl->caps=caps;impl->parent=parent;
    impl->words=std::make_shared<const std::vector<std::uint32_t>>(std::move(words));
}
NativeMToonCompositeProgramCache::~NativeMToonCompositeProgramCache(){shutdown(impl&&impl->parent&&*impl->parent);}
NativeMToonCompositePreparedProgram NativeMToonCompositeProgramCache::request()noexcept{
    if(!impl||impl->stopped||!composite_formats_supported(impl->caps))return {};
    auto& p=*impl;
    if(*p.parent){p.uncertain=true;if(p.program)p.program->quarantine();return {};}
    if(p.uncertain)return {};
    try{
        if(!p.started){p.started=true;const auto device=p.device;const auto get=p.get;const auto caps=p.caps;
            const auto words=p.words;const auto parent=p.parent;
            p.pending=std::async(std::launch::async,[device,get,caps,words,parent]()->std::shared_ptr<const NativeMToonCompositeProgram>{
                try{return std::make_shared<NativeMToonCompositeProgram>(device,get,caps,
                    NativeMToonCompositeShader{words->data(),words->size()},parent);}catch(...){return {};}
            });
        }
        if(p.pending.valid()&&p.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready)p.receive();
        if(p.program){if(!p.program->reusable()){p.failed=true;return {NativeMToonCompositeProgramStatus::failed,{}};}return {NativeMToonCompositeProgramStatus::ready,p.program};}
        return {p.failed?NativeMToonCompositeProgramStatus::failed:NativeMToonCompositeProgramStatus::pending,{}};
    }catch(...){p.failed=true;return {NativeMToonCompositeProgramStatus::failed,{}};}
}
void NativeMToonCompositeProgramCache::shutdown(bool quarantine)noexcept{
    if(!impl)return;auto& p=*impl;p.stopped=true;p.uncertain=p.uncertain||quarantine;
    p.receive();if(p.uncertain&&p.program)p.program->quarantine();
}
}
