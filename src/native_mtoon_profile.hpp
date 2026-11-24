#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>

namespace evrm {
enum class NativeMToonProfileStage : std::uint32_t {
    work,outline_visibility,outline_gbuffer,forward_capture,opaque_shade,
    forward_shade,outline_shade,composite,forward_finish,count
};
inline constexpr unsigned native_mtoon_profile_stages=9,native_mtoon_profile_queries=18;
struct NativeMToonProfileConfig {
    bool enabled=false;
    std::uint32_t observed_family=UINT32_MAX,selected_family=UINT32_MAX,timestamp_valid_bits=0;
    float timestamp_period_ns=0;
    bool valid()const noexcept{return enabled&&observed_family!=UINT32_MAX&&observed_family==selected_family&&
        timestamp_valid_bits>=36&&timestamp_valid_bits<=64&&std::isfinite(timestamp_period_ns)&&timestamp_period_ns>0;}
};
struct NativeMToonProfileResult {
    bool requested=false,enabled=false,reset_recorded=false,read_attempted=false,age_valid=false,available=false;
    std::uint32_t generation=0,written_mask=0,valid_mask=0;
    VkResult query_result=VK_NOT_READY;
    NativeMToonProfileConfig config;
    // A value is meaningful only when its valid_mask bit is set. Missing or
    // unavailable intervals are never represented as measured zero durations.
    std::array<double,native_mtoon_profile_stages> milliseconds{};
};

// One optional pool per existing FrameBank, never a separate in-flight ring.
// The caller owns serialization and proves original retirement before issuing a
// new lease or destroying this owner. Uncertainty deliberately keeps the pool.
class NativeMToonProfileQueries {
public:
    struct Result {std::uint64_t ticks=0,availability=0;};
    using Results=std::array<Result,native_mtoon_profile_queries>;
    NativeMToonProfileQueries()=default;
    NativeMToonProfileQueries(const NativeMToonProfileQueries&)=delete;
    NativeMToonProfileQueries& operator=(const NativeMToonProfileQueries&)=delete;
    ~NativeMToonProfileQueries(){if(pool_&&destroy_&&!quarantined())destroy_(device_,pool_,nullptr);}
    std::uint64_t prepare(VkDevice device,PFN_vkGetDeviceProcAddr get,const NativeMToonProfileConfig& config,
        std::uint32_t generation,const bool* uncertainty)noexcept{
        if(!config.valid()||!device||!get||!generation||quarantined())return 0;
        if(attempted_){
            if(!pool_||device!=device_||get!=get_||uncertainty!=parent_||generation<=generation_||
                config.observed_family!=config_.observed_family||config.selected_family!=config_.selected_family||
                config.timestamp_valid_bits!=config_.timestamp_valid_bits||config.timestamp_period_ns!=config_.timestamp_period_ns)return 0;
        }else{
            attempted_=true;device_=device;get_=get;parent_=uncertainty;config_=config;
            if(quarantined())return 0;
            try{
                const auto create=reinterpret_cast<PFN_vkCreateQueryPool>(get(device,"vkCreateQueryPool"));
                destroy_=reinterpret_cast<PFN_vkDestroyQueryPool>(get(device,"vkDestroyQueryPool"));
                reset_=reinterpret_cast<PFN_vkCmdResetQueryPool>(get(device,"vkCmdResetQueryPool"));
                write_=reinterpret_cast<PFN_vkCmdWriteTimestamp>(get(device,"vkCmdWriteTimestamp"));
                read_=reinterpret_cast<PFN_vkGetQueryPoolResults>(get(device,"vkGetQueryPoolResults"));
                if(!create||!destroy_||!reset_||!write_||!read_)return 0;
                VkQueryPoolCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
                info.queryType=VK_QUERY_TYPE_TIMESTAMP;info.queryCount=native_mtoon_profile_queries;
                VkQueryPool candidate{};if(create(device,&info,nullptr,&candidate)!=VK_SUCCESS||!candidate)return 0;
                pool_=candidate;
            }catch(...){return 0;} // An optional diagnostic never vetoes rendering.
        }
        if(lease_==std::numeric_limits<std::uint64_t>::max())return 0;
        generation_=generation;return ++lease_;
    }
    bool current(std::uint64_t lease,std::uint32_t generation)const noexcept{
        return pool_&&!quarantined()&&lease&&lease==lease_&&generation&&generation==generation_;
    }
    void quarantine()noexcept{uncertain_=true;}
    bool reset(VkCommandBuffer command,std::uint64_t lease,std::uint32_t generation)const noexcept{
        if(!command||!current(lease,generation))return false;
        reset_(command,pool_,0,native_mtoon_profile_queries);return true;
    }
    bool write(VkCommandBuffer command,unsigned query,std::uint64_t lease,std::uint32_t generation)const noexcept{
        if(!command||query>=native_mtoon_profile_queries||!current(lease,generation))return false;
        write_(command,query%2?VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT:VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,pool_,query);return true;
    }
    VkResult read(Results& values,std::uint64_t lease,std::uint32_t generation)const noexcept{
        if(!current(lease,generation))return VK_NOT_READY;
        static_assert(sizeof(Result)==16,"Timestamp result/availability layout");
        return read_(device_,pool_,0,native_mtoon_profile_queries,sizeof(values),values.data(),sizeof(Result),
            VK_QUERY_RESULT_64_BIT|VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);
    }
private:
    bool quarantined()const noexcept{return uncertain_||(parent_&&*parent_);}
    VkDevice device_{};PFN_vkGetDeviceProcAddr get_{};VkQueryPool pool_{};
    PFN_vkDestroyQueryPool destroy_{};PFN_vkCmdResetQueryPool reset_{};
    PFN_vkCmdWriteTimestamp write_{};PFN_vkGetQueryPoolResults read_{};
    NativeMToonProfileConfig config_;const bool* parent_=nullptr;
    std::uint64_t lease_=0;std::uint32_t generation_=0;bool attempted_=false,uncertain_=false;
};

// Metadata belongs to exactly one original recording generation. Query
// availability is not completion proof: after_original_completion may be called
// only after the parent independently establishes successful original execution.
class NativeMToonProfileFrame {
public:
    using Clock=std::chrono::steady_clock;
    void prepare(NativeMToonProfileQueries& queries,VkDevice device,PFN_vkGetDeviceProcAddr get,
        const NativeMToonProfileConfig& config,std::uint32_t generation,VkCommandBuffer command,const bool* uncertainty=nullptr)noexcept{
        *this=NativeMToonProfileFrame{};config_=config;generation_=generation;command_=command;
        if(!command||!config.valid())return;
        lease_=queries.prepare(device,get,config,generation,uncertainty);if(lease_)queries_=&queries;
    }
    bool enabled()const noexcept{return queries_&&queries_->current(lease_,generation_);}
    // The containing Frame::initialize is the sole outside-rendering reset site.
    bool reset(VkCommandBuffer command,Clock::time_point start)noexcept{
        if(!enabled()||command!=command_||reset_recorded_||harvested_)return false;
        if(!queries_->reset(command,lease_,generation_))return false;
        start_=start;reset_recorded_=true;return true;
    }
    bool begin(VkCommandBuffer command,NativeMToonProfileStage stage)noexcept{
        const auto n=static_cast<unsigned>(stage);
        if(!enabled()||command!=command_||!reset_recorded_||harvested_||n>=native_mtoon_profile_stages||(begun_&(1u<<n)))return false;
        if(!queries_->write(command,n*2,lease_,generation_))return false;begun_|=1u<<n;return true;
    }
    bool end(VkCommandBuffer command,NativeMToonProfileStage stage)noexcept{
        const auto n=static_cast<unsigned>(stage);
        if(!enabled()||command!=command_||!reset_recorded_||harvested_||n>=native_mtoon_profile_stages||!(begun_&(1u<<n))||(ended_&(1u<<n)))return false;
        if(!queries_->write(command,n*2+1,lease_,generation_))return false;ended_|=1u<<n;return true;
    }
    NativeMToonProfileResult after_original_completion(Clock::time_point now)noexcept{
        NativeMToonProfileResult out;out.requested=config_.enabled;out.enabled=enabled();out.reset_recorded=reset_recorded_;
        out.generation=generation_;out.config=config_;out.written_mask=ended_;
        if(!out.enabled||!reset_recorded_||!ended_||harvested_)return out;
        harvested_=true;
        const auto age=std::chrono::duration<long double,std::nano>(now-start_).count();
        const auto half_wrap=std::ldexp(static_cast<long double>(config_.timestamp_period_ns),int(config_.timestamp_valid_bits)-1);
        out.age_valid=std::isfinite(age)&&age>=0&&std::isfinite(half_wrap)&&half_wrap>0&&age<half_wrap;
        if(!out.age_valid)return out;
        NativeMToonProfileQueries::Results values{};out.read_attempted=true;out.query_result=queries_->read(values,lease_,generation_);
        if(out.query_result!=VK_SUCCESS&&out.query_result!=VK_NOT_READY)return out;
        const auto bits=config_.timestamp_valid_bits;
        const auto mask=bits==64?std::numeric_limits<std::uint64_t>::max():(std::uint64_t(1)<<bits)-1;
        for(unsigned n=0;n<native_mtoon_profile_stages;++n){
            if(!(ended_&(1u<<n)))continue;const auto& begin=values[n*2];const auto& end=values[n*2+1];
            if(!begin.availability||!end.availability||(begin.ticks&~mask)||(end.ticks&~mask))continue;
            const auto delta=(end.ticks-begin.ticks)&mask;
            const auto ns=static_cast<long double>(delta)*config_.timestamp_period_ns;
            const auto ms=static_cast<double>(ns/1e6L);
            if(!std::isfinite(ns)||ns>=half_wrap||!std::isfinite(ms))continue;
            out.milliseconds[n]=ms;out.valid_mask|=1u<<n;
        }
        out.available=out.valid_mask!=0;return out;
    }
private:
    NativeMToonProfileQueries* queries_=nullptr;NativeMToonProfileConfig config_;
    VkCommandBuffer command_{};std::uint64_t lease_=0;std::uint32_t generation_=0,begun_=0,ended_=0;
    Clock::time_point start_{};bool reset_recorded_=false,harvested_=false;
};
}
