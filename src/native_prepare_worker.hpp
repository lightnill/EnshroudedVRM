#pragma once
#include <vulkan/vulkan.h>
#include <array>
#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <mutex>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace evrm {
// One device, three immutable lazy camera-preparation jobs. Render-side calls
// only try the short state lock; the driver is always called without that lock.
// The owner must shut down before destroying any borrowed device/module/layout.
class NativePrepareWorker {
public:
    enum class Key : unsigned { Menu,SurfaceWorld,SurfaceMenu };
    struct Completion {
        bool complete=false;
        VkResult result=VK_NOT_READY;
        VkPipeline pipeline{};
        double milliseconds=0;
    };
    NativePrepareWorker(VkDevice device,PFN_vkCreateComputePipelines create,PFN_vkDestroyPipeline destroy)
        :device_(device),create_(create),destroy_(destroy),thread_([this]{run();}){}
    ~NativePrepareWorker(){shutdown(false);}
    NativePrepareWorker(const NativePrepareWorker&)=delete;
    NativePrepareWorker& operator=(const NativePrepareWorker&)=delete;

    bool request(Key key,VkPipelineLayout layout,VkShaderModule module)noexcept {
        const auto index=static_cast<unsigned>(key);
        if(index>=jobs_.size()||!device_||!create_||!destroy_||!layout||!module)return false;
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()||stopping_)return false;
        auto& job=jobs_[index];
        if(job.state!=State::Empty)return job.layout==layout&&job.module==module;
        job.layout=layout;job.module=module;job.state=State::Queued;
        changed_.notify_one();return true;
    }
    Completion poll(Key key,VkPipelineLayout layout,VkShaderModule module)noexcept {
        const auto index=static_cast<unsigned>(key);if(index>=jobs_.size())return {};
        std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()||stopping_)return {};
        auto& job=jobs_[index];
        if(job.state!=State::Complete||job.layout!=layout||job.module!=module)return {};
        Completion out{true,job.result,VK_NULL_HANDLE,job.milliseconds};
        if(job.result==VK_SUCCESS){out.pipeline=job.pipeline;job.pipeline=VK_NULL_HANDLE;}
        job.state=State::Consumed;return out;
    }
    // Teardown may wait. Queued jobs are cancelled; an in-progress downstream
    // call must finish naturally. Failed partial and unadopted handles stay here.
    // Quarantine deliberately retains them, matching the backend's device policy.
    void shutdown(bool quarantine)noexcept {
        {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}
        changed_.notify_one();if(thread_.joinable())thread_.join();
        for(auto& job:jobs_)if(job.pipeline){if(!quarantine)destroy_(device_,job.pipeline,nullptr);job.pipeline=VK_NULL_HANDLE;}
    }
private:
    enum class State { Empty,Queued,Compiling,Complete,Consumed };
    struct Job {
        State state=State::Empty;
        VkPipelineLayout layout{};VkShaderModule module{};VkPipeline pipeline{};
        VkResult result=VK_NOT_READY;double milliseconds=0;
    };
    const VkDevice device_;
    const PFN_vkCreateComputePipelines create_;
    const PFN_vkDestroyPipeline destroy_;
    std::array<Job,3> jobs_{};
    std::mutex mutex_;std::condition_variable changed_;bool stopping_=false;
    std::thread thread_;
    void run()noexcept {
#ifdef _WIN32
        // Avoid competing at the render thread's priority during driver work.
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
#endif
        std::unique_lock<std::mutex> lock(mutex_);
        for(;;){
            changed_.wait(lock,[&]{if(stopping_)return true;for(const auto& job:jobs_)if(job.state==State::Queued)return true;return false;});
            if(stopping_)return;
            Job* next=nullptr;for(auto& job:jobs_)if(job.state==State::Queued){next=&job;break;}
            auto& job=*next;job.state=State::Compiling;
            VkComputePipelineCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;info.layout=job.layout;
            info.stage.sType=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;info.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;
            info.stage.module=job.module;info.stage.pName="cs_prepare";
            lock.unlock();VkPipeline pipeline{};const auto start=std::chrono::steady_clock::now();
            const auto result=create_(device_,VK_NULL_HANDLE,1,&info,nullptr,&pipeline);
            const auto elapsed=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
            lock.lock();job.pipeline=pipeline;job.result=result==VK_SUCCESS&&!pipeline?VK_ERROR_INITIALIZATION_FAILED:result;
            job.milliseconds=elapsed;job.state=State::Complete;
        }
    }
};
} // namespace evrm
