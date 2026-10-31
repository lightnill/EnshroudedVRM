#pragma once
#include "native_material_plan.hpp"
#include <memory>

namespace evrm {
struct NativeSkinVertex {
    vrm::Vertex vertex;
    std::uint32_t batch=0,skinned=0;
};
static_assert(sizeof(NativeSkinVertex)==64,"Skin source ABI");
struct NativeSkinBatch {
    vrm::Matrix world{};
    std::uint32_t base=0,count=0,padding[2]{};
};
static_assert(sizeof(NativeSkinBatch)==80,"Skin batch ABI");
struct NativeSkinBounds {
    std::array<double,3> low{},high{};
    bool used=false;
};
struct NativeSkinNode {
    std::uint32_t node=0;
    int skin=-1;
    std::uint32_t joints=0;
    bool skinned=false;
    NativeSkinBounds static_blend;
    std::vector<NativeSkinBounds> joint_blend;
    double min_weight_sum=1,max_weight_sum=1;
};
struct NativeSkinMouthJoint {std::uint32_t joint=0;NativeSkinBounds bounds;};
struct NativeSkinMouthNode {
    std::uint32_t node=0; // index into the compiled skin node vector
    NativeSkinBounds safety; // all affected vertices, including opaque geometry
    NativeSkinBounds static_blend;
    std::vector<NativeSkinMouthJoint> joints;
};
struct NativeSkinAsset {
    // Initial CPU vertices are retained for the validated layout/reference only.
    // Dynamic publications reuse this object without deforming those vertices.
    std::shared_ptr<const NativeAvatarPose> layout;
    std::vector<NativeSkinVertex> vertices;
    std::vector<NativeSkinNode> nodes;
    std::size_t node_count=0;
    NativeMaterialPlan material_plan;
    std::shared_ptr<const vrm::MouthData> mouth;
    std::vector<std::uint32_t> mouth_lookup;
    std::vector<NativeSkinMouthNode> mouth_nodes;
    std::uint64_t mouth_peak_bytes=0;
    std::string mouth_limitation="No mouth shapes";
    std::string error;
    explicit operator bool()const{return error.empty()&&layout&&!vertices.empty();}
};
struct NativeSkinPose {
    std::shared_ptr<const NativeSkinAsset> asset;
    std::vector<NativeSkinBatch> batches;
    std::vector<vrm::Matrix> matrices;
    NativeMaterialPlan material_plan;
    bool mouth_safe=false;
    std::array<float,4> base_blend_min{},base_blend_max{};
    std::string error;
    explicit operator bool()const{return error.empty()&&asset&&bool(*asset)&&!batches.empty();}
};
// One asset compilation performs full CPU validation. Later pose packing walks
// only transforms/influence boxes, never vertices or indices. Failure is atomic.
NativeSkinAsset make_native_skin_asset(const vrm::Avatar&,const vrm::RetargetResult&)noexcept;
NativeSkinPose make_native_skin_pose(std::shared_ptr<const NativeSkinAsset>,const vrm::RetargetResult&)noexcept;
}
