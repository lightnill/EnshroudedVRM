#pragma once
#include "native_owner.hpp"
#include <array>
#include <atomic>
#include <mutex>
#include <tuple>
namespace evrm {
enum class NativePreviewSubject:unsigned char {unresolved,local,other};
// Private transient proposal. Never serialized and never an ownership permit.
struct NativePreviewActor {
    std::uint64_t image=0,client=0,controller=0,main=0,execution=0,registry=0,selector=0,entity=0;
    std::uint32_t id=0,generation=0;
    bool valid()const noexcept{return image&&client&&controller&&main&&execution&&registry&&selector&&entity&&id;}
    bool operator==(const NativePreviewActor& b)const noexcept {
        return std::tie(image,client,controller,main,execution,registry,selector,entity,id,generation)==
            std::tie(b.image,b.client,b.controller,b.main,b.execution,b.registry,b.selector,b.entity,b.id,b.generation);
    }
};
inline NativePreviewActor native_preview_actor(const NativeOwnerCandidate& c)noexcept {
    return {c.image.base,c.client,c.controller,c.main,c.execution,c.registry,c.local_player_data,c.entity,c.entity_id,c.entity_generation};
}
struct NativePreviewContext {
    NativePreviewActor actor;
    std::uint64_t owner=0,device=0,buffer=0,generation=0,renderer=0,epoch=0,world_lifetime=0;
    bool valid()const noexcept{return actor.valid()&&owner&&device&&buffer&&generation&&renderer;}
    bool same(const NativePreviewContext& b)const noexcept {
        return actor==b.actor&&std::tie(owner,device,buffer,generation,renderer,world_lifetime)==
            std::tie(b.owner,b.device,b.buffer,b.generation,b.renderer,b.world_lifetime);
    }
};
struct NativePreviewStamp {
    NativePreviewSubject subject=NativePreviewSubject::unresolved;
    NativePreviewContext source;
    std::uint64_t request=0;
    bool local()const noexcept{return subject==NativePreviewSubject::local&&request&&source.epoch&&source.valid();}
};
inline bool same_native_preview_stamp(const NativePreviewStamp& a,const NativePreviewStamp& b)noexcept {
    return a.subject==b.subject&&a.request==b.request&&a.source.epoch==b.source.epoch&&a.source.same(b.source);
}
inline bool same_native_preview_local(const NativePreviewStamp& a,const NativePreviewStamp& b)noexcept {
    return a.local()&&b.local()&&a.source.epoch==b.source.epoch&&a.source.same(b.source);
}
struct NativePreviewTicket {std::uint64_t epoch=0,revision=0,overlap=0;bool tracked=false;};
template<class Mutex=std::mutex>class BasicNativePreviewSubjects {
    mutable Mutex mutex_;
    NativePreviewContext source_;
    NativePreviewStamp pending_;
    struct Task {std::atomic<std::uint64_t> pointer{0},revision{0};NativePreviewStamp stamp;};
    std::array<Task,128> tasks_{};
    std::array<std::atomic<std::uint64_t>,15> published_{};
    std::atomic<std::uint64_t> active_{0},revision_{0},owner_{0},device_{0},buffer_{0};
    std::atomic<std::uint64_t> writers_{0},overlap_{0};
    std::atomic<std::uint64_t> lifetime_{1};std::atomic<unsigned> transitions_{0};std::atomic<bool> exhausted_{false};
    std::uint64_t epoch_=0;
    void revoke()noexcept{active_.store(0);}
    void advance_lifetime()noexcept{if(lifetime_.fetch_add(1)==UINT64_MAX)exhausted_.store(true);}
    bool lifetime_ready(std::uint64_t generation)const noexcept {
        return generation&&!exhausted_.load()&&!transitions_.load()&&lifetime_.load()==generation;
    }
    bool valid(const NativePreviewStamp& stamp)const noexcept {
        return stamp.request&&stamp.source.epoch&&active_.load()==stamp.source.epoch&&lifetime_ready(stamp.source.world_lifetime);
    }
    void retire(std::uint64_t owner,std::uint64_t device,std::uint64_t buffer)noexcept {
        auto epoch=active_.load();if(!epoch)return;
        const bool match=owner?owner_.load()==owner:device_.load()==device&&buffer_.load()==buffer;
        if(match)active_.compare_exchange_strong(epoch,0);
    }
    void publish(const NativePreviewContext& c)noexcept {
        const auto& a=c.actor;const std::array<std::uint64_t,15> values{{a.image,a.client,a.controller,a.main,a.execution,a.registry,a.selector,a.entity,
            (std::uint64_t(a.generation)<<32)|a.id,c.owner,c.device,c.buffer,c.generation,c.renderer,c.world_lifetime}};
        for(std::size_t i=0;i<values.size();++i)published_[i].store(values[i]);
    }
public:
    // Exact original Main constructor/destructor boundaries. This deliberately
    // vetoes all preview cohorts during a native Main lifecycle operation,
    // including same-address placement/reuse and nested/throwing operations.
    void begin_lifetime()noexcept{transitions_.fetch_add(1);advance_lifetime();revoke();}
    void end_lifetime()noexcept{advance_lifetime();revoke();transitions_.fetch_sub(1);}
    bool active()const noexcept{return active_.load()!=0;}
    NativePreviewContext configure(NativePreviewContext proposal)noexcept {
        proposal.world_lifetime=lifetime_.load();if(!lifetime_ready(proposal.world_lifetime))return {};
        const auto previous=source();if(previous.epoch&&previous.same(proposal))return previous;
        revoke();
        std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock()||!proposal.valid()||!lifetime_ready(proposal.world_lifetime))return {};
        pending_={};for(auto& task:tasks_)task.pointer.store(0);
        if(epoch_==UINT64_MAX)return {};
        proposal.epoch=++epoch_;source_=proposal;
        owner_.store(proposal.owner);device_.store(proposal.device);buffer_.store(proposal.buffer);
        publish(proposal);
        active_.store(proposal.epoch);
        if(!lifetime_ready(proposal.world_lifetime)){auto epoch=proposal.epoch;active_.compare_exchange_strong(epoch,0);return {};}
        return source_;
    }
    NativePreviewContext source()const noexcept {
        for(unsigned attempt=0;attempt<2;++attempt){const auto epoch=active_.load();if(!epoch)return {};
            std::array<std::uint64_t,15> v{};for(std::size_t i=0;i<v.size();++i)v[i]=published_[i].load();
            if(active_.load()!=epoch||!lifetime_ready(v[14]))continue;
            return {{v[0],v[1],v[2],v[3],v[4],v[5],v[6],v[7],static_cast<std::uint32_t>(v[8]),static_cast<std::uint32_t>(v[8]>>32)},
                v[9],v[10],v[11],v[12],v[13],epoch,v[14]};
        }return {};
    }
    NativePreviewTicket begin_request(std::uint64_t epoch)noexcept {
        // Invalidate BEFORE the original can publish any request bytes. Even
        // contention/failed reads must not leave the preceding local tag live.
        const auto overlap=overlap_.load();if(writers_.fetch_add(1))overlap_.fetch_add(1);
        const auto revision=revision_.fetch_add(1)+1;NativePreviewTicket ticket{0,revision,overlap,true};
        if(!revision){revoke();return ticket;}
        std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock())return ticket;
        pending_={};
        if(epoch&&active_.load()==epoch)ticket.epoch=epoch;
        return ticket;
    }
    void finish_request(NativePreviewTicket ticket,NativePreviewSubject subject,bool commit=true)noexcept {
        if(!ticket.tracked||writers_.fetch_sub(1)!=1)return;
        std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock())return;
        if(!commit||!ticket.epoch||active_.load()!=ticket.epoch||revision_.load()!=ticket.revision||overlap_.load()!=ticket.overlap)return;
        pending_={subject,source_,ticket.revision};
    }
    NativePreviewStamp copy_proposal(std::uint64_t epoch)const noexcept {
        std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);
        return lock.owns_lock()&&!writers_.load()&&epoch&&epoch==pending_.source.epoch&&valid(pending_)&&
            revision_.load()==pending_.request?pending_:NativePreviewStamp{};
    }
    NativePreviewStamp copied(const NativePreviewStamp& before)const noexcept {
        std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);
        return lock.owns_lock()&&!writers_.load()&&valid(before)&&revision_.load()==before.request&&
            same_native_preview_stamp(before,pending_)?before:NativePreviewStamp{};
    }
    void allocated(std::uint64_t pointer,const NativePreviewStamp& stamp)noexcept {
        if(!pointer)return;
        // Revoke an exact reused address even if an unrelated task owns the
        // stamp mutex. Unknown native allocations never take that mutex.
        for(auto& task:tasks_)if(task.pointer.load()==pointer){auto expected=pointer;task.pointer.compare_exchange_strong(expected,0);}
        if(!valid(stamp))return;
        std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);
        if(!lock.owns_lock())return;
        if(!valid(stamp))return;
        for(auto& task:tasks_)if(!task.pointer.load()){
            const auto revision=task.revision.load();if(revision==UINT64_MAX)continue;
            task.stamp=stamp;task.revision.store(revision+1);task.pointer.store(pointer);return;
        }
        // Capacity loss cannot turn an earlier allocation into new authority.
    }
    NativePreviewStamp consume(std::uint64_t pointer)noexcept {
        if(!pointer)return {};
        for(auto& task:tasks_)if(task.pointer.load()==pointer){
            const auto revision=task.revision.load();auto expected=pointer;
            if(!task.pointer.compare_exchange_strong(expected,0))continue;
            // Consume once before locking; a contended worker cannot replay an
            // earlier label. Slot revision prevents free-slot reuse ABA.
            std::unique_lock<Mutex> lock(mutex_,std::try_to_lock);
            if(!lock.owns_lock()||task.revision.load()!=revision)return {};
            return valid(task.stamp)?task.stamp:NativePreviewStamp{};
        }
        return {};
    }
    bool current(const NativePreviewStamp& stamp)const noexcept{return valid(stamp);}
    void retire_owner(std::uint64_t owner)noexcept{if(owner)retire(owner,0,0);}
    void retire_buffer(std::uint64_t device,std::uint64_t buffer)noexcept{if(device&&buffer)retire(0,device,buffer);}
};
using NativePreviewSubjects=BasicNativePreviewSubjects<>;
inline NativePreviewSubjects& native_preview_subjects()noexcept{static NativePreviewSubjects value;return value;}
struct NativePreviewSubjectThread {
    std::uint64_t packet=0,request_payload=0;
    NativePreviewStamp proposed,copied,rendering,working;
};
inline NativePreviewSubjectThread& native_preview_subject_thread()noexcept{thread_local NativePreviewSubjectThread value;return value;}
inline NativePreviewStamp native_preview_working(std::uint64_t device,std::uint64_t buffer)noexcept {
    const auto value=native_preview_subject_thread().working;
    return value.source.device==device&&value.source.buffer==buffer&&native_preview_subjects().current(value)?value:NativePreviewStamp{};
}
}
