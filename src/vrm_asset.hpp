#pragma once
#include "mouth_data.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <map>
#include <memory>
#include <string>
#include <vector>

namespace vrm {

// glTF right-handed coordinates, metres, column-major matrices. Front direction
// remains version-dependent; the renderer must use VrmVersion when retargeting.
using Matrix = std::array<float, 16>;
enum class VrmVersion { V0, V1 };
enum class AlphaMode { Opaque, Mask, Blend };
enum class NodeConstraintType { Roll, Aim, Rotation };
enum class ConstraintAxis { X, Y, Z, NegativeX, NegativeY, NegativeZ };

struct NodeConstraint {
    NodeConstraintType type = NodeConstraintType::Rotation;
    int target = -1, source = -1;
    ConstraintAxis axis = ConstraintAxis::X;
    float weight = 1;
};

struct Vertex {
    std::array<float, 3> position{}, normal{};
    std::array<float, 2> uv{};
    std::array<std::uint16_t, 4> joints{};
    std::array<float, 4> weights{};
};
struct Primitive {
    std::vector<Vertex> vertices;
    std::vector<std::uint32_t> indices;
    int material = -1;
    bool skinned = false;
};
struct Mesh { std::vector<Primitive> primitives; };
struct Node {
    std::string name;
    int parent = -1, mesh = -1, skin = -1;
    Matrix local{}, world{};
    bool active = false;
    // Authored glTF child order matters to legacy VRM0 first-child axes.
    std::vector<int> children;
};
struct SpringCollider {
    int node = -1;
    std::array<float,3> offset{}, tail{};
    float radius = 0;
    bool capsule = false;
};
// VRM1 consecutive pair or VRM0 recursive node/first child. child==-1 denotes
// the legacy virtual 7 cm terminal tail. All coordinates are glTF metres.
struct SpringJoint {
    int node = -1, child = -1, center = -1;
    float hit_radius = 0, stiffness = 1, gravity_power = 0, drag = 0.5f;
    std::array<float,3> gravity{0,-1,0};
    std::vector<std::size_t> colliders;
};
struct Skin {
    std::vector<int> joints;
    std::vector<Matrix> inverse_bind;
};
struct Image {
    int width = 0, height = 0;
    std::vector<std::uint8_t> rgba;
};
struct Texture {
    int image = -1;
    int wrap_s = 10497, wrap_t = 10497;
    int min_filter = 9987, mag_filter = 9729;
};
struct Material {
    std::string name;
    std::array<float, 4> base_color{1, 1, 1, 1};
    int base_texture = -1;
    AlphaMode alpha = AlphaMode::Opaque;
    float alpha_cutoff = 0.5f;
    bool double_sided = false, unlit = false;
    std::string mtoon_json; // Preserved; MToon shading is not implemented yet.
};
struct Avatar {
    VrmVersion version = VrmVersion::V1;
    std::string title, license;
    std::vector<std::string> authors, warnings;
    std::vector<Node> nodes;
    std::vector<Mesh> meshes;
    std::vector<Skin> skins;
    std::vector<Image> images;
    std::vector<Texture> textures;
    std::vector<Material> materials;
    std::vector<NodeConstraint> constraints;
    std::vector<SpringCollider> spring_colliders;
    std::vector<SpringJoint> spring_joints;
    std::map<std::string, int> humanoid;
    std::shared_ptr<const MouthData> mouth;
    std::string mouth_limitation="No mouth shapes";
    std::size_t vertex_count = 0, triangle_count = 0;
};

namespace detail {
// Shared importer/runtime validation. Local-rotation dependencies differ from
// world-position dependencies; keeping both steps avoids false ancestor cycles.
struct ConstraintStep { std::size_t node = 0; bool world = false; };
// Throws on malformed metadata or cyclic dependencies. Callers return atomic
// ImportResult/RetargetResult errors; this is not a renderer-facing API.
std::vector<ConstraintStep> node_constraint_order(const Avatar& avatar);
}
struct ImportLimits {
    std::size_t file_bytes = 512 * 1024 * 1024;
    std::size_t json_bytes = 8 * 1024 * 1024, primitives = 4096;
    std::size_t decoded_image_bytes = 256 * 1024 * 1024;
    std::size_t vertices = 2'000'000, indices = 6'000'000;
    // Bounded original storage validation, separate from retained render data.
    std::size_t source_vertex_visits = 16'000'000;
    std::size_t mouth_bindings=max_mouth_bindings,mouth_records=max_mouth_records;
    std::size_t mouth_source_vertex_visits=max_mouth_source_visits,mouth_ranges=max_mouth_ranges;
    std::size_t nodes = 2048, joints_per_skin = 512;
    int image_dimension = 8192;
};
struct ImportResult {
    Avatar avatar;
    std::string error;
    explicit operator bool() const { return error.empty(); }
};

// No game, GPU, network, or external-URI access. Failed imports return an empty
// Avatar, so callers cannot accidentally render a partially validated model.
ImportResult load_vrm_bytes(const std::vector<std::uint8_t>& bytes,
                            const ImportLimits& limits = {});
ImportResult load_vrm(const std::filesystem::path& path,
                     const ImportLimits& limits = {});

} // namespace vrm
