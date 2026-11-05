#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <filesystem>
#include <string>

namespace evrm::detail {
bool same_standalone_path(const std::filesystem::path&,const std::filesystem::path&);
bool local_standalone_path(const std::filesystem::path&,bool allow_missing=false);
// Read-only handle retains identity and denies writes/deletion. No contents stay
// cached in this owner after startup hashing. Never used under loader lock.
enum class StandaloneReadPolicy { UniqueFile, ImmutablePackage };
class StandaloneFile {
public:
    // ImmutablePackage permits manager hardlinks only at package-validation
    // sites that check expected contents and retain this read lock. User state
    // and diagnostic reads keep the unique-file default.
    explicit StandaloneFile(const std::filesystem::path&,bool directory=false,
                            StandaloneReadPolicy=StandaloneReadPolicy::UniqueFile);
    ~StandaloneFile();
    StandaloneFile(const StandaloneFile&)=delete;
    StandaloneFile& operator=(const StandaloneFile&)=delete;
    std::string read(std::size_t limit)const;
    std::string tail(std::size_t limit)const;
private:
    HANDLE handle_=INVALID_HANDLE_VALUE;
};
}
