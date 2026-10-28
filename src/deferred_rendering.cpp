#include "deferred_rendering.hpp"
namespace evrm {
VkRenderingInfo DeferredRenderingInfo::call_view() const noexcept {
    VkRenderingInfo info{};
    info.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    info.flags = flags; info.renderArea = render_area;
    info.layerCount = layer_count; info.viewMask = view_mask;
    info.colorAttachmentCount = color_count;
    info.pColorAttachments = color_count ? colors.data() : nullptr;
    info.pDepthAttachment = has_depth ? &depth : nullptr;
    info.pStencilAttachment = has_stencil ? &stencil : nullptr;
    return info;
}
bool DeferredRenderingBegin::try_defer(const VkRenderingInfo& info, bool external_eligible) noexcept {
    if (!external_eligible || pending_ || info.sType != VK_STRUCTURE_TYPE_RENDERING_INFO || info.pNext ||
        info.flags || info.viewMask || info.layerCount != 1 ||
        !info.renderArea.extent.width || !info.renderArea.extent.height ||
        info.colorAttachmentCount > DeferredRenderingInfo::max_colors ||
        (info.colorAttachmentCount && !info.pColorAttachments)) return false;
    const auto supported = [](const VkRenderingAttachmentInfo& value) noexcept {
        return value.sType == VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO && !value.pNext && value.resolveMode == VK_RESOLVE_MODE_NONE;
    };
    for (std::uint32_t i = 0; i < info.colorAttachmentCount; ++i)
        if (!supported(info.pColorAttachments[i])) return false;
    if ((info.pDepthAttachment && !supported(*info.pDepthAttachment)) ||
        (info.pStencilAttachment && !supported(*info.pStencilAttachment))) return false;
    DeferredRenderingInfo owned;
    owned.flags = info.flags; owned.render_area = info.renderArea;
    owned.layer_count = info.layerCount; owned.view_mask = info.viewMask;
    owned.color_count = info.colorAttachmentCount;
    for (std::uint32_t i = 0; i < info.colorAttachmentCount; ++i) owned.colors[i] = info.pColorAttachments[i];
    owned.has_depth = info.pDepthAttachment != nullptr; owned.has_stencil = info.pStencilAttachment != nullptr;
    if (owned.has_depth) owned.depth = *info.pDepthAttachment;
    if (owned.has_stencil) owned.stencil = *info.pStencilAttachment;
    pending_ = owned;
    return true;
}
std::optional<DeferredRenderingRelease> DeferredRenderingBegin::before(DeferredRenderingCommand command) noexcept {
    if (!pending_ || command == DeferredRenderingCommand::independent_state) return {};
    DeferredRenderingRelease release{*pending_, command == DeferredRenderingCommand::candidate_draw};
    pending_.reset();
    return release;
}
}
