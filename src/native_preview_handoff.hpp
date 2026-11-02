#pragma once
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
namespace evrm {
// Diagnostic correlation only; these values never authorize replacement.
struct NativePreviewHandoffSource {std::uint64_t epoch=0,client=0,main=0,renderer=0,buffer=0,owner=0;};
struct NativePreviewHandoffLabel {
    std::uint64_t epoch=0,request=0;unsigned origin=0,flags=0;
    std::uint64_t task=0,task_revision=0; // private reuse guards; never exported
    unsigned task_slot=128;
    explicit operator bool()const noexcept{return epoch&&request;}
};
template<class Mutex=std::mutex>class BasicNativePreviewHandoff {
    struct Task {std::uint64_t address=0;NativePreviewHandoffLabel label;};
    struct Row {std::uint64_t tick=0,request=0;unsigned stage=0,origin=0,flags=0;};
    NativePreviewHandoffSource source_{};
    NativePreviewHandoffLabel pending_{};
    std::array<Task,128> tasks_{};
    std::array<std::atomic<std::uint64_t>,128> task_keys_{};
    std::array<std::atomic<std::uint64_t>,128> task_revisions_{};
    bool task_current(const NativePreviewHandoffLabel& label)const noexcept {
        return label.task&&label.task_slot<task_keys_.size()&&
            task_keys_[label.task_slot].load(std::memory_order_acquire)==label.task&&
            task_revisions_[label.task_slot].load(std::memory_order_acquire)==label.task_revision;
    }
    void revoke_task(std::uint64_t task)noexcept {
        for(std::size_t i=0;i<task_keys_.size();++i)if(task_keys_[i].load(std::memory_order_acquire)==task){
            task_revisions_[i].fetch_add(1,std::memory_order_acq_rel);return;}
    }
    mutable Mutex mutex_;
    std::uint64_t epoch_=0,serial_=0;
    std::uint64_t pending_revision_=0;
    std::atomic<std::uint64_t> request_revision_{0};
    std::atomic<std::uint64_t> buffer_{0};
    // A zero publication epoch revokes the cohort without acquiring report's
    // mutex. Fields change only while that epoch is zero; readers verify twice.
    std::atomic<std::uint64_t> published_epoch_{0},published_client_{0},published_main_{0},published_renderer_{0},published_owner_{0};
    std::array<Row,384> rows_{};
    std::array<std::uint64_t,6> next_{};
    std::size_t cursor_=0,count_=0;
    void note(unsigned stage,const NativePreviewHandoffLabel& label,unsigned flags,std::uint64_t now)noexcept {
        if(stage<1||stage>6||now>std::numeric_limits<std::uint64_t>::max()-2000||
           (next_[stage-1]&&now<next_[stage-1]))return;
        next_[stage-1]=now+2000;
        rows_[cursor_]={now,label.request,stage,label.origin,flags};
        cursor_=(cursor_+1)%rows_.size();count_=std::min(count_+1,rows_.size());
    }
    bool current(const NativePreviewHandoffSource& source)const noexcept {
        return source_.epoch&&source_.epoch==published_epoch_.load()&&source.epoch==source_.epoch&&source.client==source_.client&&
            source.main==source_.main&&source.renderer==source_.renderer&&source.buffer==source_.buffer&&source.owner==source_.owner;
    }
    void clear()noexcept {
        published_epoch_.store(0);
        source_={};pending_={};tasks_.fill({});buffer_.store(0);
        for(std::size_t i=0;i<task_keys_.size();++i){task_revisions_[i].fetch_add(1,std::memory_order_acq_rel);task_keys_[i].store(0,std::memory_order_release);}
    }
public:
    void finish(const NativePreviewHandoffLabel& label)noexcept {
        if(!task_current(label))return;
        auto task=label.task;task_keys_[label.task_slot].compare_exchange_strong(task,0,std::memory_order_acq_rel);
    }
    bool active()const noexcept{return published_epoch_.load()!=0;}
    void unavailable(const NativePreviewHandoffSource&)noexcept{request_revision_.fetch_add(1,std::memory_order_acq_rel);}
    std::uint64_t retire_owner(std::uint64_t owner)noexcept {
        if(!owner)return 0;
        const auto snapshot=source();auto epoch=snapshot.epoch;
        return epoch&&snapshot.owner==owner&&published_epoch_.compare_exchange_strong(epoch,0)?snapshot.buffer:0;
    }
    // Formatting/correlation aliases belong to the report worker, never hooks.
    std::string report(std::uint64_t now)const noexcept {
        try{std::array<Row,384> rows;std::size_t count=0,cursor=0;
            {std::lock_guard<Mutex> lock(mutex_);rows=rows_;count=count_;cursor=cursor_;}
            std::array<std::uint64_t,384> requests{};std::size_t aliases=0;
            std::string result;result.reserve(count*100);const auto first=(cursor+rows.size()-count)%rows.size();
            for(std::size_t i=0;i<count;++i){const auto& row=rows[(first+i)%rows.size()];if(row.tick>now||now-row.tick>120000)continue;
                std::size_t alias=0;
                if(row.request){auto found=std::find(requests.begin(),requests.begin()+aliases,row.request);
                    if(found==requests.begin()+aliases)requests[aliases++]=row.request;
                    alias=std::size_t(found-requests.begin())+1;}
                result+="preview-handoff.sample stage="+std::to_string(row.stage)+" origin="+std::to_string(row.origin)+
                    " request="+std::to_string(alias)+" flags="+std::to_string(row.flags)+" age-ms="+std::to_string(now-row.tick)+'\n';}
            return result;
        }catch(...){return {};}
    }
    NativePreviewHandoffSource configure(std::uint64_t client,std::uint64_t main,std::uint64_t renderer,std::uint64_t buffer,std::uint64_t owner=0)noexcept {
        if(!client||!main||!renderer||!buffer){published_epoch_.store(0);return {};}
        const auto snapshot=source();
        if(snapshot.epoch&&snapshot.client==client&&snapshot.main==main&&snapshot.renderer==renderer&&snapshot.buffer==buffer&&snapshot.owner==owner)return snapshot;
        published_epoch_.store(0);
        try{std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
            clear();next_.fill(0);if(epoch_==std::numeric_limits<std::uint64_t>::max())return {};
            source_={++epoch_,client,main,renderer,buffer,owner};
            published_client_.store(client);published_main_.store(main);
            published_renderer_.store(renderer);published_owner_.store(owner);
            buffer_.store(buffer);published_epoch_.store(source_.epoch);return source_;
        }catch(...){return {};}
    }
    NativePreviewHandoffSource source()const noexcept {
        const auto epoch=published_epoch_.load();if(!epoch)return {};
        NativePreviewHandoffSource snapshot{epoch,published_client_.load(),published_main_.load(),published_renderer_.load(),buffer_.load(),published_owner_.load()};
        return published_epoch_.load()==epoch?snapshot:NativePreviewHandoffSource{};
    }
    void request(const NativePreviewHandoffSource& source,unsigned origin,bool accepted,std::uint64_t now)noexcept {
        const auto revision=accepted?request_revision_.fetch_add(1,std::memory_order_acq_rel)+1:request_revision_.load(std::memory_order_acquire);
        try{std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock()||!current(source))return;
            if(!accepted){note(1,{source_.epoch,0,origin<=6?origin:0,0},1,now);return;}
            if(request_revision_.load(std::memory_order_acquire)!=revision)return;
            pending_={};if(serial_!=std::numeric_limits<std::uint64_t>::max())pending_={source_.epoch,++serial_,origin<=6?origin:0,3};
            pending_revision_=revision;
            note(1,pending_,pending_.flags,now);
        }catch(...){}
    }
    NativePreviewHandoffLabel render(const NativePreviewHandoffSource& source,bool supported,bool copied,std::uint64_t now)noexcept {
        try{std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock()||!current(source))return {};
            if(!supported||!copied||!pending_||pending_revision_!=request_revision_.load(std::memory_order_acquire)){
                note(2,{},1|(copied?4:0)|(supported?8:0),now);return {};}
            auto label=pending_;label.flags|=12;note(2,label,label.flags,now);return label;
        }catch(...){return {};}
    }
    void enqueue(std::uint64_t task,const NativePreviewHandoffLabel& label,std::uint64_t now)noexcept {
        if(!task||!active())return;
        if(!label){revoke_task(task);return;}
        try{std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);
            if(!lock.owns_lock()){revoke_task(task);return;}
            if(label.epoch!=source_.epoch||source_.epoch!=published_epoch_.load())return;
            std::size_t index=task_keys_.size(),empty=index;
            for(std::size_t i=0;i<task_keys_.size();++i){const auto key=task_keys_[i].load(std::memory_order_acquire);
                if(key==task){index=i;break;}if(!key&&empty==task_keys_.size())empty=i;}
            if(index==task_keys_.size())index=empty;
            if(index==task_keys_.size()){note(3,label,128,now);return;}
            const auto revision=task_revisions_[index].fetch_add(1,std::memory_order_acq_rel)+1;
            auto& slot=tasks_[index];
            if(slot.address==task){slot.label={};note(3,label,128,now);return;}
            task_keys_[index].store(task,std::memory_order_release);
            slot.address=task;slot.label=label;slot.label.task=task;slot.label.task_revision=revision;slot.label.task_slot=unsigned(index);
            slot.label.flags|=16;note(3,slot.label,slot.label.flags,now);
        }catch(...){}
    }
    NativePreviewHandoffLabel execute(std::uint64_t task,std::uint64_t now)noexcept {
        if(!task||!active())return {};
        try{std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return {};
            if(source_.epoch!=published_epoch_.load())return {};
            for(std::size_t i=0;i<task_keys_.size();++i)if(task_keys_[i].load(std::memory_order_acquire)==task){auto& slot=tasks_[i];
                if(slot.address!=task){
                    if(!(slot.label.flags&32)||!task_current(slot.label)){task_keys_[i].store(0,std::memory_order_release);slot.label={};}
                    note(4,{},1,now);return {};}
                auto label=slot.label;slot.address=0;
                if(label.epoch!=source_.epoch||!label||!task_current(label)){
                    task_keys_[i].store(0,std::memory_order_release);slot.label={};note(4,{},128,now);return {};}
                label.flags|=32;slot.label=label;note(4,label,label.flags,now);return label;}
            note(4,{},1,now);
        }catch(...){}return {};
    }
    bool gpu(std::uint64_t buffer,const NativePreviewHandoffLabel& label,unsigned stage,std::uint64_t now)noexcept {
        if(!buffer||!active()||buffer_.load(std::memory_order_acquire)!=buffer||(stage!=5&&stage!=6))return false;
        try{std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock()||source_.buffer!=buffer||source_.epoch!=published_epoch_.load())return false;
            if(!label||source_.epoch!=label.epoch||!(label.flags&32)||!task_current(label)){
                note(stage,{},1|64,now);return false;}
            note(stage,label,label.flags|64,now);return true;
        }catch(...){return false;}
    }
    void retire(std::uint64_t buffer)noexcept {
        if(!buffer)return;
        const auto snapshot=source();auto epoch=snapshot.epoch;
        if(epoch&&snapshot.buffer==buffer)published_epoch_.compare_exchange_strong(epoch,0);
    }
};
using NativePreviewHandoff=BasicNativePreviewHandoff<>;
struct NativePreviewHandoffThread {NativePreviewHandoffLabel rendering,working;};
inline NativePreviewHandoff& native_preview_handoff_history(){static NativePreviewHandoff value;return value;}
inline NativePreviewHandoffThread& native_preview_handoff_thread(){static thread_local NativePreviewHandoffThread value;return value;}
inline void native_preview_handoff_gpu(std::uint64_t buffer,bool inline_copy,std::uint64_t tick)noexcept {
    native_preview_handoff_history().gpu(buffer,native_preview_handoff_thread().working,inline_copy?6:5,tick);
}
void initialize_native_preview_handoff()noexcept;
unsigned native_preview_handoff_setup_status()noexcept; // 0 idle,1 starting,2 ready,3 unsupported,4 failed
}
