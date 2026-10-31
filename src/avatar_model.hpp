#pragma once
#include "native_skinning.hpp"
#include <filesystem>
namespace evrm {
struct PreparedAvatar {
    std::string path,fingerprint,name;
    std::shared_ptr<const vrm::Avatar> asset;
    std::shared_ptr<const NativeSkinAsset> skin;
    std::vector<std::string> material_names;
};
struct AvatarLoadResult {
    std::shared_ptr<const PreparedAvatar> model;std::string error;
    explicit operator bool()const noexcept{return model&&model->asset&&model->skin&&bool(*model->skin)&&error.empty();}
};
bool normalize_avatar_path(const std::string&,std::string&,bool require_file=true)noexcept;
AvatarLoadResult prepare_avatar_bytes(const std::vector<std::uint8_t>&,const std::string&)noexcept;
AvatarLoadResult load_prepared_avatar(const std::string&)noexcept;
}
