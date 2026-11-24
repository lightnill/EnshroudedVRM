#include "native_mtoon_probe_program_cache.hpp"
#include <array>
#include <chrono>
#include <future>
#include <stdexcept>
namespace evrm {
struct NativeMToonProbeProgramCache::Impl {
    VkDevice device{};PFN_vkGetDeviceProcAddr get{};NativeMToonProbeCopyCapabilities caps;
    std::shared_ptr<const std::vector<std::uint32_t>> words;
    struct Job {
        bool started=false,failed=false;
        std::shared_ptr<NativeMToonProbeProgram> program;
        std::future<std::shared_ptr<NativeMToonProbeProgram>> pending;
    };
    std::array<Job,4> jobs;
};
NativeMToonProbeProgramCache::NativeMToonProbeProgramCache(VkDevice device,PFN_vkGetDeviceProcAddr get,
    const NativeMToonProbeCopyCapabilities& caps,std::vector<std::uint32_t> words){
    if(!device||!get||words.size()<5||words.size()>1024*1024||words[0]!=0x07230203)throw std::runtime_error("Probe preparation cache input");
    impl=std::make_unique<Impl>();impl->device=device;impl->get=get;impl->caps=caps;
    impl->words=std::make_shared<const std::vector<std::uint32_t>>(std::move(words));
}
NativeMToonProbeProgramCache::~NativeMToonProbeProgramCache()=default;
NativeMToonProbePreparedProgram NativeMToonProbeProgramCache::request(const NativeMToonProbeProgramShape& shape)noexcept{
    const auto key=native_mtoon_probe_program_shape_key(shape);if(!impl||key>=impl->jobs.size())return {};
    auto& job=impl->jobs[key];
    try{
        if(!job.started){
            job.started=true;const auto device=impl->device;const auto get=impl->get;const auto caps=impl->caps;const auto words=impl->words;
            job.pending=std::async(std::launch::async,[device,get,caps,words,shape]()->std::shared_ptr<NativeMToonProbeProgram>{
                try{return std::make_shared<NativeMToonProbeProgram>(device,get,shape,caps,words->data(),words->size());}catch(...){return {};}
            });
        }
        if(job.pending.valid()&&job.pending.wait_for(std::chrono::seconds(0))==std::future_status::ready){
            job.program=job.pending.get();job.failed=!job.program;
        }
        if(job.program&&job.program->matches(shape))return {NativeMToonProbeProgramStatus::ready,job.program};
        return {job.failed?NativeMToonProbeProgramStatus::failed:NativeMToonProbeProgramStatus::pending,{}};
    }catch(...){job.failed=true;return {NativeMToonProbeProgramStatus::failed,{}};}
}
} // namespace evrm
