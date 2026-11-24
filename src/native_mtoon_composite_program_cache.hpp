#pragma once
#include "native_mtoon_composite.hpp"
#include <vector>
namespace evrm {
enum class NativeMToonCompositeProgramStatus {unsupported,pending,ready,failed};
struct NativeMToonCompositePreparedProgram {
    NativeMToonCompositeProgramStatus status=NativeMToonCompositeProgramStatus::unsupported;
    std::shared_ptr<const NativeMToonCompositeProgram> program;
};
// One bounded source-free job per native device. Caller serializes request and
// shutdown, keeps device/parent alive, and shuts down before device destruction.
// Neither constructor nor poll calls the Vulkan driver. The worker never reads
// the parent's mutable quarantine bool; uncertain completion is withdrawn by
// the serialized owner and propagated through the program's atomic quarantine.
class NativeMToonCompositeProgramCache {
public:
    NativeMToonCompositeProgramCache(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonCompositeCapabilities&,
        std::vector<std::uint32_t>,const bool* parent_quarantine);
    ~NativeMToonCompositeProgramCache();
    NativeMToonCompositeProgramCache(const NativeMToonCompositeProgramCache&)=delete;
    NativeMToonCompositeProgramCache& operator=(const NativeMToonCompositeProgramCache&)=delete;
    NativeMToonCompositePreparedProgram request()noexcept;
    void shutdown(bool quarantine)noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
