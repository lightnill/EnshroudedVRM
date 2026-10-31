#include "vrm_asset.hpp"

#define TINYGLTF_IMPLEMENTATION
#define TINYGLTF_NO_FS
#define TINYGLTF_NO_EXTERNAL_IMAGE
#define TINYGLTF_NO_STB_IMAGE_WRITE
#define STB_IMAGE_IMPLEMENTATION
#define STBI_ONLY_PNG
#define STBI_ONLY_JPEG
#define STBI_MAX_DIMENSIONS 8192
#if defined(__GNUC__)
#pragma GCC diagnostic push
// TinyGLTF's disabled filesystem branches leave unused callback parameters.
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
#include "../vendor/tinygltf/tiny_gltf.h"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <fstream>
#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>

namespace vrm {
namespace {
using Json = nlohmann::json;
using Bytes = std::vector<std::uint8_t>;
[[noreturn]] void fail(const std::string& message) { throw std::runtime_error(message); }
void require(bool condition, const std::string& message) { if (!condition) fail(message); }
std::uint32_t word(const Bytes& bytes, std::size_t offset) {
    require(offset <= bytes.size() && bytes.size() - offset >= 4, "GLB length is truncated");
    std::uint32_t result; std::memcpy(&result, bytes.data() + offset, 4); return result;
}
std::size_t number(const Json& value) {
    require(value.is_number_integer() && !value.is_boolean(), "Expected a nonnegative integer");
    require(!value.is_number_integer() || value.is_number_unsigned() || value.get<std::int64_t>() >= 0,
            "Expected a nonnegative integer");
    auto result = value.get<std::uint64_t>();
    require(result <= std::numeric_limits<std::size_t>::max(), "Integer exceeds platform limit");
    return static_cast<std::size_t>(result);
}
std::size_t field(const Json& object, const char* name, std::size_t default_value = 0) {
    return object.contains(name) ? number(object.at(name)) : default_value;
}
#include "import_uv.inl"

template<class T> const T& indexed(const std::vector<T>& values, int index, const char* label) {
    require(index >= 0 && static_cast<std::size_t>(index) < values.size(), std::string("Invalid ") + label);
    return values[static_cast<std::size_t>(index)];
}
void span(std::size_t offset, std::size_t count, std::size_t stride,
          std::size_t element, std::size_t length, const char* label) {
    require(offset <= length && element <= length - offset && count > 0 && stride >= element &&
            count - 1 <= (length - offset - element) / stride,
            std::string(label) + " range exceeds bufferView");
}
int component_size(int component) {
    switch (component) {
        case 5120: case 5121: return 1;
        case 5122: case 5123: return 2;
        case 5125: case 5126: return 4;
        default: fail("Unsupported accessor component type");
    }
}
int components(const std::string& type) {
    if (type == "SCALAR") return 1;
    if (type == "VEC2") return 2;
    if (type == "VEC3") return 3;
    if (type == "VEC4" || type == "MAT2") return 4;
    if (type == "MAT3") return 9;
    if (type == "MAT4") return 16;
    fail("Unsupported accessor type");
}
std::size_t element_size(const std::string& type, int component) {
    const auto size = static_cast<std::size_t>(component_size(component));
    if (type == "MAT2" || type == "MAT3" || type == "MAT4") {
        const std::size_t n = type.back() - '0';
        return ((n * size + 3) & ~std::size_t(3)) * n;
    }
    return size * components(type);
}

// Preflight before decoding textures or allocating vertex arrays. The input is
// a self-contained GLB, so no filesystem callbacks or external URIs are needed.
Json preflight(const Bytes& bytes, const ImportLimits& limits) {
    require(bytes.size() <= limits.file_bytes && bytes.size() <= static_cast<std::size_t>(std::numeric_limits<int>::max()),
            "VRM file exceeds size limit");
    require(bytes.size() >= 28 && word(bytes, 0) == 0x46546c67 && word(bytes, 4) == 2,
            "Expected a binary glTF 2.0 GLB");
    require(word(bytes, 8) == bytes.size(), "GLB declared length differs from file length");
    const std::size_t json_size = word(bytes, 12);
    require(word(bytes, 16) == 0x4e4f534a && json_size % 4 == 0 &&
            json_size <= bytes.size() - 28, "Invalid GLB JSON chunk length");
    const std::size_t binary_header = 20 + json_size;
    const std::size_t binary_size = word(bytes, binary_header);
    require(word(bytes, binary_header + 4) == 0x004e4942 && binary_size % 4 == 0 &&
            binary_size == bytes.size() - binary_header - 8, "Invalid GLB binary chunk length");
    require(json_size <= limits.json_bytes, "GLB JSON chunk exceeds size limit");
    std::size_t json_events = 0;
    auto doc = Json::parse(bytes.begin() + 20, bytes.begin() + 20 + json_size,
        [&json_events](int depth, Json::parse_event_t, Json&) {
            require(depth <= 64 && ++json_events <= 500000, "JSON complexity exceeds limit"); return true;
        });
    require(doc.at("asset").at("version") == "2.0", "Unsupported glTF asset version");
    require(doc.at("buffers").size() == 1, "Expected one embedded GLB buffer");
    const auto& buffer = doc.at("buffers").at(0);
    require(!buffer.contains("uri"), "Buffer URI is unsupported; use an embedded VRM");
    const auto declared_size = field(buffer, "byteLength");
    require(declared_size <= binary_size && binary_size - declared_size <= 3, "GLB buffer length mismatch");
    const std::map<std::string, std::size_t> object_limits = {
        {"nodes", limits.nodes}, {"skins", 512}, {"meshes", 1024}, {"materials", 1024},
        {"images", 256}, {"textures", 1024}, {"accessors", 32768}, {"bufferViews", 32768},
        {"samplers", 1024}, {"scenes", 256}, {"animations", 128}, {"cameras", 128}
    };
    for (const auto& entry : object_limits) if (doc.contains(entry.first)) {
        require(doc.at(entry.first).is_array() && doc.at(entry.first).size() <= entry.second,
                entry.first + " exceed object limit");
    }
    for (const auto& ext : doc.value("extensionsRequired", Json::array())) {
        const auto name = ext.get<std::string>();
        require(name == "VRMC_vrm" || name == "VRM" || name == "KHR_materials_unlit" || name == "VRMC_node_constraint" || name == "VRMC_springBone" || name == "KHR_texture_transform",
                "Unsupported required extension: " + name);
    }
    for (const auto& image : doc.value("images", Json::array())) {
        require(!image.contains("uri"), "Image URI is unsupported; use embedded PNG/JPEG");
        require(image.contains("bufferView"), "Image requires a bufferView");
        const auto mime = image.value("mimeType", std::string{});
        require(mime == "image/png" || mime == "image/jpeg", "Unsupported image MIME type");
    }
    const auto& views = doc.at("bufferViews");
    for (const auto& view : views) {
        const auto offset = field(view, "byteOffset"), length = field(view, "byteLength");
        require(field(view, "buffer") == 0 && offset <= declared_size && length <= declared_size - offset,
                "Invalid bufferView bounds");
        if (view.contains("byteStride")) {
            const auto stride = field(view, "byteStride");
            require(stride >= 4 && stride <= 252 && stride % 4 == 0, "Invalid bufferView stride");
        }
    }
    for (const auto& image : doc.value("images", Json::array()))
        require(field(image, "bufferView") < views.size(), "Invalid image bufferView");
    for (const auto& a : doc.at("accessors")) {
        const auto raw_component = field(a, "componentType");
        require(raw_component <= 5126, "Unsupported accessor component type");
        const int component = static_cast<int>(raw_component);
        require(!a.contains("normalized") || a.at("normalized").is_boolean(), "Invalid normalized accessor flag");
        require(!a.value("normalized", false) || (component >= 5120 && component <= 5123), "Invalid normalized accessor component type");
        const auto element = element_size(a.at("type").get<std::string>(), component);
        const auto count = field(a, "count");
        require(count > 0 && count <= std::max(limits.source_vertex_visits, limits.indices), "Accessor count exceeds limit");
        if (a.contains("bufferView")) {
            const auto view_index = field(a, "bufferView");
            require(view_index < views.size(), "Invalid accessor bufferView");
            const auto& view = views.at(view_index);
            const auto offset = field(a, "byteOffset"), stride = field(view, "byteStride", element);
            require(stride >= element && stride % component_size(component) == 0, "Invalid accessor stride");
            require(offset % component_size(component) == 0 &&
                    field(view, "byteOffset") % component_size(component) == 0, "Misaligned accessor");
            span(offset, count, stride, element, field(view, "byteLength"), "accessor");
        } else require(field(a, "byteOffset") == 0, "Accessor offset requires a bufferView");
        if (a.contains("sparse")) {
            const auto& sparse = a.at("sparse");
            const auto sparse_count = field(sparse, "count");
            require(sparse_count > 0 && sparse_count <= count, "Invalid sparse accessor count");
            const auto& indices = sparse.at("indices");
            const auto raw_index_type = field(indices, "componentType");
            require(raw_index_type == 5121 || raw_index_type == 5123 || raw_index_type == 5125, "Invalid sparse index type");
            const int index_type = static_cast<int>(raw_index_type);
            for (const char* key : {"indices", "values"}) {
                const auto& part = sparse.at(key);
                auto view = field(part, "bufferView");
                require(view < views.size() && !views.at(view).contains("byteStride"), "Invalid sparse bufferView");
                const auto width = std::string(key) == "indices" ? component_size(index_type) : element;
                span(field(part, "byteOffset"), sparse_count, width, width,
                     field(views.at(view), "byteLength"), "sparse accessor");
            }
        }
    }
    // TinyGLTF uses int for references. Validate the original integer before it
    // can be narrowed, defaulted, or discarded by that parser.
    auto count_of = [&](const char* key) { return doc.contains(key) ? doc.at(key).size() : std::size_t(0); };
    auto reference = [&](const Json& value, const char* key, const char* label) {
        require(number(value) < count_of(key), std::string("Invalid ") + label + " reference");
    };
    auto optional_reference = [&](const Json& object, const char* member, const char* key, const char* label) {
        if (object.contains(member)) reference(object.at(member), key, label);
    };
    std::size_t edges = 0;
    for (const auto& node : doc.at("nodes")) {
        optional_reference(node, "mesh", "meshes", "node mesh");
        optional_reference(node, "skin", "skins", "node skin");
        require(!node.contains("matrix") || (!node.contains("translation") && !node.contains("rotation") && !node.contains("scale")),
                "Node matrix and TRS must be exclusive");
        const auto children = node.value("children", Json::array());
        require(children.is_array() && children.size() <= limits.nodes - edges, "Node hierarchy edge limit exceeded");
        edges += children.size();
        for (const auto& child : children) reference(child, "nodes", "hierarchy child");
    }
    for (const auto& skin : doc.at("skins")) {
        require(skin.at("joints").is_array() && !skin.at("joints").empty() &&
                skin.at("joints").size() <= limits.joints_per_skin, "Skin joint count exceeds limit");
        for (const auto& joint : skin.at("joints")) reference(joint, "nodes", "skin joint");
        optional_reference(skin, "skeleton", "nodes", "skin skeleton");
        optional_reference(skin, "inverseBindMatrices", "accessors", "inverse bind accessor");
    }
    optional_reference(doc, "scene", "scenes", "default scene");
    for (const auto& scene : doc.at("scenes"))
        for (const auto& node : scene.at("nodes")) reference(node, "nodes", "scene node");
    for (const auto& texture : doc.value("textures", Json::array())) {
        reference(texture.at("source"), "images", "texture image");
        optional_reference(texture, "sampler", "samplers", "texture sampler");
    }
    for (const auto& material : doc.value("materials", Json::array())) {
        if (material.contains("pbrMetallicRoughness") && material.at("pbrMetallicRoughness").contains("baseColorTexture")) {
            const auto& texture = material.at("pbrMetallicRoughness").at("baseColorTexture");
            reference(texture.at("index"), "textures", "base texture");
            validate_base_texture_uv(texture);
        }
    }
    for (const auto& sampler : doc.value("samplers", Json::array())) {
        for (const char* name : {"wrapS", "wrapT"}) if (sampler.contains(name)) {
            const auto wrap = field(sampler, name);
            require(wrap == 33071 || wrap == 33648 || wrap == 10497, "Invalid sampler wrap mode");
        }
        if (sampler.contains("magFilter")) {
            const auto filter = field(sampler, "magFilter");
            require(filter == 9728 || filter == 9729, "Invalid sampler magnification filter");
        }
        if (sampler.contains("minFilter")) {
            const auto filter = field(sampler, "minFilter");
            require(filter == 9728 || filter == 9729 || (filter >= 9984 && filter <= 9987), "Invalid sampler minification filter");
        }
    }
    std::size_t primitive_count = 0, vertex_count = 0, index_count = 0;
    for (const auto& mesh : doc.at("meshes")) {
        require(mesh.contains("primitives") && mesh.at("primitives").is_array() && !mesh.at("primitives").empty(),
                "Mesh requires a primitive array");
        const auto& primitives = mesh.at("primitives");
        require(primitives.size() <= limits.primitives - primitive_count, "Mesh primitive count exceeds limit");
        primitive_count += primitives.size();
        for (const auto& primitive : primitives) {
            require(field(primitive, "mode", 4) == 4, "Unsupported primitive mode; triangle meshes only");
            require(primitive.contains("attributes") && primitive.at("attributes").is_object() &&
                    primitive.at("attributes").contains("POSITION"), "Missing primitive attributes/POSITION");
            for (const auto& attribute : primitive.at("attributes")) reference(attribute, "accessors", "primitive attribute");
            optional_reference(primitive, "indices", "accessors", "primitive indices");
            optional_reference(primitive, "material", "materials", "primitive material");
            const auto accessor = number(primitive.at("attributes").at("POSITION"));
            const auto vertices = field(doc.at("accessors").at(accessor), "count");
            const auto indices = primitive.contains("indices") ? field(doc.at("accessors").at(number(primitive.at("indices"))), "count") : vertices;
            require(vertex_count <= limits.source_vertex_visits &&
                    vertices <= limits.source_vertex_visits - vertex_count && indices <= limits.indices - index_count,
                    "Cumulative geometry exceeds vertex/index limit");
            vertex_count += vertices; index_count += indices;
        }
    }
    return doc;
}

#include "import_images.inl"

template<class T> T scalar(const unsigned char* p) { T v; std::memcpy(&v, p, sizeof v); return v; }
double value_at(const unsigned char* p, int type, bool normalized) {
    switch (type) {
        case 5120: { auto v = scalar<std::int8_t>(p); return normalized ? std::max(-1.0, v / 127.0) : v; }
        case 5121: { auto v = scalar<std::uint8_t>(p); return normalized ? v / 255.0 : v; }
        case 5122: { auto v = scalar<std::int16_t>(p); return normalized ? std::max(-1.0, v / 32767.0) : v; }
        case 5123: { auto v = scalar<std::uint16_t>(p); return normalized ? v / 65535.0 : v; }
        case 5125: return scalar<std::uint32_t>(p);
        case 5126: return scalar<float>(p);
        default: fail("Unsupported accessor component");
    }
}
const unsigned char* view_data(const tinygltf::Model& model, int index, std::size_t offset) {
    const auto& view = indexed(model.bufferViews, index, "bufferView");
    const auto& buffer = indexed(model.buffers, view.buffer, "buffer");
    require(offset <= view.byteLength && view.byteOffset <= buffer.data.size() &&
            view.byteLength <= buffer.data.size() - view.byteOffset, "Invalid bufferView range");
    return buffer.data.data() + view.byteOffset + offset;
}
#include "import_accessor.inl"
Matrix identity() { return {1,0,0,0, 0,1,0,0, 0,0,1,0, 0,0,0,1}; }
void validate_affine(const Matrix& matrix, const char* label) {
    for (float value : matrix) require(std::isfinite(value), std::string(label) + " is not finite");
    require(std::abs(matrix[3]) < 1e-5f && std::abs(matrix[7]) < 1e-5f &&
            std::abs(matrix[11]) < 1e-5f && std::abs(matrix[15] - 1) < 1e-5f,
            std::string(label) + " must be affine");
    const double a=matrix[0], b=matrix[4], c=matrix[8], d=matrix[1], e=matrix[5],
                 f=matrix[9], g=matrix[2], h=matrix[6], i=matrix[10];
    const double determinant = a*(e*i-f*h) - b*(d*i-f*g) + c*(d*h-e*g);
    const double scale = std::sqrt((a*a+d*d+g*g)*(b*b+e*e+h*h)*(c*c+f*f+i*i));
    require(scale > 0 && std::abs(determinant) > scale * 1e-8, std::string("Singular ") + label);
}
Matrix multiply(const Matrix& a, const Matrix& b) {
    Matrix out{};
    for (int c = 0; c < 4; ++c) for (int r = 0; r < 4; ++r)
        for (int k = 0; k < 4; ++k) out[c * 4 + r] += a[k * 4 + r] * b[c * 4 + k];
    for (float x : out) require(std::isfinite(x), "Node transform is not finite");
    return out;
}
float checked_float(double value) {
    require(std::isfinite(value) && std::abs(value) < 1e12, "Transform/material value is not finite or reasonable");
    return static_cast<float>(value);
}
Matrix local_matrix(const tinygltf::Node& node) {
    if (!node.matrix.empty()) {
        require(node.matrix.size() == 16 && node.translation.empty() && node.rotation.empty() && node.scale.empty(),
                "Invalid matrix and TRS node transform");
        Matrix out; std::transform(node.matrix.begin(), node.matrix.end(), out.begin(), checked_float);
        validate_affine(out, "node matrix");
        return out;
    }
    require((node.translation.empty() || node.translation.size() == 3) &&
            (node.rotation.empty() || node.rotation.size() == 4) && (node.scale.empty() || node.scale.size() == 3),
            "Invalid node TRS shape");
    std::array<float, 3> t{0,0,0}, s{1,1,1}; std::array<float,4> q{0,0,0,1};
    if (!node.translation.empty()) std::transform(node.translation.begin(), node.translation.end(), t.begin(), checked_float);
    if (!node.scale.empty()) std::transform(node.scale.begin(), node.scale.end(), s.begin(), checked_float);
    if (!node.rotation.empty()) std::transform(node.rotation.begin(), node.rotation.end(), q.begin(), checked_float);
    float norm = 0; for (float v : q) norm += v * v;
    require(norm > 1e-8f && std::abs(norm - 1) < 0.01f, "Invalid node quaternion");
    for (float& v : q) v /= std::sqrt(norm);
    for (float v : s) require(std::abs(v) > 1e-8f, "Singular node scale");
    const float x=q[0], y=q[1], z=q[2], w=q[3];
    return {(1-2*y*y-2*z*z)*s[0], (2*x*y+2*z*w)*s[0], (2*x*z-2*y*w)*s[0], 0,
            (2*x*y-2*z*w)*s[1], (1-2*x*x-2*z*z)*s[1], (2*y*z+2*x*w)*s[1], 0,
            (2*x*z+2*y*w)*s[2], (2*y*z-2*x*w)*s[2], (1-2*x*x-2*y*y)*s[2], 0,
            t[0],t[1],t[2],1};
}
void build_nodes(const tinygltf::Model& model, Avatar& avatar) {
    avatar.nodes.resize(model.nodes.size());
    bool orphan_skin=false;
    for (std::size_t i = 0; i < model.nodes.size(); ++i) {
        const auto& source = model.nodes[i]; auto& node = avatar.nodes[i];
        node.name = source.name; node.mesh = source.mesh; node.skin = source.skin; node.local = local_matrix(source); node.children = source.children;
        if (node.mesh != -1) indexed(model.meshes, node.mesh, "node mesh");
        if (node.skin != -1) indexed(model.skins, node.skin, "node skin");
        if(node.mesh==-1&&node.skin!=-1){node.skin=-1;orphan_skin=true;}
        for (int child : source.children) {
            indexed(model.nodes, child, "hierarchy child");
            require(child != static_cast<int>(i) && avatar.nodes[child].parent == -1, "Invalid node hierarchy parent/cycle");
            avatar.nodes[child].parent = static_cast<int>(i);
        }
    }
    if(orphan_skin)avatar.warnings.push_back("Ignored skin references on nodes without a mesh.");
    std::vector<int> state(model.nodes.size());
    std::function<void(int)> visit = [&](int i) {
        require(state[i] != 1, "Cycle in node hierarchy"); if (state[i] == 2) return;
        state[i] = 1; auto& node = avatar.nodes[i];
        if (node.parent != -1) visit(node.parent);
        node.world = node.parent == -1 ? node.local : multiply(avatar.nodes[node.parent].world, node.local);
        validate_affine(node.world, "node world matrix");
        state[i] = 2;
    };
    for (std::size_t i = 0; i < model.nodes.size(); ++i) visit(static_cast<int>(i));
    const int scene = model.defaultScene == -1 ? 0 : model.defaultScene;
    const auto& active = indexed(model.scenes, scene, "scene");
    std::function<void(int)> activate = [&](int i) {
        indexed(model.nodes, i, "scene node"); avatar.nodes[i].active = true;
        for (int child : model.nodes[i].children) activate(child);
    };
    for (int root : active.nodes) {
        indexed(model.nodes, root, "scene root");
        require(avatar.nodes[root].parent == -1 && !avatar.nodes[root].active, "Invalid scene root hierarchy");
        activate(root);
    }
}
void build_humanoid(const Json& doc, Avatar& avatar) {
    const auto& extensions = doc.at("extensions");
    const bool v1 = extensions.contains("VRMC_vrm"), v0 = extensions.contains("VRM");
    require(v1 != v0, "Expected exactly one VRM humanoid extension");
    avatar.version = v1 ? VrmVersion::V1 : VrmVersion::V0;
    const auto& extension = extensions.at(v1 ? "VRMC_vrm" : "VRM");
    require(v1 ? extension.at("specVersion") == "1.0" : extension.value("specVersion", "0.0") == "0.0",
            "Unsupported VRM specVersion");
    const auto& meta = extension.at("meta");
    avatar.title = meta.value(v1 ? "name" : "title", std::string{});
    avatar.license = meta.value(v1 ? "licenseUrl" : "licenseName", std::string{});
    if (v1) avatar.authors = meta.value("authors", std::vector<std::string>{});
    else avatar.authors.push_back(meta.value("author", std::string{}));
    const auto& bones = extension.at("humanoid").at("humanBones");
    std::set<int> assigned;
    auto add = [&](std::string name, const Json& bone) {
        if (!v1) {
            if (name == "leftThumbProximal") name = "leftThumbMetacarpal";
            else if (name == "leftThumbIntermediate") name = "leftThumbProximal";
            else if (name == "rightThumbProximal") name = "rightThumbMetacarpal";
            else if (name == "rightThumbIntermediate") name = "rightThumbProximal";
        }
        const auto index = field(bone, "node", std::numeric_limits<std::size_t>::max());
        require(index < avatar.nodes.size() && avatar.nodes[index].active && assigned.insert(static_cast<int>(index)).second,
                "Invalid or duplicated humanoid node");
        require(avatar.humanoid.emplace(name, static_cast<int>(index)).second, "Duplicate humanoid bone name");
    };
    if (v1) for (auto it = bones.begin(); it != bones.end(); ++it) add(it.key(), it.value());
    else for (const auto& bone : bones) add(bone.at("bone").get<std::string>(), bone);
    for (const char* name : {"hips","spine","head","leftUpperLeg","leftLowerLeg","leftFoot",
             "rightUpperLeg","rightLowerLeg","rightFoot","leftUpperArm","leftLowerArm","leftHand",
             "rightUpperArm","rightLowerArm","rightHand"})
        require(avatar.humanoid.count(name) != 0, std::string("Missing required humanoid bone: ") + name);
    const std::pair<const char*, const char*> chains[] = {
        {"spine","hips"},{"head","spine"},
        {"leftUpperLeg","hips"},{"leftLowerLeg","leftUpperLeg"},{"leftFoot","leftLowerLeg"},
        {"rightUpperLeg","hips"},{"rightLowerLeg","rightUpperLeg"},{"rightFoot","rightLowerLeg"},
        {"leftUpperArm","spine"},{"leftLowerArm","leftUpperArm"},{"leftHand","leftLowerArm"},
        {"rightUpperArm","spine"},{"rightLowerArm","rightUpperArm"},{"rightHand","rightLowerArm"}
    };
    for (const auto& chain : chains) {
        int cursor = avatar.nodes[avatar.humanoid.at(chain.first)].parent;
        const int ancestor = avatar.humanoid.at(chain.second);
        while (cursor != -1 && cursor != ancestor) cursor = avatar.nodes[cursor].parent;
        require(cursor == ancestor, std::string("Invalid humanoid hierarchy for ") + chain.first);
    }
}
void build_constraints(const Json& doc, Avatar& avatar) {
    try {
        for (std::size_t i=0;i<avatar.nodes.size();++i) {
            const auto& node=doc.at("nodes").at(i);
            if (!node.contains("extensions") || !node.at("extensions").contains("VRMC_node_constraint")) continue;
            const auto& extension=node.at("extensions").at("VRMC_node_constraint");
            require(extension.is_object() && extension.at("specVersion")=="1.0","Unsupported node constraint specVersion");
            const auto& constraint=extension.at("constraint");
            require(constraint.is_object(),"Invalid node constraint object");
            const int types=static_cast<int>(constraint.contains("roll"))+static_cast<int>(constraint.contains("aim"))+static_cast<int>(constraint.contains("rotation"));
            require(types==1,"Node constraint must have exactly one roll, aim, or rotation");
            NodeConstraint out;out.target=static_cast<int>(i);
            const auto type=constraint.contains("roll")?"roll":constraint.contains("aim")?"aim":"rotation";
            out.type=std::string(type)=="roll"?NodeConstraintType::Roll:std::string(type)=="aim"?NodeConstraintType::Aim:NodeConstraintType::Rotation;
            const auto& value=constraint.at(type);require(value.is_object(),"Invalid node constraint properties");
            const auto source=field(value,"source",std::numeric_limits<std::size_t>::max());
            require(source<avatar.nodes.size(),"Invalid node constraint source range");out.source=static_cast<int>(source);
            if (out.type!=NodeConstraintType::Rotation) {
                const auto axis=value.at(out.type==NodeConstraintType::Roll?"rollAxis":"aimAxis").get<std::string>();
                const std::vector<std::string> axes=out.type==NodeConstraintType::Roll?
                    std::vector<std::string>{"X","Y","Z"}:std::vector<std::string>{"PositiveX","PositiveY","PositiveZ","NegativeX","NegativeY","NegativeZ"};
                const auto found=std::find(axes.begin(),axes.end(),axis);
                require(found!=axes.end(),"Invalid node constraint axis");out.axis=static_cast<ConstraintAxis>(found-axes.begin());
            }
            require(!value.contains("weight") || value.at("weight").is_number(),"Invalid node constraint weight type");
            const auto weight=value.value("weight",1.0);
            require(std::isfinite(weight) && weight>=0 && weight<=1,"Invalid node constraint weight range");out.weight=static_cast<float>(weight);
            avatar.constraints.push_back(out);
        }
        detail::node_constraint_order(avatar);
    } catch (const std::exception& e) { fail(std::string("Invalid node constraint: ")+e.what()); }
}
#include "spring_import.inl"

void build_skins(const tinygltf::Model& model, Avatar& avatar, const ImportLimits& limits) {
    for (const auto& source : model.skins) {
        require(!source.joints.empty() && source.joints.size() <= limits.joints_per_skin, "Skin joint count exceeds limit");
        Skin skin; skin.joints = source.joints;
        std::set<int> unique;
        for (int joint : skin.joints) {
            indexed(model.nodes, joint, "skin joint"); require(unique.insert(joint).second, "Duplicated skin joint");
        }
        if (source.skeleton != -1) indexed(model.nodes, source.skeleton, "skin skeleton");
        skin.inverse_bind.resize(skin.joints.size(), identity());
        if (source.inverseBindMatrices != -1) {
            const auto& accessor = indexed(model.accessors, source.inverseBindMatrices, "inverse bind accessor");
            require(accessor.count == skin.joints.size(), "Incorrect inverse bind matrix count");
            auto values = read_accessor(model, source.inverseBindMatrices, TINYGLTF_TYPE_MAT4);
            for (std::size_t i = 0; i < skin.joints.size(); ++i)
                for (int c = 0; c < 16; ++c) skin.inverse_bind[i][c] = static_cast<float>(values[i * 16 + c]);
        }
        for (const auto& matrix : skin.inverse_bind) validate_affine(matrix, "inverse bind matrix");
        avatar.skins.push_back(std::move(skin));
    }
}
void build_materials(tinygltf::Model& model, const Json& doc, Avatar& avatar) {
    for (auto& source : model.images) {
        require(source.bits == 8 && source.component == 4 && source.image.size() ==
                static_cast<std::size_t>(source.width) * source.height * 4, "Expected RGBA8 image");
        avatar.images.push_back({source.width, source.height, std::move(source.image)});
    }
    for (const auto& source : model.textures) {
        indexed(avatar.images, source.source, "texture image");
        Texture texture; texture.image = source.source;
        if (source.sampler != -1) {
            const auto& sampler = indexed(model.samplers, source.sampler, "texture sampler");
            texture.wrap_s = sampler.wrapS; texture.wrap_t = sampler.wrapT;
            texture.min_filter = sampler.minFilter == -1 ? 9987 : sampler.minFilter;
            texture.mag_filter = sampler.magFilter == -1 ? 9729 : sampler.magFilter;
        }
        avatar.textures.push_back(texture);
    }
    for (std::size_t i = 0; i < model.materials.size(); ++i) {
        const auto& source = model.materials[i]; Material material; material.name = source.name;
        require(source.pbrMetallicRoughness.baseColorFactor.size() == 4, "Invalid base color shape");
        for (int c = 0; c < 4; ++c) {
            material.base_color[c] = checked_float(source.pbrMetallicRoughness.baseColorFactor[c]);
            require(material.base_color[c] >= 0 && material.base_color[c] <= 1, "Invalid base color range");
        }
        const auto& info = source.pbrMetallicRoughness.baseColorTexture;
        material.base_texture = info.index;
        if (info.index != -1) {
            indexed(avatar.textures, info.index, "base color texture");
            // Raw preflight validated the selected UV set and affine transform;
            // build_meshes bakes those coordinates into each material section.
        }
        require(source.alphaMode == "OPAQUE" || source.alphaMode == "MASK" || source.alphaMode == "BLEND", "Invalid alpha mode");
        material.alpha = source.alphaMode == "MASK" ? AlphaMode::Mask : source.alphaMode == "BLEND" ? AlphaMode::Blend : AlphaMode::Opaque;
        material.alpha_cutoff = checked_float(source.alphaCutoff);
        material.double_sided = source.doubleSided;
        material.unlit = source.extensions.count("KHR_materials_unlit") != 0;
        if (source.extensions.count("VRMC_materials_mtoon"))
            material.mtoon_json = doc.at("materials").at(i).at("extensions").at("VRMC_materials_mtoon").dump();
        bool mtoon = source.extensions.count("VRMC_materials_mtoon") != 0;
        if (!mtoon && doc.contains("extensions") && doc.at("extensions").contains("VRM")) {
            const auto& legacy = doc.at("extensions").at("VRM");
            if (legacy.is_object() && legacy.contains("materialProperties")) {
                const auto& properties = legacy.at("materialProperties");
                if (properties.is_array() && i < properties.size()) {
                    const auto& property = properties.at(i);
                    mtoon = property.is_object() && property.contains("shader") && property.contains("name") &&
                        property.at("shader") == "VRM/MToon" && property.at("name") == source.name;
                }
            }
        }
        // VRoid combines the brow and its painted face overlay in one BLEND
        // material. Its nearly opaque brow otherwise takes the world's simpler
        // OIT lighting instead of the face's native lighting. Match the tested
        // menu cutout path only for this standard, unfaded MToon material.
        if (mtoon && material.alpha == AlphaMode::Blend && material.base_color[3] == 1.f &&
            material.name.find("_FaceBrow_") != std::string::npos) {
            material.alpha = AlphaMode::Mask;
            material.alpha_cutoff = .5f;
            avatar.warnings.push_back("VRoid FaceBrow overlay uses alpha cutout for consistent native face lighting.");
        }
        avatar.materials.push_back(std::move(material));
    }
}
#include "import_normals.inl"
#include "import_mouth.inl"
#include "import_meshes.inl"
} // namespace

std::vector<detail::ConstraintStep> detail::node_constraint_order(const Avatar& avatar) {
    const auto count=avatar.nodes.size();
    require(count<=std::numeric_limits<std::size_t>::max()/2,"Node constraint graph exceeds size limit");
    std::vector<std::vector<std::size_t>> outgoing(count*2);
    std::vector<std::size_t> pending(count*2);
    auto edge=[&](std::size_t from,std::size_t to){outgoing[from].push_back(to);++pending[to];};
    for(std::size_t i=0;i<count;++i){
        const auto parent=avatar.nodes[i].parent;
        require(parent>=-1 && (parent<0 || static_cast<std::size_t>(parent)<count),"Invalid node constraint parent");
        edge(i*2,i*2+1); // world requires its own local transform
        if(parent>=0)edge(static_cast<std::size_t>(parent)*2+1,i*2+1);
    }
    std::set<int> targets;
    for(const auto& c:avatar.constraints){
        require(c.target>=0 && static_cast<std::size_t>(c.target)<count && c.source>=0 && static_cast<std::size_t>(c.source)<count,
                "Invalid node constraint target/source range");
        require(c.target!=c.source && targets.insert(c.target).second,"Self or duplicate node constraint target");
        require(c.type==NodeConstraintType::Roll || c.type==NodeConstraintType::Aim || c.type==NodeConstraintType::Rotation,"Invalid node constraint type");
        const auto axis=static_cast<int>(c.axis);
        require(axis>=0 && axis<6 && (c.type!=NodeConstraintType::Roll || axis<3),"Invalid node constraint axis");
        require(std::isfinite(c.weight) && c.weight>=0 && c.weight<=1,"Invalid node constraint weight");
        const auto local=static_cast<std::size_t>(c.target)*2;
        if(c.type==NodeConstraintType::Aim){
            edge(static_cast<std::size_t>(c.source)*2+1,local);
            const auto parent=avatar.nodes[c.target].parent;
            if(parent>=0)edge(static_cast<std::size_t>(parent)*2+1,local);
        }else edge(static_cast<std::size_t>(c.source)*2,local);
    }
    std::vector<std::size_t> ready;ready.reserve(count*2);
    for(std::size_t i=0;i<pending.size();++i)if(pending[i]==0)ready.push_back(i);
    for(std::size_t cursor=0;cursor<ready.size();++cursor)
        for(auto next:outgoing[ready[cursor]])if(--pending[next]==0)ready.push_back(next);
    require(ready.size()==count*2,"Node constraint dependency cycle");
    std::vector<ConstraintStep> result;result.reserve(ready.size());
    for(auto step:ready)result.push_back({step/2,step%2!=0});
    return result;
}

ImportResult load_vrm_bytes(const Bytes& bytes, const ImportLimits& limits) {
    try {
        const auto doc = preflight(bytes, limits);
        tinygltf::TinyGLTF loader; tinygltf::Model model; std::string error, warning;
        ImageBudget image_budget(limits,doc); loader.SetImageLoader(load_image, &image_budget);
        const bool decoded = loader.LoadBinaryFromMemory(&model, &error, &warning, bytes.data(), static_cast<unsigned int>(bytes.size()));
        require(decoded && error.empty(), "glTF decode failed: " + error);
        require(model.meshes.size() == doc.at("meshes").size(), "Decoder lost mesh data");
        for (std::size_t i = 0; i < model.meshes.size(); ++i)
            require(model.meshes[i].primitives.size() == doc.at("meshes").at(i).at("primitives").size(),
                    "Decoder lost primitive data");
        Avatar avatar; if (!warning.empty()) avatar.warnings.push_back(warning);
        build_nodes(model, avatar); build_humanoid(doc, avatar); build_constraints(doc,avatar);
        build_skins(model, avatar, limits); build_springs(doc,avatar); build_materials(model, doc, avatar); build_meshes(model, doc, avatar, limits);
        std::set<std::string> used(model.extensionsUsed.begin(), model.extensionsUsed.end());
        if (used.count("VRMC_materials_mtoon") || used.count("VRM"))
            avatar.warnings.push_back("MToon parameters preserved where available; toon lighting/outlines are not implemented.");
        return {std::move(avatar), {}};
    } catch (const std::exception& e) { return {{}, e.what()}; }
}
ImportResult load_vrm(const std::filesystem::path& path, const ImportLimits& limits) {
    try {
        std::ifstream file(path, std::ios::binary | std::ios::ate);
        if (!file) return {{}, "Cannot open VRM file"};
        const auto length = file.tellg();
        if (length < 0 || static_cast<std::uint64_t>(length) > limits.file_bytes ||
            static_cast<std::uint64_t>(length) > std::numeric_limits<unsigned int>::max())
            return {{}, "VRM file exceeds size limit"};
        Bytes bytes(static_cast<std::size_t>(length)); file.seekg(0);
        if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())))
            return {{}, "Cannot read complete VRM file"};
        return load_vrm_bytes(bytes, limits);
    } catch (const std::exception& e) { return {{}, e.what()}; }
}
} // namespace vrm
