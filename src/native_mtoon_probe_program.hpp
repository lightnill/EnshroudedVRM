#pragma once
#include "native_mtoon_probe_copy.hpp"
#include "native_state.hpp"
namespace evrm {
struct NativeMToonProbeProgramShape {
    std::vector<NativeDescriptorDeclaration> coefficients,classifier;
};
// Four supported visibility combinations; UINT32_MAX means unsupported shape.
std::uint32_t native_mtoon_probe_program_shape_key(const NativeMToonProbeProgramShape&)noexcept;
// Immutable owned program/layouts. Construction consumes scalar declarations
// and owned shader bytes only, so it can run on the optional preparation worker
// without retaining native descriptor layouts/sets or reading game resources.
class NativeMToonProbeProgram {
public:
    NativeMToonProbeProgram(VkDevice,PFN_vkGetDeviceProcAddr,NativeMToonProbeProgramShape,
        const NativeMToonProbeCopyCapabilities&,const std::uint32_t*,std::size_t);
    ~NativeMToonProbeProgram();
    NativeMToonProbeProgram(const NativeMToonProbeProgram&)=delete;
    NativeMToonProbeProgram& operator=(const NativeMToonProbeProgram&)=delete;
    bool matches(const NativeMToonProbeProgramShape&)const noexcept;
    VkDevice device()const noexcept;
    VkPipeline pipeline()const noexcept;
    VkPipelineLayout layout()const noexcept;
    VkDescriptorSetLayout output_layout()const noexcept;
    // Serialize adoption with recording. Unpublished programs have no GPU
    // consumers and can be retired on the worker; published ones retain this
    // parent flag through all original-command consumers and final teardown.
    bool adopt(const bool* parent_quarantine)noexcept;
private:
    struct Impl;std::unique_ptr<Impl> impl;
};
} // namespace evrm
