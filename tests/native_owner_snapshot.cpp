#include "../src/native_owner.hpp"
#include "../src/native_owner_profile.hpp"
#include <algorithm>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <new>

static std::atomic<std::size_t> allocated{0};
__attribute__((noinline)) void* operator new(std::size_t n){if(void* p=std::malloc(n?n:1)){++allocated;return p;}throw std::bad_alloc();}
void* operator new[](std::size_t n){return ::operator new(n);}
__attribute__((noinline)) void operator delete(void* p)noexcept{std::free(p);}
__attribute__((noinline)) void operator delete[](void* p)noexcept{std::free(p);}
__attribute__((noinline)) void operator delete(void* p,std::size_t)noexcept{std::free(p);}
__attribute__((noinline)) void operator delete[](void* p,std::size_t)noexcept{std::free(p);}
#define CHECK(x) do{if(!(x)){std::fprintf(stderr,"FAILED: %s at %d\n",#x,__LINE__);return 1;}}while(false)
namespace {
constexpr std::size_t count=6500,width=52,stride=64;
struct Memory:evrm::NativeOwnerMemory{
    std::vector<std::uint8_t> bytes=std::vector<std::uint8_t>(count*stride,42);
    mutable std::size_t reads=0;
    mutable std::uint64_t now=100;
    std::size_t fail_at=count;
    std::uint64_t after_first=100;
    mutable bool in_order=true;
    bool read(std::uint64_t a,void* out,std::size_t n)const noexcept override{
        const auto ordinal=reads++;
        in_order=in_order&&a==0x10000+ordinal*stride&&n==width;
        if(!in_order||ordinal==fail_at)return false;
        std::memcpy(out,bytes.data()+ordinal*stride,n);
        if(!ordinal)now=after_first;
        return true;
    }
    std::uint64_t milliseconds()const noexcept override{return now;}
    void reset(){reads=0;now=100;after_first=100;fail_at=count;in_order=true;}
};
evrm::NativeOwnerCandidate fixture(){
    evrm::NativeOwnerCandidate c;c.image.base=0x140000000ULL;c.image.size=evrm::native_owner_profile::image_size;
    c.image.timestamp=evrm::native_owner_profile::image_timestamp;
    std::copy(std::begin(evrm::native_owner_profile::image_sha256),std::end(evrm::native_owner_profile::image_sha256),c.image.sha256.begin());
    c.entity=c.entity_id=c.native_buffer=1;c.native_buffer_bytes=1024;c.models.push_back({1,2,3,4,5});
    std::vector<evrm::NativeOwnerGuard> guards;guards.reserve(count);
    for(std::size_t i=0;i<count;++i)guards.push_back({0x10000+i*stride,std::vector<std::uint8_t>(width,42),evrm::NativeOwnerObservationPhase::census});
    c.guards=std::move(guards);return c;
}
}
int main(){
    auto retained=fixture();const auto before=allocated.load();auto copy=retained;
    CHECK(allocated.load()-before<=2); // Constant copy work, independent of guard count.
    copy.entity_id=9;copy.models[0].generation=8;CHECK(retained.entity_id==1&&retained.models[0].generation==4);
    Memory memory;std::string why;evrm::NativeOwnerObservationStats s;evrm::NativeOwnerLimits limits;limits.max_milliseconds=40;
    CHECK(evrm::revalidate_native_owner(memory,copy,why,limits,&s));
    CHECK(memory.in_order&&memory.reads==count&&s.attempted_calls==count&&s.completed_calls==count&&s.completed_bytes==count*width&&s.guard_ordinal==count);
    memory.reset();memory.bytes[5*stride+17]=0;copy=retained;
    CHECK(!evrm::revalidate_native_owner(memory,copy,why,limits,&s));
    CHECK(copy.guards.empty()&&retained.guards.size()==count&&s.guard_ordinal==5&&s.guard_difference_offset==17&&s.failure_location==evrm::NativeOwnerFailureLocation::compare);
    memory.bytes[5*stride+17]=42;
    memory.reset();memory.fail_at=4;copy=retained;
    CHECK(!evrm::revalidate_native_owner(memory,copy,why,limits,&s));CHECK(copy.guards.empty()&&s.attempted_calls==5&&s.completed_calls==4&&s.failure_location==evrm::NativeOwnerFailureLocation::read);
    memory.reset();copy=retained;limits.max_calls=3;
    CHECK(!evrm::revalidate_native_owner(memory,copy,why,limits,&s));CHECK(memory.reads==3&&s.guard_ordinal==3&&s.failure_location==evrm::NativeOwnerFailureLocation::budget);
    memory.reset();copy=retained;limits.max_calls=100000;limits.max_bytes=count*width-1;
    CHECK(!evrm::revalidate_native_owner(memory,copy,why,limits,&s));CHECK(memory.reads==count-1&&s.guard_ordinal==count-1);
    limits.max_bytes=64*1024*1024;
    for(auto time:{std::uint64_t(140),std::uint64_t(99)}){
        memory.reset();memory.after_first=time;copy=retained;
        CHECK(!evrm::revalidate_native_owner(memory,copy,why,limits,&s));
        CHECK(copy.guards.empty()&&memory.reads==1&&s.failure_location==evrm::NativeOwnerFailureLocation::postcheck&&s.clock_regressed==(time==99));
    }
    memory.reset();copy=retained;retained={}; // Last consumer keeps bytes after publisher clears.
    CHECK(evrm::revalidate_native_owner(memory,copy,why,limits,&s));CHECK(memory.in_order&&memory.reads==count);
    std::puts("immutable owner snapshots: bounded copy, independent metadata, complete fresh reads and fail-closed limits passed");return 0;
}
