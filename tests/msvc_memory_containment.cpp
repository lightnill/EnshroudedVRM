#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "../src/local_voice_copy.hpp"
#include "../src/native_visual_slot_write.hpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

#if !defined(_MSC_VER) || !defined(_M_X64)
#error This fixture verifies the Windows x64 MSVC containment implementation.
#endif
#ifndef EVRM_TEST_WRAP_VirtualQuery
#error This fixture requires the scoped VirtualQuery failure-injection seam.
#endif

extern "C" bool evrm_visual_slot_copy_leaf(void*,const void*,std::size_t) noexcept;

namespace {
unsigned checks=0;
void need(bool value,const char* why){
    ++checks;
    if(!value){std::fprintf(stderr,"FAIL %s (check %u)\n",why,checks);std::exit(1);}
}
const void* flip_after_query=nullptr;
DWORD raise_after_query=0;
SIZE_T WINAPI observed_query(LPCVOID source,PMEMORY_BASIC_INFORMATION output,SIZE_T size){
    const auto result=VirtualQuery(source,output,size);
    if(result&&source==flip_after_query){
        flip_after_query=nullptr;
        if(raise_after_query){const auto code=raise_after_query;raise_after_query=0;RaiseException(code,0,0,nullptr);}
        else {DWORD previous=0;need(VirtualProtect(const_cast<void*>(source),4096,PAGE_NOACCESS,&previous)!=FALSE,"source protection race");}
    }
    return result;
}
constexpr DWORD unrelated_exception=0xe0425652;
const void* watched_guard=nullptr;
DWORD exception_during_copy=unrelated_exception;
LONG CALLBACK raise_during_copy(EXCEPTION_POINTERS* info){
    if(info&&info->ExceptionRecord&&info->ExceptionRecord->ExceptionCode==EXCEPTION_GUARD_PAGE&&watched_guard){
        watched_guard=nullptr;
        RaiseException(exception_during_copy,0,0,nullptr);
    }
    return EXCEPTION_CONTINUE_SEARCH;
}
using Operation=bool(*)(void*,const void*,std::size_t);
__declspec(noinline) DWORD exception_from(Operation operation,void* destination,const void* source,std::size_t size){
    __try {operation(destination,source,size);}
    __except(EXCEPTION_EXECUTE_HANDLER){return GetExceptionCode();}
    return 0;
}
bool local_copy(void* destination,const void* source,std::size_t size){return evrm::copy_local_voice_bytes(source,destination,size);}
bool borrowed_copy(void* destination,const void* source,std::size_t size){return evrm::copy_borrowed_voice_bytes(source,destination,size);}
bool visual_copy(void* destination,const void* source,std::size_t size){return evrm_visual_slot_copy_leaf(destination,source,size);}
void protect(void* address,std::size_t size,DWORD protection){
    DWORD previous=0;need(VirtualProtect(address,size,protection,&previous)!=FALSE,"fixture protection");
}
void unchanged(const std::array<unsigned char,96>& output,const char* why){
    for(const auto byte:output)need(byte==0xa5,why);
}
}

extern "C" {SIZE_T(WINAPI* __wrap___imp_VirtualQuery)(LPCVOID,PMEMORY_BASIC_INFORMATION,SIZE_T)=&observed_query;}

int main(){
    SetErrorMode(SEM_NOGPFAULTERRORBOX|SEM_FAILCRITICALERRORS);
    SYSTEM_INFO system{};GetSystemInfo(&system);const auto page=system.dwPageSize;
    need(page>=4096,"supported Windows base page");
    auto* source=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    auto* destination=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    need(source&&destination,"fixture private allocations");
    std::memset(source,0x3c,page*2);std::memset(destination,0xa5,page*2);

    // A successful visual copy changes exactly the selected 48/96 bytes. A
    // non-private or non-writable destination must still be refused publicly.
    for(const auto bytes:{std::size_t(48),std::size_t(96)}){
        std::memset(destination,0xa5,128);
        need(evrm::write_native_visual_slot_data(reinterpret_cast<std::uint64_t>(destination+1),reinterpret_cast<const float*>(source+1),bytes),"unaligned private visual destination");
        need(destination[0]==0xa5&&destination[bytes+1]==0xa5,"visual copy changed adjacent bytes");
        for(std::size_t n=1;n<=bytes;++n)need(destination[n]==0x3c,"visual copy changed source bytes");
    }
    for(const auto bytes:{std::size_t(0),std::size_t(47),std::size_t(49),std::size_t(95),std::size_t(97)})
        need(!evrm::write_native_visual_slot_data(reinterpret_cast<std::uint64_t>(destination),reinterpret_cast<const float*>(source),bytes),"unbounded visual size accepted");
    need(!evrm::write_native_visual_slot_data(0,reinterpret_cast<const float*>(source),48),"null visual output accepted");
    need(!evrm::write_native_visual_slot_data(reinterpret_cast<std::uint64_t>(destination),nullptr,48),"null visual input accepted");
    need(!evrm::write_native_visual_slot_data(0x7ffffffffff0ULL,reinterpret_cast<const float*>(source),48),"visual output crossed user boundary");

    HANDLE mapping=CreateFileMappingW(INVALID_HANDLE_VALUE,nullptr,PAGE_READWRITE,0,page,nullptr);
    need(mapping!=nullptr,"shared fixture mapping");
    auto* shared=static_cast<unsigned char*>(MapViewOfFile(mapping,FILE_MAP_ALL_ACCESS,0,0,page));
    need(shared!=nullptr,"shared fixture view");std::memset(shared,0xa5,96);
    need(!evrm::write_native_visual_slot_data(reinterpret_cast<std::uint64_t>(shared),reinterpret_cast<const float*>(source),96),"mapped destination lost private-provenance guard");
    need(shared[0]==0xa5&&shared[95]==0xa5,"rejected mapped destination changed");
    need(UnmapViewOfFile(shared)!=FALSE&&CloseHandle(mapping)!=FALSE,"shared fixture cleanup");
    protect(destination,page,PAGE_READONLY);
    need(!evrm::write_native_visual_slot_data(reinterpret_cast<std::uint64_t>(destination),reinterpret_cast<const float*>(source),48),"read-only visual destination accepted");
    protect(destination,page,PAGE_READWRITE|PAGE_GUARD);
    need(!evrm::write_native_visual_slot_data(reinterpret_cast<std::uint64_t>(destination),reinterpret_cast<const float*>(source),48),"guarded visual destination accepted");
    MEMORY_BASIC_INFORMATION guarded{};VirtualQuery(destination,&guarded,sizeof guarded);
    need((guarded.Protect&PAGE_GUARD)!=0,"visual validation consumed destination guard");
    protect(destination,page,PAGE_READWRITE);

    // Fault on the final source vector. Reading and writing in interleaved
    // chunks would visibly corrupt output here, even if the fault is caught.
    protect(source+page,page,PAGE_NOACCESS);
    for(const auto bytes:{std::size_t(48),std::size_t(96)}){
        std::array<unsigned char,96> output{};output.fill(0xa5);
        const auto* crossing=source+page-(bytes-16);
        need(!evrm_visual_slot_copy_leaf(output.data(),crossing,bytes),"late visual source fault escaped or reported success");
        unchanged(output,"visual source fault wrote output before completing source");
        std::memset(destination,0xa5,96);
        need(!evrm::write_native_visual_slot_data(reinterpret_cast<std::uint64_t>(destination),reinterpret_cast<const float*>(crossing),bytes),"public visual source fault escaped or reported success");
        for(unsigned n=0;n<96;++n)need(destination[n]==0xa5,"public visual source fault changed output");
    }
    for(const auto bytes:{std::size_t(48),std::size_t(96)})
        need(!evrm_visual_slot_copy_leaf(source+page,destination,bytes),"visual destination fault escaped or reported success");
    protect(source+page,page,PAGE_READWRITE|PAGE_GUARD);
    std::array<unsigned char,96> guarded_output{};guarded_output.fill(0xa5);
    need(!evrm_visual_slot_copy_leaf(guarded_output.data(),source+page,96),"visual source guard fault escaped");
    unchanged(guarded_output,"visual guarded source changed output");
    protect(source+page,page,PAGE_READWRITE);

    std::array<unsigned char,1920> voice{};
    for(const auto bytes:{std::size_t(1),std::size_t(48),std::size_t(96),std::size_t(1920)}){
        voice.fill(0xa5);evrm::VoiceCopyTimings timings;
        need(evrm::copy_local_voice_bytes(source,voice.data(),bytes,nullptr,&timings),"checked voice copy failed");
        need(timings.queries>0&&timings.copies==1,"checked voice copy bypassed validation");
        for(std::size_t n=0;n<bytes;++n)need(voice[n]==0x3c,"checked voice bytes changed");
        voice.fill(0xa5);timings={};
        need(evrm::copy_borrowed_voice_bytes(source,voice.data(),bytes,nullptr,&timings),"borrowed voice copy failed");
        need(timings.queries==0&&timings.copies==1,"borrowed voice path queried pages");
        for(std::size_t n=0;n<bytes;++n)need(voice[n]==0x3c,"borrowed voice bytes changed");
    }
    need(!evrm::copy_borrowed_voice_bytes(source,voice.data(),0)&&!evrm::copy_borrowed_voice_bytes(source,voice.data(),1921),"borrowed size bounds lost");
    need(!evrm::copy_local_voice_bytes(source,voice.data(),0)&&!evrm::copy_local_voice_bytes(source,voice.data(),1921),"checked size bounds lost");
    need(!evrm::copy_local_voice_bytes(reinterpret_cast<const void*>(std::numeric_limits<std::uintptr_t>::max()-8),voice.data(),48),"checked source arithmetic overflow accepted");
    need(!evrm::copy_borrowed_voice_bytes(reinterpret_cast<const void*>(std::numeric_limits<std::uintptr_t>::max()-8),voice.data(),48),"borrowed source arithmetic overflow accepted");
    protect(source+page,page,PAGE_NOACCESS);
    need(!evrm::copy_local_voice_bytes(source+page,voice.data(),48),"checked unreadable source accepted");
    need(!evrm::copy_borrowed_voice_bytes(source+page,voice.data(),48),"borrowed source fault escaped");
    need(!evrm::copy_local_voice_bytes(source,source+page,48),"checked destination fault escaped");
    need(!evrm::copy_borrowed_voice_bytes(source,source+page,48),"borrowed destination fault escaped");
    protect(source+page,page,PAGE_READWRITE|PAGE_GUARD);
    need(!evrm::copy_local_voice_bytes(source+page,voice.data(),48),"checked source guard accepted");
    VirtualQuery(source+page,&guarded,sizeof guarded);
    need((guarded.Protect&PAGE_GUARD)!=0,"checked validation consumed the caller's guard");
    need(!evrm::copy_borrowed_voice_bytes(source+page,voice.data(),48),"borrowed guard fault escaped");
    protect(source+page,page,PAGE_READWRITE);

    flip_after_query=source;
    need(!evrm::copy_local_voice_bytes(source,voice.data(),48),"source race after VirtualQuery escaped local containment");
    need(flip_after_query==nullptr,"source race injection did not execute");
    protect(source,page,PAGE_READWRITE);
    flip_after_query=source;raise_after_query=EXCEPTION_ACCESS_VIOLATION;
    need(exception_from(local_copy,voice.data(),source,48)==EXCEPTION_ACCESS_VIOLATION,"copy scope swallowed a fault raised by validation");

    // Raise a different structured exception while a real guard fault enters
    // the copy leaf. Its filter must search outward, never convert it to false.
    auto* handler=AddVectoredExceptionHandler(1,&raise_during_copy);
    need(handler!=nullptr,"fixture-only vectored injection");
    for(const auto operation:{borrowed_copy,visual_copy}){
        protect(source+page,page,PAGE_READWRITE|PAGE_GUARD);watched_guard=source+page;
        need(exception_from(operation,voice.data(),source+page,48)==unrelated_exception,"copy leaf swallowed unrelated structured exception");
        need(watched_guard==nullptr,"in-leaf unrelated exception injection did not execute");
        // Exercise the in-page status through the same leaf scope without an
        // unreliable external disk/device failure in this local fixture.
        exception_during_copy=EXCEPTION_IN_PAGE_ERROR;
        protect(source+page,page,PAGE_READWRITE|PAGE_GUARD);watched_guard=source+page;
        need(!operation(voice.data(),source+page,48),"copy leaf failed to contain in-page error");
        need(watched_guard==nullptr,"in-leaf in-page injection did not execute");
        exception_during_copy=unrelated_exception;
    }
    need(RemoveVectoredExceptionHandler(handler)!=0,"fixture-only vectored cleanup");
    need(VirtualFree(source,0,MEM_RELEASE)!=FALSE&&VirtualFree(destination,0,MEM_RELEASE)!=FALSE,"fixture private cleanup");
    std::printf("PASS MSVC scoped memory containment: %u checks; late source faults preserve visual output, private provenance and unrelated exceptions preserved\n",checks);
}
