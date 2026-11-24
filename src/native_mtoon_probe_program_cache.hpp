#pragma once
#include "native_mtoon_probe_program.hpp"
namespace evrm {
enum class NativeMToonProbeProgramStatus {pending,ready,failed,unsupported};
struct NativeMToonProbePreparedProgram {
    NativeMToonProbeProgramStatus status=NativeMToonProbeProgramStatus::unsupported;
    std::shared_ptr<NativeMToonProbeProgram> program;
};
// Caller serializes request/poll and destroys the cache before the device.
// Own preparation uses scalar descriptor definitions and shader bytes only.
// It never receives native resource handles, submits GPU work or waits on it.
class NativeMToonProbeProgramCache {
public:
    NativeMToonProbeProgramCache(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonProbeCopyCapabilities&,std::vector<std::uint32_t>);
    ~NativeMToonProbeProgramCache(); // joins bounded outstanding preparations
    NativeMToonProbeProgramCache(const NativeMToonProbeProgramCache&)=delete;
    NativeMToonProbeProgramCache& operator=(const NativeMToonProbeProgramCache&)=delete;
    NativeMToonProbePreparedProgram request(const NativeMToonProbeProgramShape&)noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
} // namespace evrm
