#pragma once
#ifndef VK_NO_PROTOTYPES
#define VK_NO_PROTOTYPES
#endif
#include "device_features.hpp"
#include "native_mtoon_main.hpp"
#include "native_mtoon_work.hpp"
#include "native_mtoon_outline.hpp"
#include "native_draw.hpp"
#include <vector>
namespace evrm {
enum class NativeMToonRasterFamily {authored,fallback,ordinary,enhanced};
struct NativeMToonRasterProgramCapabilities {
    std::uint32_t api_version=0;VkPhysicalDeviceLimits limits{};DeviceFeaturesObservation enabled;
};
struct NativeMToonRasterProgramShaders {
    // vs_native, ps_visibility, ps_gbuffer, independently compiled per family.
    std::array<std::array<std::vector<std::uint32_t>,3>,4> main;
    // vs_outline, ps_outline_visibility, ps_outline_gbuffer; created on demand.
    std::array<std::vector<std::uint32_t>,3> outline;
    std::array<std::vector<std::uint32_t>,4> work; // init,bounds,arguments,finish
    std::array<std::vector<std::uint32_t>,2> forward; // optional owned transparent capture,vs/ps
    std::array<std::vector<std::uint32_t>,2> shadow; // optional main caster,vs/ps_shadow
};
// Immutable source-free fixed-layout raster stages. Preparation owns no native
// sources, frame buffers, descriptor sets, queues or scene permits. The parent
// identity is never dereferenced by a worker. The serialized cache/owner joins
// workers and propagates uncertain completion before releasing the device.
// A plain program creates no outline modules/pipelines. A later outline program
// shares its immutable core without changing existing in-flight plain programs.
class NativeMToonRasterProgram {
public:
    ~NativeMToonRasterProgram();
    NativeMToonRasterProgram(const NativeMToonRasterProgram&)=delete;
    NativeMToonRasterProgram& operator=(const NativeMToonRasterProgram&)=delete;
    bool reusable()const noexcept;
    bool has_outlines()const noexcept;
    bool has_shadows()const noexcept;
    bool has_forward()const noexcept;
    bool belongs_to(VkDevice,const bool* parent_quarantine)const noexcept;
    void quarantine()const noexcept;
    std::array<VkDescriptorSetLayout,3> descriptor_layouts()const noexcept;
    VkPipelineLayout pipeline_layout()const noexcept;
    std::shared_ptr<const NativeMToonWorkProgram> work_program()const noexcept;
    // Parent owns/retains exact compatible descriptors, dynamic native raster
    // state and complete native restore prefix. These calls do not admit a
    // scene, allocate, compile, submit, wait, transition or change push data.
    bool record(VkCommandBuffer,bool gbuffer,NativeMToonRasterFamily,const NativeMToonMainCall*,
        std::uint32_t,VkPipeline original,VkPipelineLayout compatible,std::uint32_t native_set_count,const VkDescriptorSet*)const noexcept;
    bool record_forward(VkCommandBuffer,const NativeMToonMainCall*,std::uint32_t,
        VkPipeline original,VkPipelineLayout compatible,std::uint32_t native_set_count,const VkDescriptorSet*)const noexcept;
    bool record_shadow(VkCommandBuffer,VkDescriptorSet light,const NativeMToonMainCall*,std::uint32_t,
        VkPipeline original,VkPipelineLayout compatible,std::uint32_t native_set_count,const VkDescriptorSet*)const noexcept;
    bool record_outline(VkCommandBuffer,bool gbuffer,const NativeMToonOutlineCall*,std::uint32_t,
        VkPipeline original,VkPipelineLayout compatible,std::uint32_t native_set_count,const VkDescriptorSet*)const noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
    NativeMToonRasterProgram(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonRasterProgramCapabilities&,
        const NativeMToonRasterProgramShaders&,bool outlines,bool shadows,const bool* parent_quarantine,
        const std::shared_ptr<const NativeMToonRasterProgram>& seed);
    friend class NativeMToonRasterProgramCache;
};
enum class NativeMToonRasterProgramStatus {unsupported,pending,ready,failed};
struct NativeMToonRasterPreparedProgram {
    NativeMToonRasterProgramStatus status=NativeMToonRasterProgramStatus::unsupported;
    std::shared_ptr<const NativeMToonRasterProgram> program;
};
// One source-free job at a time. Polling never enters the Vulkan driver. Caller
// serializes request/shutdown and keeps device/parent alive through shutdown.
class NativeMToonRasterProgramCache {
public:
    NativeMToonRasterProgramCache(VkDevice,PFN_vkGetDeviceProcAddr,const NativeMToonRasterProgramCapabilities&,
        NativeMToonRasterProgramShaders,const bool* parent_quarantine);
    ~NativeMToonRasterProgramCache();
    NativeMToonRasterPreparedProgram request(bool outlines,bool shadows=false)noexcept;
    void shutdown(bool quarantine)noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
}
