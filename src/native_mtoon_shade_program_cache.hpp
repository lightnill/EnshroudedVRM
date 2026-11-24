#pragma once
#include "native_mtoon_shade.hpp"
#include <vector>
namespace evrm {
enum class NativeMToonShadeProgramStatus {unsupported,pending,ready,failed};
struct NativeMToonShadePreparedProgram {
    NativeMToonShadeProgramStatus status=NativeMToonShadeProgramStatus::unsupported;
    std::shared_ptr<const NativeMToonShadeProgram> program;
};
// One bounded source-free job per native device. Caller serializes request and
// shutdown, keeps device/parent alive, and shuts down before device destruction.
// Neither constructor nor poll calls the Vulkan driver. The worker never reads
// the parent's mutable quarantine bool; uncertain completion is withdrawn by
// the serialized owner and propagated through the program's atomic quarantine.
class NativeMToonShadeProgramCache {
public:
    NativeMToonShadeProgramCache(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonShadeCapabilities&,
        std::vector<std::uint32_t>,const bool* parent_quarantine);
    ~NativeMToonShadeProgramCache();
    NativeMToonShadeProgramCache(const NativeMToonShadeProgramCache&)=delete;
    NativeMToonShadeProgramCache& operator=(const NativeMToonShadeProgramCache&)=delete;
    NativeMToonShadePreparedProgram request()noexcept;
    void shutdown(bool quarantine)noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
