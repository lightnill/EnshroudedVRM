#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/standalone_startup.hpp"
#include "../src/native_owner_process.hpp"
#include "../src/native_owner_profile.hpp"
#include <array>
#include <cstring>
#include <iostream>
#include <thread>

int wmain(int argc,wchar_t** argv) {
    SetErrorMode(GetErrorMode()|0x8003);
    // The complete verified-build string and old activation flags grant no
    // authority to this unrelated/synthetic program.
    std::wstring digest;const wchar_t hex[]=L"0123456789abcdef";
    for(const auto byte:evrm::native_owner_profile::image_sha256){digest+=hex[byte>>4];digest+=hex[byte&15];}
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_VERIFIED_EXE_SHA256",digest.c_str());
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_NATIVE_REPLACEMENT",L"1");
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_VOICE_ENABLED",L"1");
    std::array<const evrm::RuntimeStartupContext*,16> values{};std::array<std::thread,16> threads;
    for(std::size_t index=0;index<threads.size();++index)threads[index]=std::thread([&,index]{values[index]=&evrm::runtime_startup_context();});
    for(auto& thread:threads)thread.join();
    if(!values[0] || values[0]->ready() || values[0]->diagnostic_owner_verified || values[0]->lease || !values[0]->state_directory.empty())return 10;
    for(const auto* value:values)if(value!=values[0])return 11;
    IMAGE_DOS_HEADER dos{};dos.e_magic=IMAGE_DOS_SIGNATURE;dos.e_lfanew=64;
    IMAGE_NT_HEADERS64 nt{};nt.Signature=IMAGE_NT_SIGNATURE;nt.FileHeader.Machine=IMAGE_FILE_MACHINE_AMD64;
    nt.FileHeader.NumberOfSections=1;nt.FileHeader.TimeDateStamp=evrm::native_owner_profile::image_timestamp;
    nt.FileHeader.SizeOfOptionalHeader=sizeof(IMAGE_OPTIONAL_HEADER64);nt.OptionalHeader.Magic=IMAGE_NT_OPTIONAL_HDR64_MAGIC;
    nt.OptionalHeader.ImageBase=0x140000000;nt.OptionalHeader.SizeOfImage=evrm::native_owner_profile::image_size;nt.OptionalHeader.SizeOfHeaders=4096;
    evrm::detail::NativeOwnerHeaderBytes disk{};std::memcpy(disk.dos.data(),&dos,sizeof dos);std::memcpy(disk.nt.data(),&nt,sizeof nt);
    auto loaded=disk;nt.OptionalHeader.ImageBase=0x150000000;std::memcpy(loaded.nt.data(),&nt,sizeof nt);std::string reason;
    if(!evrm::detail::match_native_owner_headers(disk,loaded,0x150000000,reason))return 12;
    loaded.nt[40]^=1;if(evrm::detail::match_native_owner_headers(disk,loaded,0x150000000,reason))return 13;
    if(argc==2) {
        evrm::NativeOwnerImage disk_only;
        if(!evrm::detail::verify_native_owner_executable(argv[1],disk_only,reason) || disk_only.base || values[0]->ready())return 14;
        std::cout<<"explicit disk fixture verified supported bytes without granting loaded-image authority\n";
    }
    std::cout<<"immutable production context declined inherited flags; loaded/disk disagreement declined\n";
    return 0;
}
