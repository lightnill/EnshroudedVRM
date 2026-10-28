#pragma once

#include "vrm_asset.hpp"
#include <memory>

namespace vrm {

// Engine-independent semantic joints, both expressed globally in the same
// source model coordinates. "hips" is required; other missing joints retain
// the avatar's local bind transform and follow their animated parent.
struct SourcePose {
    std::map<std::string, Matrix> rest;
    std::map<std::string, Matrix> current;
};

struct RetargetOptions {
    // Explicit source-model to avatar-model orthonormal basis. Reflections are
    // permitted for handedness conversion; translation and scale are not.
    // The caller chooses this from its source convention AND Avatar::version.
    // No implicit VRM 0/1 front-direction flip is applied by retarget().
    Matrix source_to_avatar_basis{1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1};
    // Consistent positive rest-height measures in each model's own units.
    // Their ratio scales only source hips displacement, never avatar bones.
    float source_rest_height = 1;
    float avatar_rest_height = 1;
    // Opt in to matching authored rest directions (for example A-pose source
    // arms and T-pose avatar arms) through semantic joint-to-child vectors.
    bool align_rest_directions = false;
};

struct SkinPalette {
    int mesh_node = -1;
    int skin = -1;
    // Column-major mesh-local matrices:
    // inverse(node_world[mesh_node]) * node_world[joint] * inverse_bind[joint].
    // A renderer skins vertices with these, then applies node_world[mesh_node]
    // and its separate avatar-model-to-game-world transform exactly once.
    std::vector<Matrix> matrices;
};

struct RetargetResult {
    // One avatar-model global matrix for every node, including unskinned nodes.
    std::vector<Matrix> node_world;
    // One palette per active skinned mesh node; a shared skin can appear more
    // than once because each mesh node has its own inverse mesh transform.
    std::vector<SkinPalette> palettes;
    std::string error;
    explicit operator bool() const { return error.empty(); }
};

// Global rotation delta contract:
// D = B * R_source_current * inverse(R_source_rest) * inverse(B)
// R_avatar_current = D * Q0 * R_avatar_rest.
// Q0 is identity by default. align_rest_directions computes its minimal swing
// from avatar rest joint->child to B*(source rest joint->same semantic child),
// using the nearest shared anatomical descendant across optional joint gaps.
// Hips keeps identity Q0: a branching pelvis has no long-bone axis inferred
// from its spine attachment offset. Authored pelvis pitch and bind axes stay
// intact, while the hips' own global source delta and displacement still apply.
// Finger segments use shared same-finger descendants. A terminal finger uses
// its nearest measured incoming same-finger segment for Q0, preserving its own
// global source delta rather than copying its parent's animated orientation.
// Feet inherit Q0 from the nearest measured actual leg ancestor; toes inherit
// from the measured foot or nearest leg. Their authored ankle/toe rest angles
// survive flat-foot source calibration, while each retains its own motion delta.
// Missing/zero-length segment measurements keep identity Q0; head is unaligned.
// Only measured middle-finger proximal endpoints calibrate hands. Longitudinal
// twist remains the avatar's bind twist, followed by the source rotation delta.
// R denotes the proper rotation factor from polar decomposition. Source scale
// is removed; target hierarchy-derived scale/stretch is retained while rotating.
// This cancels arbitrary source bind axes, retains target bind axes, and
// transfers motion axes in the explicitly calibrated model coordinate basis.
// Bone positions follow avatar local offsets; only hips translation transfers.
// Validated VRMC_node_constraint 1.0 aim/roll/rotation constraints run afterward
// in local/world dependency order, before palettes. Their weights slerp from
// destination rest, and descendants retain their authored retargeted locals.
// No IK, inferred longitudinal twist calibration, or spring motion.
// Invalid input returns an error with no partial pose or palette.
RetargetResult retarget(const Avatar& avatar, const SourcePose& source,
                        const RetargetOptions& options = {});

// Repeated-pose evaluator with an immutable rig-metadata snapshot. Geometry and
// textures are not copied. Validated preparation is reused only while source
// rest bytes, current semantic availability and options match exactly. Every
// dynamic source matrix is still validated. Call from one serialized view.
class Retargeter {
public:
    explicit Retargeter(const Avatar& avatar);
    ~Retargeter();
    Retargeter(const Retargeter&) = delete;
    Retargeter& operator=(const Retargeter&) = delete;
    RetargetResult update(const SourcePose& source, const RetargetOptions& options = {});
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace vrm
