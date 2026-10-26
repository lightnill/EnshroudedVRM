#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define NTDDI_VERSION 0x0A000002
#include <windows.h>
#include <psapi.h>
#include <array>
#include "native_visual_slot_write.hpp"

#if !defined(__x86_64__) || !defined(__GNUC__)
#error "This bounded copy requires the Windows64 GNU/SEH toolchain."
#endif

// This filter belongs only to the leaf copy's scope table. It never registers
// a global handler or handles exceptions from native/game/C++ callbacks.
extern "C" int evrm_visual_slot_copy_filter(EXCEPTION_POINTERS* info,void*) noexcept {
 if(!info||!info->ExceptionRecord)return EXCEPTION_CONTINUE_SEARCH;
 const auto code=info->ExceptionRecord->ExceptionCode;
 return code==EXCEPTION_ACCESS_VIOLATION||code==EXCEPTION_IN_PAGE_ERROR||
        code==EXCEPTION_GUARD_PAGE?EXCEPTION_EXECUTE_HANDLER:EXCEPTION_CONTINUE_SEARCH;
}
extern "C" bool evrm_visual_slot_copy_leaf(void*,const void*,std::size_t) noexcept;

// Fixed-size leaf, Windows64 ABI: RCX destination, RDX source, R8 byte count.
// Only volatile registers are used; there are no calls, stack changes or C++
// objects to unwind. Read the full source before writing. Windows' ordinary
// per-function exception table sends data faults to the false-return epilogue.
asm(
 ".text\n.p2align 4\n.globl evrm_visual_slot_copy_leaf\n"
 ".def evrm_visual_slot_copy_leaf; .scl 2; .type 32; .endef\n"
 ".seh_proc evrm_visual_slot_copy_leaf\n"
 "evrm_visual_slot_copy_leaf:\n"
 ".seh_handler __C_specific_handler, @except\n.seh_endprologue\n"
 ".Levrm_copy_begin:\n"
 "movdqu 0(%rdx), %xmm0\nmovdqu 16(%rdx), %xmm1\nmovdqu 32(%rdx), %xmm2\n"
 "cmpq $48, %r8\nje .Levrm_copy_store\n"
 "movdqu 48(%rdx), %xmm3\nmovdqu 64(%rdx), %xmm4\nmovdqu 80(%rdx), %xmm5\n"
 "movdqu %xmm3, 48(%rcx)\nmovdqu %xmm4, 64(%rcx)\nmovdqu %xmm5, 80(%rcx)\n"
 ".Levrm_copy_store:\n"
 "movdqu %xmm0, 0(%rcx)\nmovdqu %xmm1, 16(%rcx)\nmovdqu %xmm2, 32(%rcx)\n"
 "movl $1, %eax\n"
 ".Levrm_copy_end:\nret\n"
 ".Levrm_copy_fault:\nxorl %eax, %eax\nret\n"
 ".seh_handlerdata\n.long 1\n"
 ".rva .Levrm_copy_begin, .Levrm_copy_end, evrm_visual_slot_copy_filter, .Levrm_copy_fault\n"
 ".text\n.seh_endproc\n"
);

namespace evrm {
bool write_native_visual_slot_data(std::uint64_t destination,const float* source,std::size_t bytes) noexcept {
 constexpr std::uint64_t user_limit=0x800000000000ULL;
 const auto input=reinterpret_cast<std::uintptr_t>(source);
 if((bytes!=48&&bytes!=96)||!destination||!input||destination>=user_limit||
    input>=user_limit||bytes>user_limit-destination||bytes>user_limit-input)return false;
 // Region metadata proves private allocation provenance without VirtualQuery's
 // potentially large contiguous protection scan. Page queries below inspect
 // only this bounded copy, never the surrounding entity heap.
 const auto end=destination+bytes;
 WIN32_MEMORY_REGION_INFORMATION region{};
 if(!QueryVirtualMemoryInformation(GetCurrentProcess(),reinterpret_cast<const void*>(destination),
    MemoryRegionInfo,&region,sizeof region,nullptr)||!region.Private)return false;
 const auto base=reinterpret_cast<std::uintptr_t>(region.AllocationBase);
 if(base>destination||!region.RegionSize||region.RegionSize>user_limit-base||end>base+region.RegionSize)return false;
 // Windows64 base pages are at least4096bytes, so48/96bytes span at most two.
 // Valid is required before reading protection/Shared/Bad fields. Native success
 // and the caller's complete read have already made these pages resident.
 std::array<PSAPI_WORKING_SET_EX_INFORMATION,2> pages{};
 pages[0].VirtualAddress=reinterpret_cast<void*>(destination);
 pages[1].VirtualAddress=reinterpret_cast<void*>(end-1);
 if(!QueryWorkingSetEx(GetCurrentProcess(),pages.data(),sizeof pages))return false;
 for(const auto& page:pages)if(!page.VirtualAttributes.Valid||page.VirtualAttributes.Bad||
    page.VirtualAttributes.Shared||page.VirtualAttributes.Win32Protection!=PAGE_READWRITE)return false;
 return evrm_visual_slot_copy_leaf(reinterpret_cast<void*>(destination),source,bytes);
}
}
