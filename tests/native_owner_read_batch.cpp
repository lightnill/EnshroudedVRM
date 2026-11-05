#include "native_fixture_compat.hpp"
// Actual verification over owned CPU bytes. No DLL, game or GPU is used.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/native_owner.hpp"
#include "../src/native_owner_profile.hpp"
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <stdexcept>

static std::size_t fail_after=std::numeric_limits<std::size_t>::max(),allocation_failures=0;
EVRM_FIXTURE_NOINLINE void* operator new(std::size_t n){
    if(fail_after!=std::numeric_limits<std::size_t>::max()){
        if(!fail_after--){fail_after=std::numeric_limits<std::size_t>::max();++allocation_failures;throw std::bad_alloc();}
    }
    if(void* p=std::malloc(n?n:1))return p;
    throw std::bad_alloc();
}
void* operator new[](std::size_t n){return ::operator new(n);}
EVRM_FIXTURE_NOINLINE void operator delete(void* p)noexcept{std::free(p);}
EVRM_FIXTURE_NOINLINE void operator delete[](void* p)noexcept{std::free(p);}
EVRM_FIXTURE_NOINLINE void operator delete(void* p,std::size_t)noexcept{std::free(p);}
EVRM_FIXTURE_NOINLINE void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
namespace {
using U=std::uint64_t;
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
struct Memory:evrm::NativeOwnerMemory{
    std::vector<std::uint8_t> data;
    U base=0x10000,fault=0,advance=100;
    mutable U now=100;
    mutable std::size_t calls=0,bytes=0,failures=0,largest=0;
    bool enabled=true,rpm=false,after_first=false,reject_wide=false;
    Memory(std::size_t n):data(n,42){}
    // Without the new optional base capability this compiles but is not called,
    // making the RED failure behavioral rather than a missing-symbol error.
    bool permit_read_ahead(std::size_t,std::size_t)const noexcept{return enabled;}
    bool read(U a,void* out,std::size_t n)const noexcept override{
        ++calls;bytes+=n;largest=std::max(largest,n);
        if(after_first&&calls==1)now=advance;
        if(a<base||n>data.size()||a-base>data.size()-n||(fault&&a<=fault&&fault-a<n)||(reject_wide&&n>52)){++failures;return false;}
        if(rpm){SIZE_T received=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(a),out,n,&received)&&received==n;}
        std::memcpy(out,data.data()+(a-base),n);return true;
    }
    U milliseconds()const noexcept override{return now;}
    void reset(){calls=bytes=failures=largest=0;now=100;}
};
evrm::NativeOwnerCandidate candidate(const Memory& m,std::size_t count,std::size_t width,std::size_t stride,bool reverse=false,bool invalid_later=false){
    evrm::NativeOwnerCandidate c;c.image.base=0x140000000ULL;c.image.size=evrm::native_owner_profile::image_size;
    c.image.timestamp=evrm::native_owner_profile::image_timestamp;
    std::copy(std::begin(evrm::native_owner_profile::image_sha256),std::end(evrm::native_owner_profile::image_sha256),c.image.sha256.begin());
    c.entity=1;c.entity_id=1;c.native_buffer=1;c.native_buffer_bytes=1024;c.models.push_back({1,2,3,4,5});
    std::vector<evrm::NativeOwnerGuard> guards;guards.reserve(count);
    for(std::size_t i=0;i<count;++i)guards.push_back({m.base+i*stride,std::vector<std::uint8_t>(width,42),evrm::NativeOwnerObservationPhase::census});
    if(reverse)std::reverse(guards.begin(),guards.end());
    if(invalid_later)guards[100].address=std::numeric_limits<U>::max();
    c.guards=std::move(guards);return c;
}
}
int main(int argc,char** argv){try{
    SetErrorMode(GetErrorMode()|0x8003);const std::string mode=argc>1?argv[1]:"dense";
    const std::size_t count=mode=="cap"?16385:6500,width=mode=="wide"?700:52,stride=mode=="separate"||mode=="boundary"?4096:mode=="groups-cap"?2048:mode=="wide"?768:64;
    Memory memory(count*stride);if(mode=="boundary")memory.base+=4080;
    auto retained=candidate(memory,count,width,stride,mode=="ordered",mode=="invalid-later");auto c=retained;
    evrm::NativeOwnerLimits limits;limits.max_milliseconds=40;evrm::NativeOwnerObservationStats stats;std::string why;
    bool expected=true;
    if(mode=="disabled")memory.enabled=false;
    else if(mode=="call-limit")limits.max_calls=count;
    else if(mode=="byte-limit")limits.max_bytes=count*width;
    else if(mode=="exhausted"){limits.max_calls=count-1;expected=false;}
    else if(mode=="mismatch"||mode=="ordered"||mode=="invalid-later"){
        const auto at=std::size_t(retained.guards[5].address-memory.base);memory.data[at+17]=0;
        if(mode=="ordered")memory.data[std::size_t(retained.guards[100].address-memory.base)+9]=0;
        expected=false;
    }
    else if(mode=="read-fault"){memory.fault=memory.base+4*stride;expected=false;}
    else if(mode=="gap-fault")memory.fault=memory.base+55;
    else if(mode=="all-gaps"){memory.reject_wide=true;limits.max_calls=7000;}
    else if(mode=="deadline"||mode=="regressed"){memory.after_first=true;memory.advance=mode=="deadline"?140:99;expected=false;}
    else if(mode=="allocation")fail_after=1; // scratch succeeds; optional plan allocation fails.
    else need(mode=="dense"||mode=="fresh"||mode=="cap"||mode=="wide"||mode=="separate"||mode=="boundary"||mode=="groups-cap"||mode=="benchmark","unknown scenario");
    const bool accepted=evrm::revalidate_native_owner(memory,c,why,limits,&stats);fail_after=std::numeric_limits<std::size_t>::max();
    need(accepted==expected,"verification acceptance differs");
    need(stats.attempted_calls==memory.calls&&stats.attempted_bytes==memory.bytes,"physical read progress differs");
    need(stats.completed_calls+memory.failures==stats.attempted_calls,"failed read accounting differs");
    if(expected)need(stats.guard_ordinal==count&&c.guards.size()==count,"verification omitted a guard");
    else need(c.guards.empty()&&c.models.empty(),"failed validation exposed a candidate");
    if(mode=="dense"||mode=="fresh"||mode=="benchmark")need(memory.calls<count/4&&memory.largest<=4096,"nearby guards still use individual reads");
    if(mode=="disabled"||mode=="call-limit"||mode=="byte-limit"||mode=="cap"||mode=="wide"||mode=="separate"||mode=="boundary")need(memory.calls==count,"exact-read fallback changed");
    if(mode=="groups-cap")need(memory.calls<count&&memory.calls>=count-512&&memory.largest<=4096,"batch count/extent cap changed");
    if(mode=="mismatch"||mode=="ordered"||mode=="invalid-later")need(stats.guard_ordinal==5&&stats.guard_difference_offset==17&&stats.guard_capture_phase==evrm::NativeOwnerObservationPhase::census&&stats.failure_location==evrm::NativeOwnerFailureLocation::compare,"first mismatch provenance changed");
    if(mode=="read-fault")need(stats.guard_ordinal==4&&stats.failure_location==evrm::NativeOwnerFailureLocation::read,"first unreadable guard changed");
    if(mode=="gap-fault")need(memory.failures>0&&accepted,"speculative gap failure did not use exact reads");
    if(mode=="all-gaps")need(memory.failures>0&&memory.calls>count&&memory.calls<=limits.max_calls&&accepted,"all-group fallback exceeded its reserved budget");
    if(mode=="exhausted")need(memory.calls==count-1&&stats.guard_ordinal==count-1&&stats.failure_location==evrm::NativeOwnerFailureLocation::budget,"read budget changed");
    if(mode=="deadline"||mode=="regressed")need(memory.calls==1&&stats.failure_location==evrm::NativeOwnerFailureLocation::postcheck&&stats.clock_regressed==(mode=="regressed"),"read deadline changed");
    if(mode=="allocation")need(allocation_failures==1&&memory.calls==count,"optional allocation failure did not use exact reads");
    if(mode=="fresh"){
        memory.reset();memory.data[5*stride+17]=0;c=retained;
        need(!evrm::revalidate_native_owner(memory,c,why,limits,&stats)&&stats.guard_ordinal==5,"verification reused data from an earlier pass");
    }
    if(mode=="benchmark"){
        memory.base=reinterpret_cast<std::uintptr_t>(memory.data.data());retained=candidate(memory,count,width,stride);memory.rpm=true;
        for(bool enabled:{false,true}){
            memory.enabled=enabled;double sum=0,worst=0;std::size_t calls=0,bytes=0;
            for(unsigned run=0;run<30;++run){memory.reset();c=retained;const auto start=std::chrono::steady_clock::now();
                const bool valid=evrm::revalidate_native_owner(memory,c,why,limits,&stats);need(valid,why.c_str());
                const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-start).count();
                if(run>=5){sum+=ms;worst=std::max(worst,ms);calls+=memory.calls;bytes+=memory.bytes;}}
            std::printf("BENCH owner-verification batched=%u mean-ms=%.6f worst-ms=%.6f calls=%zu bytes=%zu guards=%zu\n",enabled?1:0,sum/25,worst,calls/25,bytes/25,count);
        }
    }
    std::printf("PASS native_owner_read_batch %s\n",mode.c_str());return 0;
}catch(const std::exception& e){fail_after=std::numeric_limits<std::size_t>::max();std::fprintf(stderr,"FAIL native_owner_read_batch: %s\n",e.what());return 1;}}
