#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include <vulkan/vulkan.h>
#include <array>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <memory>

namespace evrm {
struct NativeMToonWorkDraw {std::uint32_t first=0,count=0,selected=0,reserved=0;};
struct NativeMToonWorkParameters {std::uint32_t width=0,height=0,draws=0,budget=0;};
static_assert(sizeof(NativeMToonWorkDraw)==16&&sizeof(NativeMToonWorkParameters)==16);
struct NativeMToonWorkShader {const std::uint32_t* words=nullptr;std::size_t count=0;};
struct NativeMToonWorkCapabilities {VkPhysicalDeviceLimits limits{};VkPhysicalDeviceMemoryProperties memory{};};
// Immutable source-free four-stage program. Prepare on the parent's worker,
// never per original frame. Parent pointer is an identity join only; worker
// construction/destruction never reads that mutable bool. The serialized owner
// withdraws uncertain completion through atomic quarantine before device release.
class NativeMToonWorkProgram {
public:
    NativeMToonWorkProgram(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonWorkCapabilities&,
        const std::array<NativeMToonWorkShader,4>&,const bool* parent_quarantine);
    ~NativeMToonWorkProgram();
    NativeMToonWorkProgram(const NativeMToonWorkProgram&)=delete;
    NativeMToonWorkProgram& operator=(const NativeMToonWorkProgram&)=delete;
    void quarantine()const noexcept;
    bool reusable()const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
    friend class NativeMToonWork;
};
// The four buffers with their memory and the descriptor pool and set of one
// work object. A caller that owns a retained frame slot may keep one here
// between frames: the next work object of that slot reuses it when the device,
// quarantine parent, program and all four buffer sizes are the same, and
// replaces it otherwise. The slot's owner guarantees that the previous work
// object has retired first. Contents are never reused: both mapped buffers are
// rewritten in full, the records are rewritten by cs_init and the summary is
// filled with zero in record(), and all five descriptors are written again.
class NativeMToonWorkStorage;
// Sticky: the storage is abandoned without any Vulkan call.
void native_mtoon_work_storage_quarantine(const std::shared_ptr<NativeMToonWorkStorage>&)noexcept;
// Actual allocation bytes held, or zero.
VkDeviceSize native_mtoon_work_storage_bytes(const std::shared_ptr<NativeMToonWorkStorage>&)noexcept;
// Bounded owned compute preparation, not scene admission. The parent owns the
// identity buffer, serializes recording, proves its initialization/producer and
// keeps it/device/quarantine flag alive through all consumers. Recording changes
// compute bindings (not push constants); the enclosing native renderer restores
// them before returning to the original command stream. No waits/submissions.
class NativeMToonWork {
public:
    static constexpr VkDeviceSize memory_byte_limit=8ull*1024*1024;
    static constexpr std::uint32_t draw_limit=8192,pixel_limit=1u<<24,group_budget_limit=1u<<18;
    NativeMToonWork(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonWorkCapabilities&,VkDescriptorBufferInfo identities,
        NativeMToonWorkParameters,const std::vector<NativeMToonWorkDraw>&,const std::array<NativeMToonWorkShader,4>&,const bool* quarantine,
        std::shared_ptr<const NativeMToonWorkProgram> prepared={},std::shared_ptr<NativeMToonWorkStorage>* retained_slot=nullptr);
    ~NativeMToonWork();
    NativeMToonWork(const NativeMToonWork&)=delete;NativeMToonWork& operator=(const NativeMToonWork&)=delete;
    bool record(VkCommandBuffer)noexcept;
    // Owned ranges after recording, not GPU-completion/readiness receipts.
    VkDescriptorBufferInfo records()const noexcept;
    VkDescriptorBufferInfo summary()const noexcept;
    VkDeviceSize indirect_offset(std::uint32_t draw)const noexcept;
    // Independent source-free program lease; no work/scene readiness receipt.
    std::shared_ptr<const NativeMToonWorkProgram> prepared_program()const noexcept;
    void quarantine()noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
