#pragma once
#include <memory>
#include <string>
namespace evrm {
struct NativePoseOwner;
struct NativePlayerPoseOwner {
    std::string subject;
    std::shared_ptr<const NativePoseOwner> owner;
};
}
