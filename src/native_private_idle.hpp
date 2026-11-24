#pragma once
#include <array>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <mutex>

namespace evrm {
inline std::uint64_t native_private_idle_clock()noexcept {
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}
// Retired private driver storage awaiting reuse, in fixed slots. The owner
// offers an entry only where it previously destroyed that storage, so an idle
// entry has no recorded, submitted or readback consumer left. The holder never
// interprets an entry and issues no Vulkan call: everything it gives up is
// returned to the caller, which destroys it outside this lock. keep/take/
// expire/drain never allocate. A lock failure declines the operation, which
// the owner treats as ordinary destruction.
template<class T,std::size_t N> class NativePrivateIdle {
public:
    using Clock=std::uint64_t(*)()noexcept;
    using Entries=std::array<std::unique_ptr<T>,N>;
    explicit NativePrivateIdle(std::uint64_t idle_milliseconds,Clock clock=nullptr)noexcept
        :limit_(idle_milliseconds),clock_(clock?clock:native_private_idle_clock){}
    // Returns what the caller must destroy now: `entry` itself when it is not
    // kept, the longest-idle entry when a full holder accepted `entry`, or
    // nothing. A full holder therefore cannot stay occupied by storage of a
    // shape that is no longer requested.
    // With N of zero nothing is ever kept.
    std::unique_ptr<T> keep(std::unique_ptr<T> entry)noexcept {
        if(!entry)return entry;
        if constexpr(N==0)return entry;
        else try{std::lock_guard<std::mutex> lock(mutex_);
            if(!retaining_)return entry;
            std::size_t slot=N,oldest=0;
            for(std::size_t n=0;n<N;++n){
                if(!idle_[n].value){slot=n;break;}
                if(idle_[n].stamp<idle_[oldest].stamp)oldest=n;
            }
            std::unique_ptr<T> displaced;
            if(slot==N){slot=oldest;displaced=std::move(idle_[slot].value);}else count_.fetch_add(1,std::memory_order_relaxed);
            idle_[slot].value=std::move(entry);idle_[slot].since=clock_();idle_[slot].stamp=++sequence_;
            return displaced;
        }catch(...){return entry;}
    }
    // Most recently kept entry accepted by `match`, or nothing.
    template<class Match> std::unique_ptr<T> take(Match&& match)noexcept {
        if(!count_.load(std::memory_order_relaxed))return {};
        try{std::lock_guard<std::mutex> lock(mutex_);
            std::size_t found=N;
            for(std::size_t n=0;n<N;++n)if(idle_[n].value&&match(*idle_[n].value)&&(found==N||idle_[n].stamp>idle_[found].stamp))found=n;
            if(found==N)return {};
            count_.fetch_sub(1,std::memory_order_relaxed);return std::move(idle_[found].value);
        }catch(...){}
        return {};
    }
    // Every idle entry accepted by `match`, for the caller to destroy before
    // it allocates what those entries cannot serve. Entries that `match`
    // rejects stay where they are. One relaxed load when nothing is idle.
    template<class Match> Entries evict(Match&& match)noexcept {
        Entries out;
        if(!count_.load(std::memory_order_relaxed))return out;
        try{std::lock_guard<std::mutex> lock(mutex_);
            for(std::size_t n=0;n<N;++n)if(idle_[n].value&&match(*idle_[n].value)){
                out[n]=std::move(idle_[n].value);count_.fetch_sub(1,std::memory_order_relaxed);}
        }catch(...){}
        return out;
    }
    // Entries idle for longer than the limit. A clock that moved backwards
    // expires nothing. One relaxed load when nothing is idle.
    Entries expire()noexcept {
        Entries out;
        if(!count_.load(std::memory_order_relaxed))return out;
        try{std::lock_guard<std::mutex> lock(mutex_);const auto now=clock_();
            for(std::size_t n=0;n<N;++n)if(idle_[n].value&&now>=idle_[n].since&&now-idle_[n].since>limit_){
                out[n]=std::move(idle_[n].value);count_.fetch_sub(1,std::memory_order_relaxed);}
        }catch(...){}
        return out;
    }
    // Every idle entry, and whether later keeps are accepted.
    Entries drain(bool retain)noexcept {
        Entries out;
        try{std::lock_guard<std::mutex> lock(mutex_);retaining_=retain;
            for(std::size_t n=0;n<N;++n)out[n]=std::move(idle_[n].value);
            count_.store(0,std::memory_order_relaxed);
        }catch(...){}
        return out;
    }
    std::size_t size()const noexcept {return count_.load(std::memory_order_relaxed);}
    template<class Visit> void visit(Visit&& each)const noexcept {
        try{std::lock_guard<std::mutex> lock(mutex_);for(const auto& slot:idle_)if(slot.value)each(*slot.value);}catch(...){}
    }
    // Read-only and never waits. False when the holder is busy; `each` may
    // then have seen nothing. On true, every idle entry was visited under one
    // lock, so a count and a sum taken by `each` describe the same instant.
    template<class Visit> bool try_visit(Visit&& each)const noexcept {
        try{std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return false;
            for(const auto& slot:idle_)if(slot.value)each(*slot.value);return true;
        }catch(...){return false;}
    }
#ifdef EVRM_PRIVATE_REUSE_TESTING
    // Fixtures only; no product build defines the macro. Runs `inside` with
    // the holder's lock held, standing in for a busy keep, take or drain.
    template<class Hold> void test_hold(Hold&& inside)const {std::lock_guard<std::mutex> lock(mutex_);inside();}
#endif
private:
    struct Slot {std::unique_ptr<T> value;std::uint64_t since=0,stamp=0;};
    mutable std::mutex mutex_;
    std::array<Slot,N> idle_{};
    std::atomic<std::size_t> count_{0};
    std::uint64_t limit_,sequence_=0;
    Clock clock_;
    bool retaining_=true;
};
}
