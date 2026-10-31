#include "local_voice_copy.hpp"
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <excpt.h>
#include <algorithm>
#include <limits>
#if !defined(__GNUC__) || !defined(__x86_64__)
#error Local voice copy requires the supported Windows x64 GCC toolchain
#endif
extern "C" __attribute__((used)) int evrm_voice_copy_filter(EXCEPTION_POINTERS* info,void*)noexcept {
    if(!info||!info->ExceptionRecord)return EXCEPTION_CONTINUE_SEARCH;
    const auto code=info->ExceptionRecord->ExceptionCode;
    return code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_IN_PAGE_ERROR||code==EXCEPTION_GUARD_PAGE?
        EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH;
}
namespace evrm {
namespace {
// Keep exactly one compiler-stable scope around the read instruction. The
// surrounding caller, encoder and unrelated exceptions are outside this scope.
__attribute__((noinline,noclone,optimize("O0"))) bool contained_copy(void* destination,const void* source,std::size_t size)noexcept {
    volatile bool complete=false;
    __try1(evrm_voice_copy_filter)
    __asm__ __volatile__("rep movsb" : "+D"(destination),"+S"(source),"+c"(size) : : "memory");
    complete=true;
    __except1
    return complete;
}
bool readable(std::uintptr_t at,std::uintptr_t end,VoiceCopyTimings* timings)noexcept {
    while(at<end){MEMORY_BASIC_INFORMATION memory{};if(timings)++timings->queries;
        if(!VirtualQuery(reinterpret_cast<const void*>(at),&memory,sizeof memory)||memory.State!=MEM_COMMIT||
            (memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto protection=memory.Protect&0xff;
        if(protection!=PAGE_READONLY&&protection!=PAGE_READWRITE&&protection!=PAGE_WRITECOPY&&
            protection!=PAGE_EXECUTE_READ&&protection!=PAGE_EXECUTE_READWRITE&&protection!=PAGE_EXECUTE_WRITECOPY)return false;
        const auto base=reinterpret_cast<std::uintptr_t>(memory.BaseAddress);
        if(memory.RegionSize>std::numeric_limits<std::uintptr_t>::max()-base)return false;
        const auto next=base+memory.RegionSize;if(next<=at)return false;at=std::min(end,next);
    }
    return true;
}
}
bool copy_local_voice_bytes(const void* source,void* destination,std::size_t size,const VoiceClock* clock,VoiceCopyTimings* timings)noexcept {
    const auto address=reinterpret_cast<std::uintptr_t>(source);
    if(!address||!destination||!size||size>1920||size>std::numeric_limits<std::uintptr_t>::max()-address)return false;
    const bool measured=clock&&timings;const auto start=measured?clock->ticks():0;
    const bool valid=readable(address,address+size,timings);
    const auto checked=measured?clock->ticks():0;
    if(measured)timings->validation_ns+=counter_duration_ns(start,checked,clock->frequency());
    if(!valid)return false;
    if(timings)++timings->copies;
    const auto copied=contained_copy(destination,source,size);
    if(measured)timings->copy_ns+=counter_duration_ns(checked,clock->ticks(),clock->frequency());
    return copied;
}
bool copy_borrowed_voice_bytes(const void* source,void* destination,std::size_t size,const VoiceClock* clock,VoiceCopyTimings* timings)noexcept {
    const auto address=reinterpret_cast<std::uintptr_t>(source);
    if(!address||!destination||!size||size>1920||size>std::numeric_limits<std::uintptr_t>::max()-address)return false;
    const auto start=clock&&timings?clock->ticks():0;
    if(timings)++timings->copies;
    const bool copied=contained_copy(destination,source,size);
    if(clock&&timings)timings->copy_ns+=counter_duration_ns(start,clock->ticks(),clock->frequency());
    return copied;
}
}
