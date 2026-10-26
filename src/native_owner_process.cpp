#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "native_owner_process.hpp"
#include "native_owner_profile.hpp"
#include "sha256.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <vector>

namespace evrm { namespace {
using U=std::uint64_t;
constexpr U file_bytes=35338312;
constexpr std::size_t header_window=65536;
struct Declined {const char* reason;};
void need(bool ok,const char* reason){if(!ok)throw Declined{reason};}
void set_reason(std::string& out,const char* text)noexcept{try{out.assign(text,std::min<std::size_t>(std::strlen(text),512));}catch(...){out.clear();}}
struct Headers {IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};};
static_assert(sizeof(IMAGE_DOS_HEADER)==64&&sizeof(IMAGE_NT_HEADERS64)==264,"owner header byte seam ABI");
detail::NativeOwnerHeaderBytes header_bytes(const Headers& h){detail::NativeOwnerHeaderBytes b;std::memcpy(b.dos.data(),&h.dos,sizeof h.dos);std::memcpy(b.nt.data(),&h.nt,sizeof h.nt);return b;}
void dos_valid(const IMAGE_DOS_HEADER& dos){
    need(dos.e_magic==IMAGE_DOS_SIGNATURE,"current executable DOS signature mismatch");
    need(dos.e_lfanew>=static_cast<LONG>(sizeof(IMAGE_DOS_HEADER))&&static_cast<U>(dos.e_lfanew)<=header_window-sizeof(IMAGE_NT_HEADERS64),"current executable NT offset outside bounded header window");
}
void headers_valid(const Headers& headers){
    dos_valid(headers.dos);const auto& nt=headers.nt;const auto& file=nt.FileHeader;const auto& optional=nt.OptionalHeader;
    need(nt.Signature==IMAGE_NT_SIGNATURE&&file.Machine==IMAGE_FILE_MACHINE_AMD64&&optional.Magic==IMAGE_NT_OPTIONAL_HDR64_MAGIC,"current executable AMD64/PE32+ signature mismatch");
    need(file.SizeOfOptionalHeader==sizeof(IMAGE_OPTIONAL_HEADER64)&&file.NumberOfSections>0&&file.NumberOfSections<=96,"current executable optional/section dimensions invalid");
    const U section_end=U(headers.dos.e_lfanew)+sizeof(IMAGE_NT_HEADERS64)+U(file.NumberOfSections)*sizeof(IMAGE_SECTION_HEADER);
    need(optional.SizeOfHeaders>=section_end&&optional.SizeOfHeaders<=header_window,"current executable headers exceed bounded window");
    need(file.TimeDateStamp==native_owner_profile::image_timestamp&&optional.SizeOfImage==native_owner_profile::image_size,"current executable timestamp/image size unsupported");
}
void image_range_valid(U base,U size){
    constexpr U limit=0x800000000000ULL;
    need(base>=0x10000&&base<limit&&(base&0xFFFF)==0&&size>0&&size<=limit-base,"executable image base/range invalid");
}
struct File {
    HANDLE handle=INVALID_HANDLE_VALUE;
    ~File(){if(handle!=INVALID_HANDLE_VALUE)CloseHandle(handle);}
    File()=default;File(const File&)=delete;File& operator=(const File&)=delete;
};
NativeOwnerImage verify_disk(const std::filesystem::path& path,const Headers* loaded,U actual_base){
    need(loaded||actual_base==0,"disk-only executable verifier cannot attest a loaded base");
    need(!path.empty()&&path.native().size()<32768,"executable disk path missing or oversized");File file;
    file.handle=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    need(file.handle!=INVALID_HANDLE_VALUE,"executable readonly disk open failed");
    LARGE_INTEGER size{};need(GetFileSizeEx(file.handle,&size)&&size.QuadPart==static_cast<LONGLONG>(file_bytes),"executable disk file size unsupported");
    BY_HANDLE_FILE_INFORMATION before{};need(GetFileInformationByHandle(file.handle,&before)&&!(before.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY),"executable disk identity unavailable");
    std::vector<std::uint8_t> bytes(static_cast<std::size_t>(file_bytes));
    for(std::size_t offset=0;offset<bytes.size();){DWORD wanted=static_cast<DWORD>(std::min<std::size_t>(65536,bytes.size()-offset)),received=0;need(ReadFile(file.handle,bytes.data()+offset,wanted,&received,nullptr)&&received==wanted,"executable disk read incomplete");offset+=received;}
    std::uint8_t extra=0;DWORD received=0;need(ReadFile(file.handle,&extra,1,&received,nullptr)&&received==0,"executable disk size changed");
    BY_HANDLE_FILE_INFORMATION after{};need(GetFileInformationByHandle(file.handle,&after)&&before.dwVolumeSerialNumber==after.dwVolumeSerialNumber&&before.nFileIndexHigh==after.nFileIndexHigh&&before.nFileIndexLow==after.nFileIndexLow&&before.nFileSizeHigh==after.nFileSizeHigh&&before.nFileSizeLow==after.nFileSizeLow&&CompareFileTime(&before.ftLastWriteTime,&after.ftLastWriteTime)==0,"executable disk identity changed during read");
    Headers headers;std::memcpy(&headers.dos,bytes.data(),sizeof headers.dos);dos_valid(headers.dos);std::memcpy(&headers.nt,bytes.data()+headers.dos.e_lfanew,sizeof headers.nt);headers_valid(headers);
    if(loaded){std::string why;need(detail::match_native_owner_headers(header_bytes(headers),header_bytes(*loaded),actual_base,why),"loaded and disk executable headers disagree");}
    const auto digest=detail::sha256(bytes.data(),bytes.size());need(std::equal(digest.begin(),digest.end(),std::begin(native_owner_profile::image_sha256)),"executable disk SHA256 unsupported");
    NativeOwnerImage result;result.size=headers.nt.OptionalHeader.SizeOfImage;result.timestamp=headers.nt.FileHeader.TimeDateStamp;result.sha256=digest;return result;
}
bool same_image(const NativeOwnerImage& a,const NativeOwnerImage& b){return a.base==b.base&&a.size==b.size&&a.timestamp==b.timestamp&&a.sha256==b.sha256;}
} // namespace
NativeOwnerProcess::NativeOwnerProcess()noexcept{
    try{
        const HMODULE module=GetModuleHandleW(nullptr);need(module!=nullptr,"current main module unavailable");const U base=reinterpret_cast<std::uintptr_t>(module);Headers loaded;
        need(read(base,&loaded.dos,sizeof loaded.dos),"current main DOS header unreadable");dos_valid(loaded.dos);
        need(base<=0x800000000000ULL-U(loaded.dos.e_lfanew)&&read(base+U(loaded.dos.e_lfanew),&loaded.nt,sizeof loaded.nt),"current main NT header unreadable");headers_valid(loaded);
        std::array<wchar_t,32768> path{};const DWORD length=GetModuleFileNameW(module,path.data(),static_cast<DWORD>(path.size()));need(length&&length<path.size(),"current main executable filename unavailable or truncated");
        auto verified=verify_disk(std::filesystem::path(path.data()),&loaded,base);verified.base=base;image_=verified;supported_=true;reason_.clear();
    }catch(const Declined& error){set_reason(reason_,error.reason);}catch(...){set_reason(reason_,"current main executable attestation unavailable");}
    if(!supported_)image_={};
}
bool NativeOwnerProcess::read(std::uint64_t address,void* destination,std::size_t count)const noexcept{
    if(!destination||!count||count>65536||address<0x10000||address>=0x800000000000ULL||count>0x800000000000ULL-address)return false;
    SIZE_T received=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),destination,count,&received)!=0&&received==count;
}
std::uint64_t NativeOwnerProcess::milliseconds()const noexcept{return GetTickCount64();}
bool NativeOwnerProcess::capture(NativeOwnerCandidate& candidate,std::string& why,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats)const noexcept{
    if(stats){*stats={};stats->phase=NativeOwnerObservationPhase::adapter;stats->failure_location=NativeOwnerFailureLocation::validation;}
    if(!supported_){candidate={};set_reason(why,reason_.empty()?"current main executable is unsupported":reason_.c_str());return false;}
    try{std::lock_guard<std::mutex> lock(cache_mutex_);return cache_.capture(*this,image_,candidate,why,limits,stats);}
    catch(...){candidate={};set_reason(why,"owner observation synchronization failure");return false;}
}
bool NativeOwnerProcess::revalidate(NativeOwnerCandidate& candidate,std::string& why,const NativeOwnerLimits& limits,NativeOwnerObservationStats* stats)const noexcept{
    if(stats){*stats={};stats->phase=NativeOwnerObservationPhase::adapter;stats->failure_location=NativeOwnerFailureLocation::validation;}
    if(!supported_){candidate={};set_reason(why,reason_.empty()?"current main executable is unsupported":reason_.c_str());return false;}
    if(!same_image(candidate.image,image_)){candidate={};set_reason(why,"owner candidate image disagrees with attested current main");return false;}
    return revalidate_native_owner(*this,candidate,why,limits,stats);
}
bool NativeOwnerProcess::capture_menu(NativeMenuOwnerCandidate& candidate,std::string& why,const NativeOwnerLimits& limits)const noexcept{
    if(!supported_){candidate={};set_reason(why,reason_.empty()?"current main executable is unsupported":reason_.c_str());return false;}
    if(capture_native_menu_owner(*this,image_,candidate,why,limits))return true;
    return capture_native_character_preview_owner(*this,image_,candidate,why,limits);
}
bool NativeOwnerProcess::revalidate_menu(NativeMenuOwnerCandidate& candidate,std::string& why,const NativeOwnerLimits& limits)const noexcept{
    if(!supported_||!same_image(candidate.image,image_)){
        candidate={};set_reason(why,"menu candidate disagrees with attested current main");return false;
    }
    return revalidate_native_menu_owner(*this,candidate,why,limits);
}
namespace detail {
bool match_native_owner_headers(const NativeOwnerHeaderBytes& disk,const NativeOwnerHeaderBytes& loaded,U actual_base,std::string& why)noexcept{
    why.clear();try{
        Headers original,current;std::memcpy(&original.dos,disk.dos.data(),sizeof original.dos);std::memcpy(&original.nt,disk.nt.data(),sizeof original.nt);
        std::memcpy(&current.dos,loaded.dos.data(),sizeof current.dos);std::memcpy(&current.nt,loaded.nt.data(),sizeof current.nt);
        headers_valid(original);headers_valid(current);
        image_range_valid(original.nt.OptionalHeader.ImageBase,original.nt.OptionalHeader.SizeOfImage);
        image_range_valid(actual_base,current.nt.OptionalHeader.SizeOfImage);
        need(current.nt.OptionalHeader.ImageBase==original.nt.OptionalHeader.ImageBase||current.nt.OptionalHeader.ImageBase==actual_base,"loaded executable ImageBase is neither preferred nor trusted actual base");
        // Only this local copy's ImageBase is normalized. All other DOS/NT bytes
        // remain exact, and the disk digest is calculated from original bytes.
        current.nt.OptionalHeader.ImageBase=original.nt.OptionalHeader.ImageBase;
        need(std::memcmp(&original.dos,&current.dos,sizeof original.dos)==0&&std::memcmp(&original.nt,&current.nt,sizeof original.nt)==0,"loaded and disk executable headers disagree");
        return true;
    }catch(const Declined& error){set_reason(why,error.reason);}catch(...){set_reason(why,"executable header comparison unavailable");}return false;
}
bool verify_native_owner_executable(const std::filesystem::path& path,NativeOwnerImage& image,std::string& why)noexcept{
    image={};why.clear();try{image=verify_disk(path,nullptr,0);return true;}catch(const Declined& error){set_reason(why,error.reason);}catch(...){set_reason(why,"executable disk verification unavailable");}image={};return false;
}
}
}
