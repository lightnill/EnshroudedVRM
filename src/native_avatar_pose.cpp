#include "native_avatar_pose.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace evrm {
namespace {
using Matrix = std::array<double,16>;
constexpr std::size_t max_vertices = 2'000'000, max_indices = 6'000'000;
constexpr std::size_t max_primitives = 4096, max_nodes = 2048, max_joints = 512;

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void add_bounded(std::size_t& total, std::size_t amount, std::size_t limit) {
    require(total <= limit && amount <= limit-total, "Native avatar geometry exceeds import limits");
    total += amount;
}
bool reference(int index, std::size_t count) {
    return index >= 0 && static_cast<std::size_t>(index) < count;
}
// Cofactors of the linear part, in row order, divided by its determinant.
// This is A^-T, rather than the incorrect A*n operation for affine stretch.
std::array<double,9> normal_matrix(const Matrix& m) {
    const double a=m[0],b=m[4],c=m[8], d=m[1],e=m[5],f=m[9], g=m[2],h=m[6],i=m[10];
    std::array<double,9> cofactor{e*i-f*h,f*g-d*i,d*h-e*g,
                                c*h-b*i,a*i-c*g,b*g-a*h,
                                b*f-c*e,c*d-a*f,a*e-b*d};
    const double determinant=a*cofactor[0]+b*cofactor[1]+c*cofactor[2];
    // Double intermediates safely cover products and inverses of finite float
    // matrices. Do not reject valid small model scales with an absolute epsilon.
    require(std::isfinite(determinant) && determinant != 0, "Singular native avatar transform");
    for(auto& value:cofactor) {
        value /= determinant;
        require(std::isfinite(value), "Nonfinite inverse native avatar transform");
    }
    return cofactor;
}
Matrix affine(const vrm::Matrix& input) {
    Matrix output{};
    for(std::size_t i=0;i<output.size();++i) {
        require(std::isfinite(input[i]), "Nonfinite native avatar transform");
        output[i]=input[i];
    }
    require(std::abs(output[3])<=1e-5 && std::abs(output[7])<=1e-5 &&
            std::abs(output[11])<=1e-5 && std::abs(output[15]-1)<=1e-5,
            "Nonaffine native avatar transform");
    output[3]=output[7]=output[11]=0;output[15]=1;
    (void)normal_matrix(output);
    return output;
}
Matrix product(const Matrix& a,const Matrix& b) {
    Matrix result{};
    for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)
        result[c*4+r] += a[k*4+r]*b[c*4+k];
    for(const auto value:result)require(std::isfinite(value),"Nonfinite combined native avatar transform");
    return result;
}
float output_float(double value) {
    require(std::isfinite(value) && std::abs(value)<=std::numeric_limits<float>::max(),
            "Nonfinite native avatar vertex");
    return static_cast<float>(value);
}

bool matching_history(const NativeAvatarPose* previous,const NativeAvatarPose& current) {
    if(!previous || !*previous || previous->vertices.size()!=current.vertices.size() ||
       previous->indices!=current.indices || previous->draws.size()!=current.draws.size())return false;
    for(std::size_t i=0;i<current.draws.size();++i) {
        const auto& a=previous->draws[i];const auto& b=current.draws[i];
        if(a.first_index!=b.first_index || a.index_count!=b.index_count || a.material!=b.material)return false;
    }
    for(const auto& vertex:previous->vertices) {
        for(const auto value:vertex.position)if(!std::isfinite(value))return false;
        for(const auto value:vertex.normal)if(!std::isfinite(value))return false;
        for(const auto value:vertex.uv)if(!std::isfinite(value))return false;
        for(const auto value:vertex.previous_position)if(!std::isfinite(value))return false;
        const double length=std::hypot(static_cast<double>(vertex.normal[0]),
                                     static_cast<double>(vertex.normal[1]),static_cast<double>(vertex.normal[2]));
        if(std::abs(length-1)>1e-4)return false;
    }
    return true;
}
}

NativeAvatarPose make_native_avatar_pose(const vrm::Avatar& avatar,const vrm::RetargetResult& pose,
                                        const NativeAvatarPose* previous) noexcept {
    try {
        require(static_cast<bool>(pose), "Invalid native avatar retarget result");
        require(!avatar.nodes.empty() && avatar.nodes.size()<=max_nodes &&
                pose.node_world.size()==avatar.nodes.size(), "Invalid native avatar node count");
        require(avatar.meshes.size()<=max_primitives && avatar.skins.size()<=max_nodes &&
                avatar.materials.size()<=max_primitives && pose.palettes.size()<=max_nodes,
                "Native avatar metadata exceeds limits");
        require(avatar.vertex_count<=max_vertices && avatar.triangle_count<=max_indices/3,
                "Native avatar declared geometry exceeds limits");

        // Count storage and instanced output separately, before allocating output
        // or walking vertices. Shared meshes can expand beyond the import cap.
        std::size_t source_vertices=0,source_indices=0,source_primitives=0;
        for(const auto& mesh:avatar.meshes) {
            add_bounded(source_primitives,mesh.primitives.size(),max_primitives);
            for(const auto& primitive:mesh.primitives) {
                add_bounded(source_vertices,primitive.vertices.size(),max_vertices);
                add_bounded(source_indices,primitive.indices.size(),max_indices);
            }
        }
        std::size_t vertex_count=0,index_count=0,draw_count=0;
        for(std::size_t n=0;n<avatar.nodes.size();++n) {
            const auto& node=avatar.nodes[n];
            require(node.parent==-1 || (reference(node.parent,avatar.nodes.size()) && node.parent!=static_cast<int>(n)),
                    "Invalid native avatar parent node");
            require(node.mesh==-1 || reference(node.mesh,avatar.meshes.size()), "Invalid native avatar mesh node");
            require(node.skin==-1 || reference(node.skin,avatar.skins.size()), "Invalid native avatar skin node");
            if(!node.active || node.mesh<0)continue;
            for(const auto& primitive:avatar.meshes[node.mesh].primitives) {
                require(!primitive.vertices.empty() && !primitive.indices.empty() && primitive.indices.size()%3==0,
                        "Invalid native avatar triangle layout");
                require(primitive.material==-1 || reference(primitive.material,avatar.materials.size()),
                        "Invalid native avatar material");
                add_bounded(vertex_count,primitive.vertices.size(),max_vertices);
                add_bounded(index_count,primitive.indices.size(),max_indices);
                add_bounded(draw_count,1,max_primitives);
            }
        }
        require(vertex_count && index_count && draw_count, "No active native avatar geometry");
        require(vertex_count<=std::numeric_limits<std::uint32_t>::max() &&
                index_count<=std::numeric_limits<std::uint32_t>::max(), "Native avatar index overflow");

        std::vector<Matrix> worlds;worlds.reserve(avatar.nodes.size());
        for(const auto& world:pose.node_world)worlds.push_back(affine(world));
        std::vector<const vrm::SkinPalette*> palettes(avatar.nodes.size(),nullptr);
        for(const auto& palette:pose.palettes) {
            require(reference(palette.mesh_node,avatar.nodes.size()), "Invalid native avatar palette mesh node");
            const auto& node=avatar.nodes[palette.mesh_node];
            require(node.active && node.mesh>=0 && reference(palette.skin,avatar.skins.size()) &&
                    palette.skin==node.skin, "Mismatched native avatar skin palette");
            require(!palettes[palette.mesh_node], "Ambiguous native avatar skin palette");
            const auto& skin=avatar.skins[palette.skin];
            require(!skin.joints.empty() && skin.joints.size()<=max_joints &&
                    skin.inverse_bind.size()==skin.joints.size() && palette.matrices.size()==skin.joints.size(),
                    "Invalid native avatar palette size");
            std::array<bool,max_nodes> used{};
            for(std::size_t j=0;j<skin.joints.size();++j) {
                require(reference(skin.joints[j],avatar.nodes.size()) && !used[skin.joints[j]],
                        "Invalid native avatar skin joint node");
                used[skin.joints[j]]=true;
                (void)affine(skin.inverse_bind[j]);
                (void)affine(palette.matrices[j]);
            }
            palettes[palette.mesh_node]=&palette;
        }

        NativeAvatarPose output;
        output.vertices.reserve(vertex_count);output.indices.reserve(index_count);output.draws.reserve(draw_count);
        for(std::size_t n=0;n<avatar.nodes.size();++n) {
            const auto& node=avatar.nodes[n];if(!node.active || node.mesh<0)continue;
            // A bounded per-node conversion avoids validating/copying palette
            // matrices for every vertex and permits multiple mesh instances.
            std::vector<Matrix> skin;
            if(palettes[n]) {
                skin.reserve(palettes[n]->matrices.size());
                for(const auto& m:palettes[n]->matrices)skin.push_back(affine(m));
            }
            for(const auto& primitive:avatar.meshes[node.mesh].primitives) {
                require(!primitive.skinned || !skin.empty(), "Missing native avatar skin palette");
                const auto base=static_cast<std::uint32_t>(output.vertices.size());
                output.draws.push_back({static_cast<std::uint32_t>(output.indices.size()),
                                        static_cast<std::uint32_t>(primitive.indices.size()),primitive.material});
                for(const auto index:primitive.indices) {
                    require(index<primitive.vertices.size(), "Native avatar index outside primitive");
                    require(index<=std::numeric_limits<std::uint32_t>::max()-base, "Native avatar global index overflow");
                    output.indices.push_back(base+index);
                }
                const auto static_normal=normal_matrix(worlds[n]);
                for(const auto& vertex:primitive.vertices) {
                    for(const auto value:vertex.position)require(std::isfinite(value),"Nonfinite native avatar position");
                    for(const auto value:vertex.normal)require(std::isfinite(value),"Nonfinite native avatar normal");
                    for(const auto value:vertex.uv)require(std::isfinite(value),"Nonfinite native avatar UV");
                    Matrix transform=worlds[n];
                    auto normals=static_normal;
                    if(primitive.skinned) {
                        Matrix blend{};double sum=0;
                        for(std::size_t influence=0;influence<4;++influence) {
                            const auto weight=vertex.weights[influence];
                            require(vertex.joints[influence]<skin.size(), "Native avatar joint outside palette");
                            require(std::isfinite(weight) && weight>=0 && weight<=1.001f,
                                    "Invalid native avatar weight");
                            sum+=weight;
                            for(std::size_t i=0;i<blend.size();++i)blend[i]+=skin[vertex.joints[influence]][i]*weight;
                        }
                        require(std::isfinite(sum) && std::abs(sum-1)<=1e-4, "Invalid native avatar weight sum");
                        // Positions from the renderer's weighted mesh-local xyz
                        // then node_world(p,1); the node translation is applied once.
                        blend[3]=blend[7]=blend[11]=0;blend[15]=1;
                        transform=product(worlds[n],blend);normals=normal_matrix(transform);
                    }
                    NativeAvatarVertex result{};
                    std::array<double,3> normal{};
                    for(int r=0;r<3;++r) {
                        double position=transform[12+r];
                        for(int c=0;c<3;++c) {
                            position+=transform[c*4+r]*vertex.position[c];
                            normal[r]+=normals[r*3+c]*vertex.normal[c];
                        }
                        result.position[r]=result.previous_position[r]=output_float(position);
                    }
                    const double length=std::hypot(normal[0],normal[1],normal[2]);
                    require(std::isfinite(length) && length>0, "Degenerate native avatar normal");
                    for(int c=0;c<3;++c)result.normal[c]=output_float(normal[c]/length);
                    std::copy(vertex.uv.begin(),vertex.uv.end(),result.uv);
                    output.vertices.push_back(result);
                }
            }
        }
        if(matching_history(previous,output)) {
            for(std::size_t i=0;i<output.vertices.size();++i)
                std::copy(std::begin(previous->vertices[i].position),std::end(previous->vertices[i].position),
                          output.vertices[i].previous_position);
        }
        return output;
    } catch(const std::exception& error) {
        NativeAvatarPose failed;
        // A short fallback still reports an atomic error if allocating the
        // descriptive message fails (including an output allocation failure).
        try {failed.error=error.what();}catch(...) {failed.error="pose failure";}
        return failed;
    } catch(...) {
        NativeAvatarPose failed;
        failed.error="pose failure";
        return failed;
    }
}
}
