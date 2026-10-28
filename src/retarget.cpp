#include "retarget.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <set>
#include <stdexcept>

namespace vrm {
namespace {

using M3 = std::array<double, 9>;
using M4 = std::array<double, 16>;
using V3 = std::array<double, 3>;

constexpr M3 identity_rotation{1,0,0, 0,1,0, 0,0,1};

void require(bool condition, const std::string& error) {
    if (!condition) throw std::runtime_error(error);
}
void require(bool condition,const char* error) {
    if(!condition)throw std::runtime_error(error);
}

M3 linear(const M4& a) {
    M3 r{};
    for (int c = 0; c < 3; ++c) for (int i = 0; i < 3; ++i) r[c*3+i] = a[c*4+i];
    return r;
}

M3 transpose(const M3& a) {
    M3 r{};
    for (int c = 0; c < 3; ++c) for (int i = 0; i < 3; ++i) r[c*3+i] = a[i*3+c];
    return r;
}

M3 multiply(const M3& a, const M3& b) {
    M3 r{};
    for (int c = 0; c < 3; ++c) for (int i = 0; i < 3; ++i)
        for (int k = 0; k < 3; ++k) r[c*3+i] += a[k*3+i]*b[c*3+k];
    return r;
}

V3 cross(const V3& a, const V3& b) {
    return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};
}

double squared_length(const V3& a) { return a[0]*a[0]+a[1]*a[1]+a[2]*a[2]; }

V3 rotate_vector(const M3& m,const V3& v) {
    V3 out{};for(int i=0;i<3;++i)for(int k=0;k<3;++k)out[i]+=m[k*3+i]*v[k];return out;
}

M3 quaternion_rotation(double x,double y,double z,double w) {
    return {1-2*(y*y+z*z),2*(x*y+z*w),2*(x*z-y*w),
            2*(x*y-z*w),1-2*(x*x+z*z),2*(y*z+x*w),
            2*(x*z+y*w),2*(y*z-x*w),1-2*(x*x+y*y)};
}

// Slerp from destination rest, as required by VRMC_node_constraint 1.0.
M3 weighted_rotation(const M3& rest,const M3& desired,double weight) {
    if(weight==0)return rest;
    if(weight==1)return desired;
    const auto m=multiply(transpose(rest),desired);std::array<double,4> q{};
    const double trace=m[0]+m[4]+m[8];
    if(trace>0){const double s=2*std::sqrt(trace+1);q={(m[5]-m[7])/s,(m[6]-m[2])/s,(m[1]-m[3])/s,.25*s};}
    else if(m[0]>m[4] && m[0]>m[8]){const double s=2*std::sqrt(std::max(0.0,1+m[0]-m[4]-m[8]));q={.25*s,(m[3]+m[1])/s,(m[6]+m[2])/s,(m[5]-m[7])/s};}
    else if(m[4]>m[8]){const double s=2*std::sqrt(std::max(0.0,1+m[4]-m[0]-m[8]));q={(m[3]+m[1])/s,.25*s,(m[7]+m[5])/s,(m[6]-m[2])/s};}
    else{const double s=2*std::sqrt(std::max(0.0,1+m[8]-m[0]-m[4]));q={(m[6]+m[2])/s,(m[7]+m[5])/s,.25*s,(m[1]-m[3])/s};}
    double length=0;for(double v:q)length+=v*v;length=std::sqrt(length);
    require(length>0 && std::isfinite(length),"Invalid constraint quaternion");
    for(double& v:q)v/=length;
    if(q[3]<0)for(double&v:q)v=-v;
    const double angle=std::acos(std::clamp(q[3],-1.0,1.0));
    if(angle<1e-10)return rest;
    const double factor=std::sin(weight*angle)/std::sin(angle);
    return multiply(rest,quaternion_rotation(q[0]*factor,q[1]*factor,q[2]*factor,std::cos(weight*angle)));
}

// Minimal swing aligns directions without guessing a longitudinal twist axis.
// Exactly opposite directions use a deterministic perpendicular half-turn.
// Zero-length endpoints carry no directional information and are skipped.
bool direction_rotation(V3 from, V3 to, M3& result) {
    const double from_length = std::sqrt(squared_length(from)), to_length = std::sqrt(squared_length(to));
    if (from_length <= 1e-12 || to_length <= 1e-12) return false;
    for (int i = 0; i < 3; ++i) { from[i] /= from_length; to[i] /= to_length; }
    const double dot = std::clamp(from[0]*to[0]+from[1]*to[1]+from[2]*to[2],-1.0,1.0);
    if (dot < -1+1e-12) {
        int least_aligned = 0;
        for (int i = 1; i < 3; ++i) if (std::abs(from[i]) < std::abs(from[least_aligned])) least_aligned = i;
        V3 cardinal{}; cardinal[least_aligned] = 1;
        auto axis = cross(from,cardinal);
        const double length = std::sqrt(squared_length(axis));
        for (double& value : axis) value /= length;
        for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r)
            result[c*3+r] = 2*axis[c]*axis[r]-(c == r ? 1.0 : 0.0);
        return true;
    }
    const auto swing_axis = cross(from,to);
    const double w = 1+dot, inv_length = 1/std::sqrt(squared_length(swing_axis)+w*w);
    const double x = swing_axis[0]*inv_length, y = swing_axis[1]*inv_length;
    const double z = swing_axis[2]*inv_length, qw = w*inv_length;
    result = quaternion_rotation(x,y,z,qw);
    return true;
}

const std::vector<std::string>& child_candidates(const std::string& semantic) {
    // Order follows each anatomical chain. Matching the same endpoint semantic
    // in both rigs avoids comparing a source chest with a target neck when one
    // of the optional intermediate joints is absent.
    static const std::map<std::string,std::vector<std::string>> candidates=[] {
        std::map<std::string,std::vector<std::string>> result{
        // Hips branches into torso and both legs. Its authored pelvis frame
        // must not be swung to a short/oblique spine attachment offset.
        // Global source rotation/translation still transfer below with Q0=I.
        {"spine",{"chest","upperChest","neck","head"}},
        {"chest",{"neck","head"}}, {"upperChest",{"neck","head"}}, {"neck",{"head"}},
        {"leftShoulder",{"leftUpperArm","leftLowerArm","leftHand","leftMiddleProximal"}},
        {"rightShoulder",{"rightUpperArm","rightLowerArm","rightHand","rightMiddleProximal"}},
        {"leftUpperArm",{"leftLowerArm","leftHand","leftMiddleProximal"}},
        {"rightUpperArm",{"rightLowerArm","rightHand","rightMiddleProximal"}},
        {"leftLowerArm",{"leftHand","leftMiddleProximal"}},
        {"rightLowerArm",{"rightHand","rightMiddleProximal"}},
        {"leftHand",{"leftMiddleProximal"}}, {"rightHand",{"rightMiddleProximal"}},
        {"leftUpperLeg",{"leftLowerLeg","leftFoot","leftToes"}},
        {"rightUpperLeg",{"rightLowerLeg","rightFoot","rightToes"}},
        {"leftLowerLeg",{"leftFoot","leftToes"}}, {"rightLowerLeg",{"rightFoot","rightToes"}}};
        for(const auto& side:{std::string("left"),std::string("right")}){
            result[side+"ThumbMetacarpal"]={side+"ThumbProximal",side+"ThumbDistal"};
            result[side+"ThumbProximal"]={side+"ThumbDistal"};
            for(const auto& finger:{std::string("Index"),std::string("Middle"),std::string("Ring"),std::string("Little")}){
                const auto prefix=side+finger;
                result[prefix+"Proximal"]={prefix+"Intermediate",prefix+"Distal"};
                result[prefix+"Intermediate"]={prefix+"Distal"};
            }
        }
        return result;
    }();
    static const std::vector<std::string> none;
    const auto found = candidates.find(semantic);
    return found == candidates.end() ? none : found->second;
}

const std::vector<std::string>& terminal_finger_parent_candidates(const std::string& semantic) {
    static const std::map<std::string,std::vector<std::string>> candidates=[] {
        std::map<std::string,std::vector<std::string>> result;
        for(const auto& side:{std::string("left"),std::string("right")}){
            result[side+"ThumbDistal"]={side+"ThumbProximal",side+"ThumbMetacarpal"};
            for(const auto& finger:{std::string("Index"),std::string("Middle"),std::string("Ring"),std::string("Little")}){
                const auto prefix=side+finger;
                result[prefix+"Distal"]={prefix+"Intermediate",prefix+"Proximal"};
            }
        }
        return result;
    }();
    static const std::vector<std::string> none;
    const auto found=candidates.find(semantic);
    return found==candidates.end()?none:found->second;
}

M4 multiply(const M4& a, const M4& b) {
    M4 r{};
    for (int c = 0; c < 4; ++c) for (int i = 0; i < 4; ++i)
        for (int k = 0; k < 4; ++k) r[c*4+i] += a[k*4+i]*b[c*4+k];
    return r;
}

double determinant(const M3& a) {
    return a[0]*(a[4]*a[8]-a[7]*a[5])
         - a[3]*(a[1]*a[8]-a[7]*a[2])
         + a[6]*(a[1]*a[5]-a[4]*a[2]);
}

M3 inverse(const M3& matrix) {
    double magnitude = 0;
    for (double value : matrix) magnitude = std::max(magnitude,std::abs(value));
    require(std::isfinite(magnitude) && magnitude > 0,"Singular or nonfinite linear transform");
    M3 a{};
    for (int i = 0; i < 9; ++i) a[i] = matrix[i]/magnitude;
    const double det = determinant(a);
    require(std::isfinite(det) && std::abs(det) > 1e-12,"Singular or ill-conditioned linear transform");
    M3 r{a[4]*a[8]-a[7]*a[5], a[7]*a[2]-a[1]*a[8], a[1]*a[5]-a[4]*a[2],
         a[6]*a[5]-a[3]*a[8], a[0]*a[8]-a[6]*a[2], a[3]*a[2]-a[0]*a[5],
         a[3]*a[7]-a[6]*a[4], a[6]*a[1]-a[0]*a[7], a[0]*a[4]-a[3]*a[1]};
    for (double& value : r) value /= det*magnitude;
    return r;
}

M4 affine(const Matrix& input, const std::string& label) {
    M4 m{};
    for (int i = 0; i < 16; ++i) {
        if(!std::isfinite(input[i]))throw std::runtime_error(label + " is nonfinite");
        m[i] = input[i];
    }
    if(!(std::abs(m[3]) <= 1e-5 && std::abs(m[7]) <= 1e-5 &&
            std::abs(m[11]) <= 1e-5 && std::abs(m[15]-1) <= 1e-5))throw std::runtime_error(label + " is not affine");
    inverse(linear(m));
    m[3] = m[7] = m[11] = 0; m[15] = 1;
    return m;
}

M4 inverse_affine(const M4& a) {
    const auto inv = inverse(linear(a));
    M4 r{}; r[15] = 1;
    for (int c = 0; c < 3; ++c) for (int i = 0; i < 3; ++i) r[c*4+i] = inv[c*3+i];
    for (int i = 0; i < 3; ++i) r[12+i] = -(inv[i]*a[12] + inv[3+i]*a[13] + inv[6+i]*a[14]);
    return r;
}

// Scaled Newton polar iteration removes source customization scale without
// treating a non-orthogonal 3x3 as a rotation. A static reflected scale is
// retained in stretch using a fixed first-column reflection, yielding a proper
// rotation factor and avoiding an arbitrary sign choice every frame.
M3 rotation(const M4& transform) {
    auto x = linear(transform);
    double magnitude = 0;
    for (double value : x) magnitude = std::max(magnitude,std::abs(value));
    require(magnitude > 0 && std::isfinite(magnitude),"Invalid rotation factor");
    for (double& value : x) value /= magnitude;
    if (determinant(x) < 0) for (int i = 0; i < 3; ++i) x[i] = -x[i];
    bool converged = false;
    for (int iteration = 0; iteration < 64; ++iteration) {
        const auto inv_transpose = transpose(inverse(x));
        double x_norm = 0, inv_norm = 0;
        for (int i = 0; i < 9; ++i) { x_norm += x[i]*x[i]; inv_norm += inv_transpose[i]*inv_transpose[i]; }
        const double gamma = std::sqrt(std::sqrt(inv_norm/x_norm));
        double difference = 0;
        for (int i = 0; i < 9; ++i) {
            const double next = 0.5*(gamma*x[i] + inv_transpose[i]/gamma);
            difference = std::max(difference,std::abs(next-x[i])); x[i] = next;
        }
        if (difference < 1e-11) { converged = true; break; }
    }
    require(converged && std::isfinite(determinant(x)),"Rotation decomposition failed");
    return x;
}

void evaluate_constraints(const Avatar& avatar,const std::vector<M4>& rest_local,
                          std::vector<M4>& world,const std::vector<detail::ConstraintStep>& schedule) {
    if(avatar.constraints.empty())return;
    const auto count=avatar.nodes.size();
    std::vector<M4> local(count);
    std::vector<const NodeConstraint*> constraints(count);
    for(const auto&c:avatar.constraints)constraints[c.target]=&c;
    // Freeze the retargeted LOCAL animation, not the bind pose. A constraint
    // ancestor then carries descendants without erasing their authored motion.
    for(std::size_t i=0;i<count;++i){const int parent=avatar.nodes[i].parent;
        local[i]=parent<0?world[i]:multiply(inverse_affine(world[parent]),world[i]);}
    for(const auto&step:schedule){
        const auto i=step.node;const int parent=avatar.nodes[i].parent;
        if(step.world){world[i]=parent<0?local[i]:multiply(world[parent],local[i]);continue;}
        const auto* c=constraints[i];if(!c)continue;
        const auto dst_rest=rotation(rest_local[i]);auto desired=dst_rest;
        if(c->type==NodeConstraintType::Aim){
            const auto parent_rotation=parent<0?identity_rotation:rotation(world[parent]);
            V3 axis{};const auto axis_index=static_cast<int>(c->axis);axis[axis_index%3]=axis_index<3?1:-1;
            const auto from=rotate_vector(multiply(parent_rotation,dst_rest),axis);
            V3 position{local[i][12],local[i][13],local[i][14]};
            if(parent>=0){const auto point=rotate_vector(linear(world[parent]),position);
                for(int k=0;k<3;++k)position[k]=point[k]+world[parent][12+k];}
            V3 to{};for(int k=0;k<3;++k)to[k]=world[c->source][12+k]-position[k];
            auto swing=identity_rotation;
            if(direction_rotation(from,to,swing))desired=multiply(multiply(multiply(transpose(parent_rotation),swing),parent_rotation),dst_rest);
        }else{
            const auto src_rest=rotation(rest_local[c->source]),src_current=rotation(local[c->source]);
            const auto delta=multiply(transpose(src_rest),src_current);
            if(c->type==NodeConstraintType::Rotation)desired=multiply(dst_rest,delta);
            else{
                const auto in_parent=multiply(multiply(src_rest,delta),transpose(src_rest));
                const auto in_destination=multiply(multiply(transpose(dst_rest),in_parent),dst_rest);
                V3 axis{};axis[static_cast<int>(c->axis)]=1;
                auto swing=identity_rotation;direction_rotation(axis,rotate_vector(in_destination,axis),swing);
                desired=multiply(dst_rest,multiply(transpose(swing),in_destination));
            }
        }
        const auto weighted=weighted_rotation(dst_rest,desired,c->weight);
        const auto correction=multiply(weighted,transpose(rotation(local[i])));
        const auto rotated=multiply(correction,linear(local[i]));
        for(int column=0;column<3;++column)for(int row=0;row<3;++row)local[i][column*4+row]=rotated[column*3+row];
    }
}

Matrix output(const M4& input) {
    Matrix out{};
    for (int i = 0; i < 16; ++i) {
        require(std::isfinite(input[i]) && std::abs(input[i]) <= std::numeric_limits<float>::max(),
                "Retarget output exceeds finite matrix range");
        out[i] = static_cast<float>(input[i]);
    }
    return out;
}

bool close(const M4& a, const M4& b) {
    for (int i = 0; i < 16; ++i)
        if (std::abs(a[i]-b[i]) > 2e-4*std::max(1.0,std::abs(b[i]))) return false;
    return true;
}

struct RetargetPreparation {
    bool ready=false;
    std::map<std::string,Matrix> rest_key;
    std::vector<std::string> current_keys;
    RetargetOptions options;
    std::map<std::string,M4> source_rest;
    std::map<std::string,M3> rest_rotation;
    std::vector<M4> locals,rest_world;
    std::vector<std::size_t> order;
    std::vector<detail::ConstraintStep> constraint_schedule;
    std::vector<std::string> semantic;
    std::vector<M3> rest_alignment,avatar_rotation;
    std::vector<std::vector<M4>> inverse_binds;
    bool matches(const SourcePose& source,const RetargetOptions& value)const {
        if(!ready||source.rest.size()!=rest_key.size()||source.current.size()!=current_keys.size()||
            value.align_rest_directions!=options.align_rest_directions||
            std::memcmp(value.source_to_avatar_basis.data(),options.source_to_avatar_basis.data(),sizeof(Matrix))||
            std::memcmp(&value.source_rest_height,&options.source_rest_height,sizeof(float))||
            std::memcmp(&value.avatar_rest_height,&options.avatar_rest_height,sizeof(float)))return false;
        auto old=rest_key.begin();
        for(const auto& entry:source.rest){if(entry.first!=old->first||std::memcmp(entry.second.data(),old->second.data(),sizeof(Matrix)))return false;++old;}
        std::size_t index=0;for(const auto& entry:source.current)if(entry.first!=current_keys[index++])return false;
        return true;
    }
};
} // namespace

static RetargetResult retarget_prepared(const Avatar& avatar,const SourcePose& source,const RetargetOptions& options,RetargetPreparation* cached) {
    try {
        require(!avatar.nodes.empty(),"Avatar has no nodes");
        require(std::isfinite(options.source_rest_height) && options.source_rest_height > 0 &&
                std::isfinite(options.avatar_rest_height) && options.avatar_rest_height > 0,"Rest heights must be positive and finite");
        const auto basis_matrix = affine(options.source_to_avatar_basis,"Source-to-avatar basis");
        require(std::abs(basis_matrix[12]) < 1e-6 && std::abs(basis_matrix[13]) < 1e-6 &&
                std::abs(basis_matrix[14]) < 1e-6,"Source-to-avatar basis must not translate");
        const auto basis = linear(basis_matrix), basis_inverse = transpose(basis);
        const auto metric = multiply(basis_inverse,basis);
        for (int c = 0; c < 3; ++c) for (int i = 0; i < 3; ++i)
            require(std::abs(metric[c*3+i]-(c == i ? 1.0 : 0.0)) < 1e-5,"Source-to-avatar basis must be orthonormal");
        require(source.rest.count("hips") && source.current.count("hips"),"Source rest and current hips are required");
        const bool reuse=cached&&cached->matches(source,options);
        RetargetPreparation fresh;auto& preparation=reuse?*cached:fresh;
        auto& source_rest=preparation.source_rest;auto& rest_rotation=preparation.rest_rotation;
        std::map<std::string,M4> source_current;
        std::map<std::string,M3> current_rotation;
        if(!reuse)for (const auto& joint : source.rest) {
            source_rest[joint.first] = affine(joint.second,"Source rest " + joint.first);
            rest_rotation[joint.first] = rotation(source_rest.at(joint.first));
        }
        for (const auto& joint : source.current) {
            if(!source.rest.count(joint.first))throw std::runtime_error("Source current joint lacks rest calibration: " + joint.first);
            source_current[joint.first] = affine(joint.second,"Source current " + joint.first);
            current_rotation[joint.first] = rotation(source_current.at(joint.first));
        }

        const auto count = avatar.nodes.size();
        auto& locals=preparation.locals;auto& rest_world=preparation.rest_world;auto& order=preparation.order;
        auto& semantic=preparation.semantic;
        std::vector<M4> pose(count);
        if(!reuse) {
        locals.resize(count);rest_world.resize(count);semantic.resize(count);
        std::vector<std::vector<std::size_t>> children(count);
        order.reserve(count);
        for (std::size_t i = 0; i < count; ++i) {
            const auto& node = avatar.nodes[i];
            require(node.parent >= -1 && (node.parent == -1 || static_cast<std::size_t>(node.parent) < count),"Invalid node parent");
            require(node.mesh >= -1 && (node.mesh == -1 || static_cast<std::size_t>(node.mesh) < avatar.meshes.size()),"Invalid node mesh");
            require(node.skin >= -1 && (node.skin == -1 || static_cast<std::size_t>(node.skin) < avatar.skins.size()),"Invalid node skin");
            require(node.skin == -1 || node.mesh >= 0,"Skin node requires a mesh");
            locals[i] = affine(node.local,"Avatar local transform");
            rest_world[i] = affine(node.world,"Avatar cached world transform");
            if (node.parent < 0) order.push_back(i); else children[node.parent].push_back(i);
        }
        // Iterative traversal handles arbitrary glTF node ordering and avoids
        // recursive stack growth on externally supplied hierarchies.
        for (std::size_t cursor = 0; cursor < order.size(); ++cursor)
            for (auto child : children[order[cursor]]) order.push_back(child);
        require(order.size() == count,"Avatar node hierarchy contains a cycle");
        for (auto i : order) {
            const int parent = avatar.nodes[i].parent;
            const auto expected = parent < 0 ? locals[i] : multiply(rest_world[parent],locals[i]);
            require(close(rest_world[i],expected),"Avatar local and cached world transforms disagree");
        }
        preparation.constraint_schedule=detail::node_constraint_order(avatar);
        require(avatar.humanoid.count("hips"),"Avatar humanoid hips are required");
        std::set<int> humanoid_nodes;
        for (const auto& joint : avatar.humanoid) {
            require(joint.second >= 0 && static_cast<std::size_t>(joint.second) < count,"Invalid humanoid node");
            require(humanoid_nodes.insert(joint.second).second,"Duplicated humanoid node");
            semantic[joint.second] = joint.first;
        }
        const int hips=avatar.humanoid.at("hips");
        for (const auto& joint : avatar.humanoid) if (joint.first != "hips") {
            int ancestor = avatar.nodes[joint.second].parent;
            while (ancestor >= 0 && ancestor != hips) ancestor = avatar.nodes[ancestor].parent;
            require(ancestor == hips,"Humanoid joint is not below hips: " + joint.first);
        }
        }
        const int hips=avatar.humanoid.at("hips");
        auto& rest_alignment=preparation.rest_alignment;
        if(!reuse)rest_alignment.assign(count,identity_rotation);
        auto align_segment=[&](const std::string& parent_name,const std::string& child_name,M3& alignment){
            const auto target_parent=avatar.humanoid.find(parent_name),target_child=avatar.humanoid.find(child_name);
            const auto source_parent=source_rest.find(parent_name),source_child=source_rest.find(child_name);
            if(target_parent==avatar.humanoid.end() || target_child==avatar.humanoid.end() || source_parent==source_rest.end() || source_child==source_rest.end())return false;
            int ancestor=avatar.nodes[target_child->second].parent;
            while(ancestor>=0 && ancestor!=target_parent->second)ancestor=avatar.nodes[ancestor].parent;
            if(ancestor!=target_parent->second)return false;
            V3 from{},to{};
            for(int i=0;i<3;++i){
                from[i]=rest_world[target_child->second][12+i]-rest_world[target_parent->second][12+i];
                for(int k=0;k<3;++k)to[i]+=basis[k*3+i]*(source_child->second[12+k]-source_parent->second[12+k]);
            }
            return direction_rotation(from,to,alignment);
        };
        if (!reuse&&options.align_rest_directions) for (const auto& joint : avatar.humanoid) {
            for(const auto& child_name:child_candidates(joint.first))
                if(align_segment(joint.first,child_name,rest_alignment[joint.second]))break;
            // A distal finger has no measured outgoing endpoint. Its incoming
            // segment supplies the same rest alignment as its nearest measured
            // same-finger parent, including an optional intermediate gap. Only
            // Q0 transfers; its own global source delta is still applied below.
            for(const auto& parent_name:terminal_finger_parent_candidates(joint.first))
                if(align_segment(parent_name,joint.first,rest_alignment[joint.second]))break;
        }
        // Foot->toe endpoints encode authored ankle pitch (for example heels),
        // not a neutral flat-ground axis. Preserve that rest relationship by
        // carrying the nearest measured leg's Q0 through foot and toe. Only Q0
        // is inherited: each bone still receives its own animated source delta.
        if (!reuse&&options.align_rest_directions) for(const auto& side:{std::string("left"),std::string("right")}) {
            for(const auto& name:{side+"Foot",side+"Toes"}) {
                const auto target=avatar.humanoid.find(name);if(target==avatar.humanoid.end())continue;
                for(const auto& parent_name:{side+"Foot",side+"LowerLeg",side+"UpperLeg"}) {
                    if(parent_name==name)continue;
                    const auto parent=avatar.humanoid.find(parent_name);
                    if(parent==avatar.humanoid.end() || !current_rotation.count(parent_name))continue;
                    int ancestor=avatar.nodes[target->second].parent;
                    while(ancestor>=0 && ancestor!=parent->second)ancestor=avatar.nodes[ancestor].parent;
                    if(ancestor!=parent->second)continue;
                    rest_alignment[target->second]=rest_alignment[parent->second];break;
                }
            }
        }
        auto& inverse_binds=preparation.inverse_binds;
        if(!reuse)inverse_binds.resize(avatar.skins.size());
        if(!reuse)for (std::size_t i = 0; i < avatar.skins.size(); ++i) {
            const auto& skin = avatar.skins[i];
            require(!skin.joints.empty() && skin.joints.size() == skin.inverse_bind.size(),"Invalid skin inverse-bind count");
            std::set<int> joints;
            for (std::size_t j = 0; j < skin.joints.size(); ++j) {
                const int node = skin.joints[j];
                require(node >= 0 && static_cast<std::size_t>(node) < count && joints.insert(node).second,"Invalid or duplicated skin joint");
                inverse_binds[i].push_back(affine(skin.inverse_bind[j],"Skin inverse bind transform"));
            }
        }

        if(!reuse){preparation.avatar_rotation.assign(count,identity_rotation);for(std::size_t i=0;i<count;++i)if(!semantic[i].empty()&&current_rotation.count(semantic[i]))preparation.avatar_rotation[i]=rotation(rest_world[i]);}

        std::array<double,3> hips_delta{};
        const double height_ratio = static_cast<double>(options.avatar_rest_height)/options.source_rest_height;
        const auto& rest_hips = source_rest.at("hips"); const auto& current_hips = source_current.at("hips");
        for (int i = 0; i < 3; ++i) for (int k = 0; k < 3; ++k)
            hips_delta[i] += basis[k*3+i]*(current_hips[12+k]-rest_hips[12+k])*height_ratio;
        for (auto i : order) {
            const int parent = avatar.nodes[i].parent;
            pose[i] = parent < 0 ? locals[i] : multiply(pose[parent],locals[i]);
            const auto& name = semantic[i];
            if (!name.empty() && current_rotation.count(name)) {
                const auto delta = multiply(multiply(multiply(basis,current_rotation.at(name)),transpose(rest_rotation.at(name))),basis_inverse);
                const auto desired = multiply(multiply(delta,rest_alignment[i]),preparation.avatar_rotation[i]);
                const auto correction = multiply(desired,transpose(rotation(pose[i])));
                const auto rotated = multiply(correction,linear(pose[i]));
                for (int c = 0; c < 3; ++c) for (int r = 0; r < 3; ++r) pose[i][c*4+r] = rotated[c*3+r];
            }
            if (static_cast<int>(i) == hips) for (int k = 0; k < 3; ++k) pose[i][12+k] += hips_delta[k];
        }

        evaluate_constraints(avatar,locals,pose,preparation.constraint_schedule);
        RetargetResult result;
        result.node_world.reserve(count);
        for (const auto& matrix : pose) result.node_world.push_back(output(matrix));
        for (std::size_t i = 0; i < count; ++i) {
            const auto& node = avatar.nodes[i];
            if (!node.active || node.mesh < 0 || node.skin < 0) continue;
            SkinPalette palette; palette.mesh_node = static_cast<int>(i); palette.skin = node.skin;
            const auto mesh_inverse = inverse_affine(pose[i]);
            const auto& skin = avatar.skins[node.skin];
            palette.matrices.reserve(skin.joints.size());
            for (std::size_t j = 0; j < skin.joints.size(); ++j)
                palette.matrices.push_back(output(multiply(multiply(mesh_inverse,pose[skin.joints[j]]),inverse_binds[node.skin][j])));
            result.palettes.push_back(std::move(palette));
        }
        if(cached&&!reuse){
            // Publish only a completely validated calibration. A failed new
            // source/options combination leaves the previous cache intact.
            preparation.rest_key=source.rest;preparation.current_keys.reserve(source.current.size());
            for(const auto& entry:source.current)preparation.current_keys.push_back(entry.first);
            preparation.options=options;preparation.ready=true;*cached=std::move(preparation);
        }
        return result;
    } catch (const std::exception& exception) {
        RetargetResult failure; failure.error = exception.what(); return failure;
    }
}

struct Retargeter::Impl {
    Avatar avatar;
    RetargetPreparation preparation;
    explicit Impl(const Avatar& source){
        avatar.version=source.version;avatar.nodes=source.nodes;avatar.skins=source.skins;
        avatar.humanoid=source.humanoid;avatar.constraints=source.constraints;
        // Only mesh-count metadata is used by retarget validation.
        avatar.meshes.resize(source.meshes.size());
    }
};
Retargeter::Retargeter(const Avatar& avatar):impl_(std::make_unique<Impl>(avatar)){}
Retargeter::~Retargeter()=default;
RetargetResult Retargeter::update(const SourcePose& source,const RetargetOptions& options){return retarget_prepared(impl_->avatar,source,options,&impl_->preparation);}
RetargetResult retarget(const Avatar& avatar,const SourcePose& source,const RetargetOptions& options){return retarget_prepared(avatar,source,options,nullptr);}

} // namespace vrm
