#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <array>
#include <cstddef>
#include <cstdint>
#include <mutex>

namespace evrm {
// Idle private host buffers, keyed by exact byte size and usage. The owner
// offers an entry only where it previously destroyed that buffer, so an idle
// entry has no recorded, submitted, history or readback consumer left. Entries
// are driver handles and a mapping only: the pool never interprets or retains
// contents, permits, poses, owners or history, and it issues no Vulkan call.
// Storage is fixed; keep/take/drain never allocate. An internal lock failure
// declines the operation, which the owner treats as ordinary destruction.
class NativePrivateBufferPool {
public:
    struct Entry {
        VkBuffer handle{};VkDeviceMemory memory{};void* mapped=nullptr;
        VkDeviceSize size=0;VkBufferUsageFlags usage=0;std::uint64_t stamp=0;
        VkDeviceSize allocation_size=0;
    };
    static constexpr std::size_t max_entries=64;
    static constexpr VkDeviceSize max_entry_bytes=VkDeviceSize(4)*1024*1024;
    static constexpr VkDeviceSize max_bytes=VkDeviceSize(16)*1024*1024;
    using Entries=std::array<Entry,max_entries>;
    enum class Kept {no,yes,displaced};
    // Exact size and usage only: the result is the object a new allocation
    // with the same create info would have been. Most recently kept first.
    bool take(VkDeviceSize size,VkBufferUsageFlags usage,Entry& out)noexcept {
        if(!size||size>max_entry_bytes)return false;
        try{std::lock_guard<std::mutex> lock(mutex_);
            std::size_t found=max_entries;
            for(std::size_t n=0;n<count_;++n)if(idle_[n].size==size&&idle_[n].usage==usage&&
                (found==max_entries||idle_[n].stamp>idle_[found].stamp))found=n;
            if(found==max_entries)return false;
            out=idle_[found];bytes_-=out.allocation_size;idle_[found]=idle_[--count_];idle_[count_]={};return true;
        }catch(...){}
        return false;
    }
    // no: the caller still owns `entry` and destroys it as before.
    // displaced: `entry` was kept and the caller now owns and destroys
    // `displaced`, the longest-idle entry. A full pool therefore cannot be
    // occupied indefinitely by sizes that are no longer requested.
    Kept keep(Entry entry,Entry& displaced)noexcept {
        if(!entry.handle||!entry.memory||!entry.mapped||!entry.size||entry.size>max_entry_bytes||
           entry.allocation_size<entry.size||entry.allocation_size>max_entry_bytes)return Kept::no;
        try{std::lock_guard<std::mutex> lock(mutex_);
            if(!retaining_)return Kept::no;
            if(count_<max_entries&&entry.allocation_size<=max_bytes-bytes_){
                entry.stamp=++clock_;idle_[count_++]=entry;bytes_+=entry.allocation_size;return Kept::yes;
            }
            if(!count_)return Kept::no;
            std::size_t oldest=0;
            for(std::size_t n=1;n<count_;++n)if(idle_[n].stamp<idle_[oldest].stamp)oldest=n;
            if(entry.allocation_size>max_bytes-(bytes_-idle_[oldest].allocation_size))return Kept::no;
            displaced=idle_[oldest];bytes_-=displaced.allocation_size;
            entry.stamp=++clock_;idle_[oldest]=entry;bytes_+=entry.allocation_size;return Kept::displaced;
        }catch(...){}
        return Kept::no;
    }
    // Transfers every idle entry to the caller and sets whether later keeps
    // are accepted. Returns the number of valid leading entries in `out`.
    std::size_t drain(bool retain,Entries& out)noexcept {
        try{std::lock_guard<std::mutex> lock(mutex_);
            retaining_=retain;const auto count=count_;
            for(std::size_t n=0;n<count;++n){out[n]=idle_[n];idle_[n]={};}
            count_=0;bytes_=0;return count;
        }catch(...){}
        return 0;
    }
    // Pressure recovery: release idle entries without changing the current
    // enabled/disabled retention policy. Outstanding leases are not in here.
    std::size_t evict(Entries& out)noexcept {
        try{std::lock_guard<std::mutex> lock(mutex_);
            const auto count=count_;
            for(std::size_t n=0;n<count;++n){out[n]=idle_[n];idle_[n]={};}
            count_=0;bytes_=0;return count;
        }catch(...){}
        return 0;
    }
    std::size_t idle_entries()const noexcept {try{std::lock_guard<std::mutex> lock(mutex_);return count_;}catch(...){return 0;}}
    VkDeviceSize idle_bytes()const noexcept {try{std::lock_guard<std::mutex> lock(mutex_);return bytes_;}catch(...){return 0;}}
    // Read-only and never waits. False when the pool is busy, and then the
    // outputs are untouched. On true both values come from one lock.
    bool try_idle(std::size_t& entries,VkDeviceSize& bytes)const noexcept {
        try{std::unique_lock<std::mutex> lock(mutex_,std::try_to_lock);if(!lock.owns_lock())return false;
            entries=count_;bytes=bytes_;return true;
        }catch(...){return false;}
    }
#ifdef EVRM_PRIVATE_REUSE_TESTING
    // Fixtures only; no product build defines the macro. Runs `inside` with
    // the pool's lock held, standing in for a busy keep, take or drain.
    template<class Hold> void test_hold(Hold&& inside)const {std::lock_guard<std::mutex> lock(mutex_);inside();}
#endif
private:
    mutable std::mutex mutex_;
    Entries idle_{};
    std::size_t count_=0;
    VkDeviceSize bytes_=0;
    std::uint64_t clock_=0;
    bool retaining_=true;
};
}
