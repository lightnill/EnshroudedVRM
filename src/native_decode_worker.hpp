#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <array>
#include <mutex>
#include <string_view>

namespace evrm {
// Environment overrides the optional state file; otherwise new installs use
// the established worker. Explicit inline/off selections retain the old path.
inline bool native_decode_worker_enabled(std::wstring_view environment,std::string_view file)noexcept {
    if(environment==L"0")return false;
    if(environment==L"1")return true;
    while(!file.empty()&&(file.back()=='\r'||file.back()=='\n'||file.back()==' '))file.remove_suffix(1);
    return file!="inline"&&file!="off";
}
// Three existing staging slots are the entire queue. Tasks own their slot
// lease. Ready callbacks may query owned fences; no task submits Vulkan work
// or publishes rendering authority. Unfinished tasks rotate without blocking.
class NativeDecodeWorker {
public:
    struct Task {
        void (*run)(void*)noexcept=nullptr;void* context=nullptr;
        bool (*ready)(void*)noexcept=nullptr;
        void (*cancel)(void*)noexcept=nullptr;
    };
    NativeDecodeWorker()noexcept {
        event_=CreateEventW(nullptr,FALSE,FALSE,nullptr);
        if(event_)thread_=CreateThread(nullptr,0,[](void* raw)->DWORD{static_cast<NativeDecodeWorker*>(raw)->run();return 0;},this,0,nullptr);
        if(!thread_&&event_){CloseHandle(event_);event_=nullptr;}
    }
    ~NativeDecodeWorker(){
        if(thread_){
            {std::lock_guard<std::mutex> lock(mutex_);stopping_=true;}
            SetEvent(event_);WaitForSingleObject(thread_,INFINITE);CloseHandle(thread_);
        }
        if(event_)CloseHandle(event_);
    }
    NativeDecodeWorker(const NativeDecodeWorker&)=delete;
    NativeDecodeWorker& operator=(const NativeDecodeWorker&)=delete;
    bool ready()const noexcept{return thread_!=nullptr;}
    bool submit(Task task)noexcept {
        if(!ready()||!task.run||!task.context)return false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if(stopping_||leased_==tasks_.size())return false;
            tasks_[(first_+count_)%tasks_.size()]=task;++count_;++leased_;
        }
        SetEvent(event_);return true;
    }
private:
    void run()noexcept {
        SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);
        for(;;){
            DWORD timeout=INFINITE;
            {std::lock_guard<std::mutex> lock(mutex_);if(stopping_&&!count_)return;if(count_)timeout=1;}
            const auto wake=WaitForSingleObject(event_,timeout);
            if(wake!=WAIT_OBJECT_0&&wake!=WAIT_TIMEOUT)return;
            std::size_t sweep=0;
            {std::lock_guard<std::mutex> lock(mutex_);sweep=count_;}
            for(std::size_t n=0;n<sweep;++n){
                Task task;bool cancel=false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    if(!count_)break;
                    task=tasks_[first_];first_=(first_+1)%tasks_.size();--count_;
                    cancel=stopping_;
                }
                if(cancel){if(task.cancel)task.cancel(task.context);finish();continue;}
                // Never acquire a slot/lifetime lock while holding this queue
                // mutex: submitters hold that slot lock before enqueueing.
                if(!task.ready||task.ready(task.context)){task.run(task.context);finish();continue;}
                {
                    std::lock_guard<std::mutex> lock(mutex_);cancel=stopping_;
                    if(!cancel){tasks_[(first_+count_)%tasks_.size()]=task;++count_;}
                }
                if(cancel){if(task.cancel)task.cancel(task.context);finish();}
            }
        }
    }
    void finish()noexcept{std::lock_guard<std::mutex> lock(mutex_);--leased_;}
    std::mutex mutex_;
    std::array<Task,3> tasks_{};
    std::size_t first_=0,count_=0,leased_=0;
    bool stopping_=false;
    HANDLE event_=nullptr,thread_=nullptr;
};
}
