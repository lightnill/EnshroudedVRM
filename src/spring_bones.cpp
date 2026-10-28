#include "spring_bones.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <utility>

namespace vrm {
namespace {
using V=std::array<double,3>;
using M=std::array<double,16>;
using Q=std::array<double,4>;
constexpr M identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
constexpr double hair_bend_cosine=.9063077870366499632; // 25 degrees to posed curve.
struct Invalid { const char* why; };
void require(bool condition,const char* why){if(!condition)throw Invalid{why};}
V add(V a,const V& b){for(int c=0;c<3;++c)a[c]+=b[c];return a;}
V sub(V a,const V& b){for(int c=0;c<3;++c)a[c]-=b[c];return a;}
V scale(V a,double s){for(auto& c:a)c*=s;return a;}
double dot(const V& a,const V& b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
double norm(const V& a){return std::sqrt(dot(a,a));}
V cross(const V& a,const V& b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
V unit(const V& a,const V& fallback){const double n=norm(a);return n>1e-12?scale(a,1/n):fallback;}
V position(const M& m){return {m[12],m[13],m[14]};}
V direction(const M& m,const V& p){V r{};for(int c=0;c<3;++c)for(int k=0;k<3;++k)r[c]+=m[k*4+c]*p[k];return r;}
V point(const M& m,const V& p){return add(direction(m,p),position(m));}
M multiply(const M& a,const M& b){M r{};for(int c=0;c<4;++c)for(int row=0;row<4;++row)for(int k=0;k<4;++k)r[c*4+row]+=a[k*4+row]*b[c*4+k];return r;}
M inverse(const M& m) {
    const double a=m[0],b=m[4],c=m[8],d=m[1],e=m[5],f=m[9],g=m[2],h=m[6],i=m[10];
    const double determinant=a*(e*i-f*h)-b*(d*i-f*g)+c*(d*h-e*g);
    const double magnitude=std::sqrt((a*a+d*d+g*g)*(b*b+e*e+h*h)*(c*c+f*f+i*i));
    require(std::isfinite(determinant)&&magnitude>0&&std::abs(determinant)>magnitude*1e-8,"Singular spring transform");
    M r=identity;const double s=1/determinant;
    r[0]=(e*i-f*h)*s;r[4]=(c*h-b*i)*s;r[8]=(b*f-c*e)*s;
    r[1]=(f*g-d*i)*s;r[5]=(a*i-c*g)*s;r[9]=(c*d-a*f)*s;
    r[2]=(d*h-e*g)*s;r[6]=(b*g-a*h)*s;r[10]=(a*e-b*d)*s;
    const auto t=scale(direction(r,position(m)),-1);for(int k=0;k<3;++k)r[12+k]=t[k];return r;
}
M matrix(const Matrix& input) {
    M m{};for(int c=0;c<16;++c){require(std::isfinite(input[c]),"Nonfinite spring transform");m[c]=input[c];}
    require(std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)<1e-5,"Nonaffine spring transform");
    return m;
}
Matrix output(const M& m) {
    Matrix out{};for(int c=0;c<16;++c){require(std::isfinite(m[c])&&std::abs(m[c])<std::numeric_limits<float>::max(),"Nonfinite spring output");out[c]=static_cast<float>(m[c]);}return out;
}
double max_scale(const M& m){double s=0;for(int c=0;c<3;++c)s=std::max(s,norm({m[c*4],m[c*4+1],m[c*4+2]}));return s;}
V frame_side(const V& axis,const M& frame);
Q swing(const V& from,const V& to,const M& frame) {
    const double cosine=std::clamp(dot(from,to),-1.0,1.0);
    if(cosine<-.99999999) {
        const auto axis=unit(cross(from,frame_side(from,frame)),{0,0,1});return {axis[0],axis[1],axis[2],0};
    }
    const auto axis=cross(from,to);const double w=1+cosine,n=std::sqrt(dot(axis,axis)+w*w);
    require(n>1e-12,"Invalid spring swing");return {axis[0]/n,axis[1]/n,axis[2]/n,w/n};
}
V rotate(const Q& q,const V& p){const V v{q[0],q[1],q[2]};const auto t=scale(cross(v,p),2);return add(p,add(scale(t,q[3]),cross(v,t)));}
double qdot(const Q& a,const Q& b){double n=0;for(int c=0;c<4;++c)n+=a[c]*b[c];return n;}
Q normalized(Q q){const double n=std::sqrt(qdot(q,q));require(std::isfinite(n)&&n>1e-12,"Invalid spring rotation");for(auto& c:q)c/=n;return q;}
Q compose(const Q& a,const Q& b){const V av{a[0],a[1],a[2]},bv{b[0],b[1],b[2]};const auto v=add(add(scale(av,b[3]),scale(bv,a[3])),cross(av,bv));return {v[0],v[1],v[2],a[3]*b[3]-dot(av,bv)};}
Q rotation_quaternion(const M& m) {
    Q q{};const double trace=m[0]+m[5]+m[10];
    if(trace>0){const double s=2*std::sqrt(trace+1);q={(m[6]-m[9])/s,(m[8]-m[2])/s,(m[1]-m[4])/s,.25*s};}
    else if(m[0]>m[5]&&m[0]>m[10]){const double s=2*std::sqrt(std::max(0.0,1+m[0]-m[5]-m[10]));require(s>1e-12,"Invalid spring rotation");q={.25*s,(m[4]+m[1])/s,(m[8]+m[2])/s,(m[6]-m[9])/s};}
    else if(m[5]>m[10]){const double s=2*std::sqrt(std::max(0.0,1+m[5]-m[0]-m[10]));require(s>1e-12,"Invalid spring rotation");q={(m[4]+m[1])/s,.25*s,(m[9]+m[6])/s,(m[8]-m[2])/s};}
    else{const double s=2*std::sqrt(std::max(0.0,1+m[10]-m[0]-m[5]));require(s>1e-12,"Invalid spring rotation");q={(m[8]+m[2])/s,(m[9]+m[6])/s,.25*s,(m[1]-m[4])/s};}
    return normalized(q);
}
double twist_angle(const V& posed_axis,const V& axis,const M& posed,const M& solved,const Q& preview={0,0,0,1}) {
    unsigned column=0;double best=2;
    for(unsigned k=0;k<3;++k){const auto v=unit({posed[k*4],posed[k*4+1],posed[k*4+2]},{1,0,0});const double alignment=std::abs(dot(v,posed_axis));if(alignment<best-1e-7){column=k;best=alignment;}}
    const V before{posed[column*4],posed[column*4+1],posed[column*4+2]},after=rotate(preview,{solved[column*4],solved[column*4+1],solved[column*4+2]});
    const auto expected=rotate(swing(posed_axis,axis,posed),unit(sub(before,scale(posed_axis,dot(before,posed_axis))),{1,0,0}));
    const auto actual=unit(sub(after,scale(axis,dot(after,axis))),expected);
    return std::atan2(dot(axis,cross(expected,actual)),dot(expected,actual));
}
void rotate_about(M& m,const Q& rotation,const V& pivot) {
    for(int c=0;c<3;++c){const auto v=rotate(rotation,{m[c*4],m[c*4+1],m[c*4+2]});for(int r=0;r<3;++r)m[c*4+r]=v[r];}
    const auto p=add(pivot,rotate(rotation,sub(position(m),pivot)));for(int c=0;c<3;++c)m[12+c]=p[c];
}
bool finite(const V& v){return std::all_of(v.begin(),v.end(),[](double n){return std::isfinite(n);});}
V closest_on_segment(const V& start,const V& end,const V& p) {
    const auto axis=sub(end,start);const double length2=dot(axis,axis);
    return length2>1e-20?add(start,scale(axis,std::clamp(dot(sub(p,start),axis)/length2,0.0,1.0))):start;
}
V capsule_contact(const V& root,const V& next,const V& start,const V& end) {
    // Closest point on the capsule axis to the complete hair segment.
    const auto hair=sub(next,root),axis=sub(end,start),offset=sub(root,start);
    const double a=dot(hair,hair),b=dot(hair,axis),c=dot(axis,axis),d=dot(hair,offset),e=dot(axis,offset);
    if(c<=1e-20)return start;
    const double determinant=a*c-b*b;
    const double s=determinant>a*c*1e-12?std::clamp((b*e-c*d)/determinant,0.0,1.0):0;
    return add(start,scale(axis,std::clamp((b*s+e)/c,0.0,1.0)));
}
V frame_side(const V& axis,const M& frame) {
    auto seed=unit({frame[0],frame[1],frame[2]},{1,0,0});double best=std::abs(dot(seed,axis));
    for(int column=1;column<3;++column) {
        const auto candidate=unit({frame[column*4],frame[column*4+1],frame[column*4+2]},{0,1,0});
        const double alignment=std::abs(dot(candidate,axis));
        if(alignment<best-1e-7){seed=candidate;best=alignment;}
    }
    return sub(seed,scale(axis,dot(seed,axis)));
}
V limit_bend(const V& root,const V& next,const V& axis,const M& frame) {
    constexpr double minimum_cosine=hair_bend_cosine;
    const auto offset=sub(next,root);const double length=norm(offset);const auto desired=unit(offset,axis);
    const double cosine=dot(desired,axis);if(cosine>=minimum_cosine)return next;
    auto sideways=sub(desired,scale(axis,cosine));if(norm(sideways)<1e-10)sideways=frame_side(axis,frame);
    const auto limited=add(scale(axis,minimum_cosine),scale(unit(sideways,{0,1,0}),std::sqrt(1-minimum_cosine*minimum_cosine)));
    return add(root,scale(limited,length));
}
V avoid_sphere(const V& root,const V& next,const V& center,double radius,const V& fallback,const M& frame,bool segment=true) {
    const auto towards=sub(center,root);const double distance=norm(towards),length=norm(sub(next,root));
    if(radius<=0||distance<1e-12||length<1e-12)return next;
    const bool outside=distance>=radius;
    const auto closest=outside&&segment?closest_on_segment(root,next,center):next;
    if(norm(sub(closest,center))>=radius)return next;
    // Rotate the fixed-length segment to the sphere's tangent cone. Pushing
    // the tip out and then restoring length can put it straight back inside.
    // An embedded attachment cannot clear its entire segment; clear its tip
    // when reachable, preserving the natural attachment and bone length.
    const double tangent2=std::max(0.0,distance*distance-radius*radius);
    const double limit=outside&&segment&&length*length>=tangent2
        ?std::sqrt(tangent2)/distance
        :(distance*distance+length*length-radius*radius)/(2*distance*length);
    if(limit< -1||limit>=1)return next; // No reachable surface, or no contact.
    const auto axis=scale(towards,1/distance),desired=unit(sub(next,root),fallback);
    auto sideways=sub(desired,scale(axis,dot(desired,axis)));
    if(norm(sideways)<1e-10)sideways=sub(fallback,scale(axis,dot(fallback,axis)));
    if(norm(sideways)<1e-10)sideways=frame_side(axis,frame);
    const auto direction=add(scale(axis,limit),scale(unit(sideways,{0,1,0}),std::sqrt(std::max(0.0,1-limit*limit))));
    return add(root,scale(direction,length));
}
struct Contact {V start,end;double radius;bool capsule,full,reachable;};
bool blocks(const V& root,const V& next,const Contact& c) {
    if(!c.reachable)return false; // Attachment is too deep for any reachable tip.
    const auto center=!c.capsule?c.start:(c.full?capsule_contact(root,next,c.start,c.end):closest_on_segment(c.start,c.end,next));
    const auto away=sub(c.full?closest_on_segment(root,next,center):next,center);
    return dot(away,away)<c.radius*c.radius*(1-1e-10);
}
V solve_contacts(const V& root,V next,const V& unconstrained,const V& fallback,const M& frame,const Contact* contacts,std::size_t count,double bend_cosine=-1) {
    const auto limited_input=next;
    for(unsigned pass=0;pass<2;++pass)for(std::size_t i=0;i<count;++i) {
        const auto& c=contacts[i];if(!c.reachable)continue;
        const auto center=!c.capsule?c.start:(c.full?capsule_contact(root,next,c.start,c.end):closest_on_segment(c.start,c.end,next));
        next=avoid_sphere(root,next,center,c.radius,fallback,frame,c.full);
    }
    auto blocked=[&](const V& point){for(std::size_t i=0;i<count;++i)if(blocks(root,point,contacts[i]))return true;return false;};
    std::size_t first=0;while(first<count&&!blocks(root,next,contacts[first]))++first;
    const double length=norm(sub(next,root));
    const bool within_bend=bend_cosine<0||dot(unit(sub(next,root),fallback),fallback)>=bend_cosine-1e-9;
    if(first==count&&within_bend)return next;
    auto approach_clear=[&](const V& start,const V& safe_direction) {
        if(!blocked(start))return start;
        const auto desired=unit(sub(start,root),fallback);double low=0,high=1;V resolved=add(root,scale(safe_direction,length));
        for(unsigned iteration=0;iteration<24;++iteration) {
            const double middle=(low+high)*.5;const auto direction=unit(add(scale(desired,1-middle),scale(safe_direction,middle)),safe_direction);
            const auto candidate=add(root,scale(direction,length));
            if(blocked(candidate))low=middle;else{high=middle;resolved=candidate;}
        }
        return resolved;
    };
    if(bend_cosine>=0) {
        // Search the authored bend cone before accepting an extreme escape.
        // Interpolating between two directions in this small cone keeps
        // the bend valid. Body clearance remains mandatory on every candidate.
        if(!blocked(add(root,scale(fallback,length))))return approach_clear(limited_input,fallback);
        const auto desired=unit(sub(limited_input,root),fallback);V shape_safe{};double shape_best=-2;
        auto consider_shape=[&](const V& direction) {
            const auto d=unit(direction,fallback);const double alignment=dot(d,desired);
            if(dot(d,fallback)>=bend_cosine-1e-9&&alignment>shape_best+1e-7&&!blocked(add(root,scale(d,length)))){shape_safe=d;shape_best=alignment;}
        };
        consider_shape(sub(limited_input,root));consider_shape(sub(next,root));
        // A narrow clear arc can fall between regular cone samples. Include
        // exact contact/cone boundary intersections before relaxing the shape.
        // Capsule-derived candidates still have to clear the complete capsule.
        for(std::size_t i=0;i<count;++i) {
            const auto& c=contacts[i];if(!c.reachable)continue;
            const auto center=!c.capsule?c.start:(c.full?capsule_contact(root,limited_input,c.start,c.end):closest_on_segment(c.start,c.end,limited_input));
            consider_shape(sub(avoid_sphere(root,limited_input,center,c.radius,fallback,frame,c.full),root));
            const auto towards=sub(center,root);const double distance=norm(towards);
            if(distance<1e-12||length<1e-12)continue;
            const double tangent2=std::max(0.0,distance*distance-c.radius*c.radius);
            const double limit=distance>=c.radius&&c.full&&length*length>=tangent2?std::sqrt(tangent2)/distance:(distance*distance+length*length-c.radius*c.radius)/(2*distance*length);
            if(limit< -1||limit>=1)continue;
            const auto axis=scale(towards,1/distance);const double alignment=dot(axis,fallback),determinant=1-alignment*alignment;
            if(determinant<1e-12)continue;
            const auto base=add(scale(fallback,(bend_cosine-alignment*limit)/determinant),scale(axis,(limit-alignment*bend_cosine)/determinant));
            const double remaining=1-dot(base,base);if(remaining< -1e-10)continue;
            const auto side=scale(unit(cross(fallback,axis),{0,1,0}),std::sqrt(std::max(0.0,remaining)));
            consider_shape(add(base,side));consider_shape(sub(base,side));
        }
        const auto side=unit(frame_side(fallback,frame),{0,1,0}),other=unit(cross(fallback,side),{0,0,1});
        for(double cosine:{std::sqrt((1+bend_cosine)*.5),bend_cosine}) {
            const double sine=std::sqrt(std::max(0.0,1-cosine*cosine));
            for(int sample=0;sample<16;++sample) {
                const int cardinal_first=(sample%4)*4+sample/4;const double angle=cardinal_first*.3926990816987241548;
                consider_shape(add(scale(fallback,cosine),add(scale(side,sine*std::cos(angle)),scale(other,sine*std::sin(angle)))));
            }
        }
        if(shape_best>=-1)return approach_clear(limited_input,shape_safe);
        // No sampled bend-constrained direction clears the body. Prefer body
        // clearance, favoring the authored axis over a sideways escape.
    }
    // A capsule's closest axis point can move after a projection, and a later
    // overlapping collider can undo an earlier correction. Find a direction
    // that clears the COMPLETE shapes, then approach that safe direction with
    // a fixed 24-step search. Accepted endpoints clear all reachable constraints.
    const auto desired=bend_cosine>=0?fallback:unit(sub(next,root),fallback);
    V safe{};double best=-2;
    auto consider=[&](const V& direction){
        if(norm(direction)<1e-12)return;
        const auto d=unit(direction,fallback);const double cosine=std::clamp(dot(d,desired),-1.0,1.0);
        if(cosine>best+1e-7&&!blocked(add(root,scale(d,length)))){safe=d;best=cosine;}
    };
    consider(sub(next,root));
    // Shape limiting must not discard a collision-free incoming direction.
    // Keep it available even when all bounded fallback samples are blocked.
    consider(sub(unconstrained,root));
    consider(fallback);
    const auto& c=contacts[first<count?first:0];
    consider(sub(root,c.capsule?closest_on_segment(c.start,c.end,root):c.start));
    // Attachment-local directions remain consistent through actor turns. A
    // world-axis search makes neighboring strands choose different escape
    // routes when the same character merely faces a different direction.
    for(int x=-1;x<=1;++x)for(int y=-1;y<=1;++y)for(int z=-1;z<=1;++z)
        if(x||y||z)consider(direction(frame,{double(x),double(y),double(z)}));
    if(best< -1)return next; // No sampled feasible direction; keep bounded solve.
    double low=0,high=1;V resolved=add(root,scale(safe,length));
    for(unsigned iteration=0;iteration<24;++iteration) {
        const double middle=(low+high)*.5;
        const auto direction=unit(add(scale(desired,1-middle),scale(safe,middle)),safe);
        const auto candidate=add(root,scale(direction,length));
        if(blocked(candidate))low=middle;else{high=middle;resolved=candidate;}
    }
    return resolved;
}
} // namespace

struct SpringBones::Impl {
    struct Joint {SpringJoint settings;V local_tail{};std::vector<std::size_t> descendants,neighbours;bool hair=false;double airflow_weight=0;};
    struct State {V current{},previous{};};
    const Avatar& avatar;
    const SpringMotion motion;
    std::vector<Joint> joints;
    std::vector<State> state;
    std::vector<M> baseline,world,collision_baseline,render_baseline;
    std::vector<bool> changed;
    std::vector<bool> limb_colliders;
    std::vector<Q> section_rotations;
    std::vector<V> section_tails;
    std::vector<double> section_twists;
    std::vector<bool> section_changed;
    std::string error;
    bool initialized=false,cached_render=false;
    std::uint64_t time=0,epoch=0,actor=0;
    V previous_anchor{},previous_actor_position{},air_velocity{};
    V inertial_pending_carry{},render_inertial_carry{};
    double accumulator=0,inertial_follow=0,cohesion_gain=0;
    explicit Impl(const Avatar& a,SpringMotion preference):avatar(a),motion(preference) {}
    bool ancestor(int a,int b)const {
        for(std::size_t i=0;b>=0&&i<=avatar.nodes.size();++i){if(a==b)return true;b=avatar.nodes[b].parent;}return false;
    }
    void compile() {
        require(motion==SpringMotion::Authored||motion==SpringMotion::Relaxed||motion==SpringMotion::Airflow||motion==SpringMotion::Inertial,"Invalid spring motion preference");
        require(avatar.nodes.size()<=2048&&avatar.spring_joints.size()<=256&&avatar.spring_colliders.size()<=256,"Spring topology limit");
        const auto count=avatar.nodes.size();baseline.resize(count);world.resize(count);collision_baseline.resize(count);render_baseline.resize(count);changed.resize(count);
        auto valid_node=[&](int n){return n>=0&&static_cast<std::size_t>(n)<count;};
        // Validate the complete parent table before following any indirect edge.
        for(std::size_t n=0;n<count;++n)
            require(avatar.nodes[n].parent==-1||valid_node(avatar.nodes[n].parent),"Invalid spring parent");
        for(std::size_t n=0;n<count;++n) {
            int at=static_cast<int>(n);std::size_t depth=0;
            for(;at>=0&&depth<=count;++depth)at=avatar.nodes[at].parent;
            require(depth<=count,"Cyclic spring hierarchy");inverse(matrix(avatar.nodes[n].world));
        }
        std::size_t palette_work=0;
        for(const auto& n:avatar.nodes)if(n.active&&n.mesh>=0&&n.skin>=0) {
            require(static_cast<std::size_t>(n.skin)<avatar.skins.size(),"Invalid spring skin");
            palette_work+=avatar.skins[n.skin].joints.size();require(palette_work<=8192,"Spring palette work limit");
        }
        std::vector<bool> assigned(count);
        std::size_t visits=0,collisions=0;
        for(const auto& source:avatar.spring_joints) {
            require(valid_node(source.node)&&!assigned[source.node],"Invalid/duplicated spring joint");assigned[source.node]=true;
            require(source.center==-1||(valid_node(source.center)&&ancestor(source.center,source.node)),"Invalid spring center");
            require(source.child==-1||(valid_node(source.child)&&source.child!=source.node&&ancestor(source.node,source.child)),"Invalid spring child");
            require(source.child>=0||avatar.version==VrmVersion::V0,"VRM1 spring requires explicit tail");
            for(const auto& h:avatar.humanoid)require(valid_node(h.second)&&!ancestor(source.node,h.second),"Spring owns humanoid bone");
            require(std::isfinite(source.stiffness)&&source.stiffness>=0&&source.stiffness<=10000&&std::isfinite(source.drag)&&source.drag>=0&&source.drag<=1&&std::isfinite(source.gravity_power)&&source.gravity_power>=0&&source.gravity_power<=10000&&std::isfinite(source.hit_radius)&&source.hit_radius>=0&&source.hit_radius<=100,"Invalid spring settings");
            for(auto v:source.gravity)require(std::isfinite(v)&&std::abs(v)<=10000,"Invalid spring gravity");
            Joint j;j.settings=source;
            const auto head_bone=avatar.humanoid.find("head");
            j.hair=head_bone!=avatar.humanoid.end()&&ancestor(head_bone->second,source.node);
            // World-space tails let root movement drive inertia and damping.
            // Change only compiled hair settings; keep authored metadata intact.
            // Airflow weights and speed forces are exclusive to Airflow mode.
            if(j.hair&&motion==SpringMotion::Inertial)j.settings.center=-1;
            if(motion!=SpringMotion::Authored) {
                // Resolve attachment once from the validated humanoid hierarchy,
                // so unfamiliar bone names do not restore excessive body sway.
                const bool hair=j.hair;
                j.settings.drag=1-(1-source.drag)*(hair?.5f:.15f);
                j.settings.stiffness=std::min(10000.f,std::max(hair?2.f:8.f,source.stiffness*(hair?4.f:16.f)));
            }
            const auto head=matrix(avatar.nodes[source.node].world);
            if(source.child>=0)j.local_tail=point(inverse(head),position(matrix(avatar.nodes[source.child].world)));
            else {const auto& local=avatar.nodes[source.node].local;j.local_tail=scale(unit({local[12],local[13],local[14]},{0,1,0}),.07);}
            require(source.colliders.size()<=64,"Spring collision limit");
            for(auto c:source.colliders)require(c<avatar.spring_colliders.size(),"Invalid spring collider index");
            for(std::size_t n=0;n<count;++n)if(ancestor(source.node,static_cast<int>(n)))j.descendants.push_back(n);
            visits+=j.descendants.size();collisions+=source.colliders.size()*2;
            require(visits<=16384&&collisions<=16384,"Spring work limit");
            if(norm(direction(head,j.local_tail))>1e-8)joints.push_back(std::move(j));
        }
        limb_colliders.resize(avatar.spring_colliders.size());std::size_t collider_index=0;
        for(const auto& c:avatar.spring_colliders) {
            require(valid_node(c.node)&&std::isfinite(c.radius)&&c.radius>=0&&c.radius<=100,"Invalid spring collider");
            for(int k=0;k<3;++k)require(std::isfinite(c.offset[k])&&std::isfinite(c.tail[k]),"Invalid spring collider vector");
            for(const auto& j:joints)require(!ancestor(j.settings.node,c.node),"Spring collider depends on simulated subtree");
            for(const char* name:{"leftUpperArm","rightUpperArm"}) {
                const auto arm=avatar.humanoid.find(name);
                if(arm!=avatar.humanoid.end()&&valid_node(arm->second)&&ancestor(arm->second,c.node))limb_colliders[collider_index]=true;
            }
            ++collider_index;
        }
        for(const auto& j:joints)if(j.settings.center>=0)
            for(const auto& owner:joints)if(ancestor(owner.settings.node,j.settings.center))
                require(owner.settings.node==j.settings.center&&owner.settings.center==j.settings.center,"Spring center depends on another simulation chain");
        std::stable_sort(joints.begin(),joints.end(),[&](const Joint& a,const Joint& b){
            auto depth=[&](int n){unsigned d=0;for(;n>=0;n=avatar.nodes[n].parent)++d;return d;};return depth(a.settings.node)<depth(b.settings.node);
        });
        if(motion==SpringMotion::Airflow)for(auto& j:joints)if(j.hair) {
            unsigned depth=0;for(const auto& owner:joints)if(owner.hair&&owner.settings.node!=j.settings.node&&ancestor(owner.settings.node,j.settings.node))++depth;
            j.airflow_weight=.4+.15*std::min(depth,4u);
        }
        if(motion==SpringMotion::Inertial) {
            const auto size=joints.size();std::vector<std::size_t> roots(size);std::vector<double> arc(size),lengths(size);
            for(std::size_t i=0;i<size;++i)if(joints[i].hair) {
                roots[i]=i;
                for(std::size_t k=0;k<i;++k)if(joints[k].hair&&ancestor(joints[k].settings.node,joints[i].settings.node)){roots[i]=roots[k];break;}
                const auto& j=joints[i];arc[i]=norm(direction(matrix(avatar.nodes[j.settings.node].world),j.local_tail));
                for(int n=j.settings.node;n!=joints[roots[i]].settings.node;n=avatar.nodes[n].parent)
                    arc[i]+=norm(sub(position(matrix(avatar.nodes[n].world)),position(matrix(avatar.nodes[avatar.nodes[n].parent].world))));
                lengths[roots[i]]=std::max(lengths[roots[i]],arc[i]);
            }
            struct Candidate {double score;std::size_t a,b;};std::vector<Candidate> candidates;
            auto bind_tail=[&](const Joint& j){return point(matrix(avatar.nodes[j.settings.node].world),j.local_tail);};
            for(std::size_t i=0;i<size;++i)if(joints[i].hair)for(std::size_t k=i+1;k<size;++k)if(joints[k].hair&&roots[i]!=roots[k]) {
                const double minimum=std::min(lengths[roots[i]],lengths[roots[k]]),maximum=std::max(lengths[roots[i]],lengths[roots[k]]);
                const double root_distance=norm(sub(position(matrix(avatar.nodes[joints[roots[i]].settings.node].world)),position(matrix(avatar.nodes[joints[roots[k]].settings.node].world))));
                const double progress=std::abs(arc[i]/lengths[roots[i]]-arc[k]/lengths[roots[k]]);
                const auto first=unit(direction(matrix(avatar.nodes[joints[i].settings.node].world),joints[i].local_tail),{0,1,0});
                const auto second=unit(direction(matrix(avatar.nodes[joints[k].settings.node].world),joints[k].local_tail),{0,1,0});
                if(minimum<maximum*.5||root_distance>minimum*.65||progress>.2||dot(first,second)<.5)continue;
                candidates.push_back({progress*2+(root_distance+norm(sub(bind_tail(joints[i]),bind_tail(joints[k]))))/minimum,i,k});
            }
            std::sort(candidates.begin(),candidates.end(),[&](const Candidate& a,const Candidate& b){
                if(a.score!=b.score)return a.score<b.score;
                const auto first=std::minmax(joints[a.a].settings.node,joints[a.b].settings.node),second=std::minmax(joints[b.a].settings.node,joints[b.b].settings.node);
                return first<second;
            });
            for(const auto& c:candidates)if(joints[c.a].neighbours.size()<2&&joints[c.b].neighbours.size()<2){joints[c.a].neighbours.push_back(c.b);joints[c.b].neighbours.push_back(c.a);}
            section_rotations.resize(size);section_tails.resize(size);section_twists.resize(size);section_changed.resize(count);
        }
        state.resize(joints.size());
    }
    M center(const Joint& j)const {return j.settings.center<0?identity:baseline[j.settings.center];}
    V tail(const Joint& j,const std::vector<M>& transforms)const {
        return j.settings.child>=0?position(transforms[j.settings.child]):point(transforms[j.settings.node],j.local_tail);
    }
    V contact_tail(const Joint& j,const V& root,V next,const V& unconstrained,const V& fallback)const {
        const auto& settings=j.settings;const bool preserve_shape=j.hair&&motion==SpringMotion::Relaxed;
        std::array<Contact,64> contacts;std::size_t count=0;
        const double length=norm(sub(next,root)),hair_radius=settings.hit_radius*max_scale(world[settings.node]);
        for(auto index:settings.colliders) {
            // Relaxed hair releases arms; other modes retain their chosen contacts.
            if(preserve_shape&&limb_colliders[index])continue;
            const auto& c=avatar.spring_colliders[index];const auto& cm=baseline[c.node];auto& contact=contacts[count++];
            contact.start=point(cm,{c.offset[0],c.offset[1],c.offset[2]});contact.end=point(cm,{c.tail[0],c.tail[1],c.tail[2]});
            contact.radius=c.radius*max_scale(cm)+hair_radius;contact.capsule=c.capsule;
            const auto closest=c.capsule?closest_on_segment(contact.start,contact.end,root):contact.start;
            const double distance=norm(sub(root,closest));
            // Preserve authored hair-tip arm contact without anchoring mid-strand.
            contact.full=distance>=contact.radius*(1-1e-10)&&!(j.hair&&limb_colliders[index]&&motion!=SpringMotion::Airflow);
            contact.reachable=distance+length>=contact.radius*(1-1e-12);
        }
        if(motion==SpringMotion::Inertial) {
            bool clear=true;for(std::size_t i=0;i<count;++i)if(blocks(root,next,contacts[i])){clear=false;break;}
            if(clear)return next;
        }
        return count?solve_contacts(root,next,unconstrained,fallback,baseline[settings.node],contacts.data(),count,preserve_shape?hair_bend_cosine:-1):next;
    }
    void cohere_sections() {
        if(cohesion_gain<=0)return;
        bool linked=false;std::fill(section_changed.begin(),section_changed.end(),false);
        for(std::size_t i=0;i<joints.size();++i)if(joints[i].hair) {
            section_tails[i]=tail(joints[i],world);
            if(!joints[i].neighbours.empty()){section_rotations[i]=rotation_quaternion(multiply(world[joints[i].settings.node],inverse(baseline[joints[i].settings.node])));linked=true;}
        }
        if(!linked)return;
        for(std::size_t i=0;i<joints.size();++i)if(joints[i].hair) {
            const auto& j=joints[i];const int node=j.settings.node;const auto root=position(world[node]);bool moved=section_changed[node];
            auto apply=[&](const Q& rotation){for(auto n:j.descendants){rotate_about(world[n],rotation,root);section_changed[n]=changed[n]=true;}moved=true;};
            if(!j.neighbours.empty()) {
                const auto& own=section_rotations[i];Q average{};
                for(int c=0;c<4;++c)average[c]=own[c]*(1-cohesion_gain);
                for(auto other:j.neighbours){const auto& q=section_rotations[other];const double weight=cohesion_gain/j.neighbours.size()*(qdot(own,q)<0?-1:1);for(int c=0;c<4;++c)average[c]+=q[c]*weight;}
                average=normalized(average);
                const auto current=rotation_quaternion(multiply(world[node],inverse(baseline[node])));
                if(std::abs(qdot(current,average))<1-1e-14)apply(compose(average,{-current[0],-current[1],-current[2],current[3]}));
            }
            if(moved&&!j.settings.colliders.empty()) {
                const auto before=tail(j,world),axis=unit(sub(before,root),{0,1,0});const auto clear=contact_tail(j,root,before,before,axis);
                if(norm(sub(before,clear))>1e-12)apply(swing(axis,unit(sub(clear,root),axis),baseline[node]));
            }
        }
        // Constraint projection is a shared shift of both Verlet points, not a
        // new force. Retain the solved roll for the render projection as well.
        for(std::size_t i=0;i<joints.size();++i)if(joints[i].hair){const auto& j=joints[i];const auto correction=sub(tail(j,world),section_tails[i]);state[i].current=add(state[i].current,correction);state[i].previous=add(state[i].previous,correction);
            if(section_changed[j.settings.node])section_twists[i]=twist_angle(unit(sub(tail(j,baseline),position(baseline[j.settings.node])),{0,1,0}),unit(sub(tail(j,world),position(world[j.settings.node])),{0,1,0}),baseline[j.settings.node],world[j.settings.node]);
        }
    }
    void reset(const SpringFrame& frame,const V& anchor,const V& actor_position) {
        for(std::size_t i=0;i<joints.size();++i) {
            const auto& j=joints[i];
            state[i].current=state[i].previous=point(inverse(center(j)),tail(j,baseline));
        }
        time=frame.timestamp_ms;epoch=frame.source_epoch;actor=frame.actor_id;previous_anchor=anchor;accumulator=0;initialized=true;
        collision_baseline=baseline;
        cached_render=false;
        previous_actor_position=actor_position;air_velocity={};inertial_follow=cohesion_gain=0;
        inertial_pending_carry=render_inertial_carry={};
    }
    SpringUpdate update(RetargetResult& pose,const SpringFrame& frame) {
        SpringUpdate result;if(joints.empty())return result;
        require(pose&&pose.node_world.size()==avatar.nodes.size(),"Invalid spring input pose");require(finite(frame.origin_delta),"Invalid spring origin delta");
        // Bound incoming storage before touching history or deep-copying a pose.
        require(pose.palettes.size()<=avatar.nodes.size(),"Spring palette count limit");
        std::size_t input_work=0;
        for(const auto& palette:pose.palettes) {
            require(palette.mesh_node>=0&&static_cast<std::size_t>(palette.mesh_node)<avatar.nodes.size()&&palette.skin>=0&&static_cast<std::size_t>(palette.skin)<avatar.skins.size(),"Invalid spring skin palette");
            const auto& skin=avatar.skins[palette.skin];require(palette.matrices.size()==skin.joints.size()&&skin.joints.size()==skin.inverse_bind.size(),"Invalid spring palette size");
            input_work+=palette.matrices.size();require(input_work<=8192,"Spring palette work limit");
        }
        const auto actor_matrix=matrix(frame.model_to_world),actor_inverse=inverse(actor_matrix);
        const auto actor_position=position(actor_matrix);
        for(std::size_t n=0;n<baseline.size();++n){const auto m=matrix(pose.node_world[n]);inverse(m);baseline[n]=multiply(actor_matrix,m);}
        const auto hips=avatar.humanoid.find("hips");const auto anchor=hips==avatar.humanoid.end()?position(actor_matrix):position(baseline[hips->second]);
        if(initialized) {
            previous_anchor=sub(previous_anchor,frame.origin_delta);
            previous_actor_position=sub(previous_actor_position,frame.origin_delta);
            for(std::size_t i=0;i<state.size();++i)if(joints[i].settings.center<0){state[i].current=sub(state[i].current,frame.origin_delta);state[i].previous=sub(state[i].previous,frame.origin_delta);}
            for(auto& m:collision_baseline)for(int c=0;c<3;++c)m[12+c]-=frame.origin_delta[c];
            if(cached_render)for(std::size_t n=0;n<world.size();++n)for(int c=0;c<3;++c){world[n][12+c]-=frame.origin_delta[c];render_baseline[n][12+c]-=frame.origin_delta[c];}
        }
        if(!initialized||frame.source_epoch!=epoch||frame.actor_id!=actor||frame.timestamp_ms<time||frame.timestamp_ms-time>250||norm(sub(anchor,previous_anchor))>3) {
            reset(frame,anchor,actor_position);result.reset=true;return result;
        }
        const bool inertial=motion==SpringMotion::Inertial;
        const double sample_dt=static_cast<double>(frame.timestamp_ms-time)*.001;
        const auto actor_travel=sub(actor_position,previous_actor_position);
        V inertial_sample_carry{};
        if(inertial) {
            if(sample_dt>0) {
                const double speed=norm({actor_travel[0],0,actor_travel[2]})/sample_dt;
                // Keep effective travel increasing with speed throughout the ramp.
                inertial_follow=speed>2?std::min(.6,.95*(speed-2)/speed):0;
            }
            inertial_sample_carry={actor_travel[0]*inertial_follow,0,actor_travel[2]*inertial_follow};
            // Integrate each sample with its own fraction. A later slow/idle
            // sample must not reweight travel queued before the next fixed tick.
            // This is a displacement, so camera-origin rebases leave it intact.
            if(sample_dt>0)inertial_pending_carry=add(inertial_pending_carry,inertial_sample_carry);
        }
        const V duplicate_carry=inertial&&sample_dt==0?inertial_sample_carry:V{};
        if(motion==SpringMotion::Airflow&&frame.timestamp_ms>time) {
            // Actor travel, rather than animated hips/head bob or camera motion,
            // supplies relative airflow. Rebased history makes a following camera
            // neutral. Duplicate samples never advance the velocity filter.
            const double dt=static_cast<double>(frame.timestamp_ms-time)*.001;
            auto observed=scale(sub(actor_position,previous_actor_position),1/dt);observed[1]=0;
            const double speed=norm(observed);
            if(speed<.15||speed>20)observed={};
            else if(speed>8)observed=scale(observed,8/speed);
            const double blend=1-std::exp(-dt/.15);
            air_velocity=add(scale(air_velocity,1-blend),scale(observed,blend));
            if(norm(air_velocity)<1e-5)air_velocity={};
            previous_actor_position=actor_position;
        }
        accumulator+=static_cast<double>(frame.timestamp_ms-time)*.001;time=frame.timestamp_ms;previous_anchor=anchor;
        constexpr double step=1.0/60.0;
        result.steps=std::min(4u,static_cast<unsigned>(std::floor((accumulator+1e-10)/step)));
        if(result.steps==4)accumulator=std::fmod(accumulator,step);else accumulator-=result.steps*step;
        // Keep walking gentle; strengthen the existing sweep progressively
        // across running speeds without changing its direction or smoothing.
        const double airflow_gain=.08*(1+.25*std::clamp((norm(air_velocity)-2)/3,0.0,1.0));
        // A newly animated body can contact hair between fixed physics ticks.
        // Identical poses retain the previous solve exactly, including repeated
        // captured timestamps; render-only projections do not add velocity.
        const bool resolve_collisions=result.steps||baseline!=collision_baseline;
        // Even with zero new steps, render the stored tails against this pose.
        // Reuse an identical completed solve exactly; another normalization at
        // a bend boundary can otherwise change floating-point rotations.
        const bool reuse_render=!result.steps&&cached_render&&baseline==render_baseline&&
            (!inertial||add(inertial_pending_carry,duplicate_carry)==render_inertial_carry);
        std::vector<M> input_baseline;if(inertial)input_baseline=baseline;
        const unsigned iterations=inertial?result.steps+1:std::max(1u,result.steps);
        for(unsigned iteration=0;iteration<(reuse_render?0u:iterations);++iteration) {
            const bool advance=result.steps&&(!inertial||iteration<result.steps);
            if(inertial) {
                baseline=input_baseline;V remaining_carry{};
                if(advance&&sample_dt>0) {
                    // Sample actor translation at each fixed tick, then render
                    // stored tails against the latest pose without another tick.
                    // Keep current bone bases/scales; never blend rotation matrices.
                    const double remaining=(result.steps-1-iteration)*step+accumulator;
                    const double fraction=std::clamp(remaining/sample_dt,0.0,1.0);
                    const auto offset=scale(actor_travel,fraction);
                    for(auto& m:baseline)for(int c=0;c<3;++c)m[12+c]-=offset[c];
                    remaining_carry=scale(inertial_sample_carry,fraction);
                }
                if(advance) {
                    // Follow part of horizontal running travel with BOTH history
                    // points. Consume only carry through this fixed tick and
                    // retain the remaining displacement for subsequent ticks.
                    const auto carry=sub(inertial_pending_carry,remaining_carry);
                    cohesion_gain=.12*std::clamp(norm(carry)/(step*2),0.0,1.0);
                    for(std::size_t i=0;i<state.size();++i)if(joints[i].hair){state[i].current=add(state[i].current,carry);state[i].previous=add(state[i].previous,carry);}
                    inertial_pending_carry=remaining_carry;
                }
            }
            world=baseline;std::fill(changed.begin(),changed.end(),false);
            for(std::size_t i=0;i<joints.size();++i) {
                const auto& j=joints[i];const auto& settings=j.settings;auto& s=state[i];const auto ctr=center(j),inv_center=inverse(ctr);
                const auto root=position(world[settings.node]);const auto axis=sub(tail(j,world),root);const auto axis_unit=unit(axis,{0,1,0});const auto length=norm(axis);
                require(std::isfinite(length)&&length>1e-10,"Degenerate spring length");
                const bool preserve_shape=j.hair&&motion==SpringMotion::Relaxed;
                const auto shape_axis=preserve_shape?unit(sub(tail(j,baseline),position(baseline[settings.node])),axis_unit):axis_unit;
                V next=point(ctr,s.current);
                if(inertial&&!advance&&j.hair) {
                    // Render the remaining fraction of travel without advancing
                    // the stored tails or consuming their pending carry.
                    next=add(next,add(inertial_pending_carry,duplicate_carry));
                }
                if(advance) {
                    const auto inertia=scale(sub(s.current,s.previous),1-settings.drag);
                    next=point(ctr,add(s.current,inertia));next=add(next,scale(shape_axis,step*settings.stiffness));
                    next=add(next,scale({settings.gravity[0],settings.gravity[1],settings.gravity[2]},step*settings.gravity_power));
                    if(j.airflow_weight>0)next=add(next,scale(air_velocity,-step*airflow_gain*j.airflow_weight));
                }
                next=add(root,scale(unit(sub(next,root),axis_unit),length));
                const auto collision_input=next;
                if(preserve_shape&&resolve_collisions)next=limit_bend(root,next,shape_axis,baseline[settings.node]);
                if(resolve_collisions&&!settings.colliders.empty())next=contact_tail(j,root,next,collision_input,shape_axis);
                require(finite(next),"Nonfinite spring tail");
                if(advance){
                    // A positional bend/contact correction is not extra velocity.
                    // Shift both history points by that correction, retaining
                    // ordinary motion while preventing a collision-driven kick.
                    s.previous=add(s.current,direction(inv_center,sub(next,collision_input)));
                    s.current=point(inv_center,next);
                }
                const auto solved_axis=unit(sub(next,root),axis_unit);auto rotation=swing(axis_unit,solved_axis,baseline[settings.node]);
                if(inertial&&!advance&&cohesion_gain>0&&section_changed[settings.node]) {
                    // Transport the last physical roll onto this rendered tail.
                    // Its axis stays fixed, so length and contacts stay valid.
                    // Merge it into the normal rotation: no second neighbour or
                    // collision pass, and no render-only history writes.
                    const auto posed_axis=unit(sub(tail(j,baseline),position(baseline[settings.node])),axis_unit);
                    const double raw=twist_angle(posed_axis,solved_axis,baseline[settings.node],world[settings.node],rotation);
                    const double angle=std::remainder(section_twists[i]-raw,2*3.14159265358979323846),s=std::sin(angle*.5);
                    rotation=compose({solved_axis[0]*s,solved_axis[1]*s,solved_axis[2]*s,std::cos(angle*.5)},rotation);
                }
                for(auto n:j.descendants){rotate_about(world[n],rotation,root);changed[n]=true;}
                result.max_offset=std::max(result.max_offset,norm(sub(next,tail(j,baseline))));
            }
            if(inertial&&advance)cohere_sections();
        }
        if(result.steps)collision_baseline=baseline;
        // Commit the complete pose and palettes together. Any failure above or
        // below leaves the caller's ordinary retargeted pose untouched.
        auto revised=pose;
        for(std::size_t n=0;n<world.size();++n)if(changed[n])revised.node_world[n]=output(multiply(actor_inverse,world[n]));
        std::size_t palette_work=0;
        for(auto& palette:revised.palettes) {
            require(palette.mesh_node>=0&&static_cast<std::size_t>(palette.mesh_node)<world.size()&&palette.skin>=0&&static_cast<std::size_t>(palette.skin)<avatar.skins.size(),"Invalid spring skin palette");
            const auto& skin=avatar.skins[palette.skin];require(skin.joints.size()==skin.inverse_bind.size()&&skin.joints.size()==palette.matrices.size(),"Invalid spring palette size");
            palette_work+=skin.joints.size();require(palette_work<=8192,"Spring palette work limit");const auto mesh_inverse=inverse(matrix(revised.node_world[palette.mesh_node]));
            for(std::size_t i=0;i<skin.joints.size();++i) {
                const int joint=skin.joints[i];require(joint>=0&&static_cast<std::size_t>(joint)<world.size(),"Invalid spring skin joint");
                palette.matrices[i]=output(multiply(multiply(mesh_inverse,matrix(revised.node_world[joint])),matrix(skin.inverse_bind[i])));
            }
        }
        pose=std::move(revised);render_baseline=baseline;cached_render=true;
        if(inertial)render_inertial_carry=add(inertial_pending_carry,duplicate_carry);
        if(inertial&&sample_dt>0)previous_actor_position=actor_position;
        result.applied=true;return result;
    }
};
SpringBones::SpringBones(const Avatar& avatar,SpringMotion motion):impl_(std::make_unique<Impl>(avatar,motion)) {
    try{impl_->compile();}catch(const Invalid& e){impl_->error=e.why;impl_->joints.clear();}catch(const std::exception& e){impl_->error=e.what();impl_->joints.clear();}
}
SpringBones::~SpringBones()=default;
std::unique_ptr<SpringBones> make_optional_springs(const Avatar& avatar,SpringMotion motion)noexcept {
    try{return std::make_unique<SpringBones>(avatar,motion);}catch(...){return {};}
}
bool SpringBones::ready()const noexcept{return impl_->error.empty();}
const std::string& SpringBones::error()const noexcept{return impl_->error;}
std::size_t SpringBones::joint_count()const noexcept{return impl_->joints.size();}
SpringUpdate SpringBones::update(RetargetResult& pose,const SpringFrame& frame)noexcept {
    if(!ready())return {false,false,0,0,"Spring topology disabled"};
    try{return impl_->update(pose,frame);}catch(const Invalid& e){impl_->initialized=false;return {false,true,0,0,e.why};}catch(...){impl_->initialized=false;return {false,true,0,0,"Spring simulation failed"};}
}
} // namespace vrm
