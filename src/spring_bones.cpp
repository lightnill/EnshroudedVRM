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
Q swing(const V& from,const V& to) {
    const double cosine=std::clamp(dot(from,to),-1.0,1.0);
    if(cosine<-.99999999) {
        const V seed=std::abs(from[0])<.8?V{1,0,0}:V{0,1,0};const auto axis=unit(cross(from,seed),{0,0,1});return {axis[0],axis[1],axis[2],0};
    }
    const auto axis=cross(from,to);const double w=1+cosine,n=std::sqrt(dot(axis,axis)+w*w);
    require(n>1e-12,"Invalid spring swing");return {axis[0]/n,axis[1]/n,axis[2]/n,w/n};
}
V rotate(const Q& q,const V& p){const V v{q[0],q[1],q[2]};const auto t=scale(cross(v,p),2);return add(p,add(scale(t,q[3]),cross(v,t)));}
void rotate_about(M& m,const Q& rotation,const V& pivot) {
    for(int c=0;c<3;++c){const auto v=rotate(rotation,{m[c*4],m[c*4+1],m[c*4+2]});for(int r=0;r<3;++r)m[c*4+r]=v[r];}
    const auto p=add(pivot,rotate(rotation,sub(position(m),pivot)));for(int c=0;c<3;++c)m[12+c]=p[c];
}
bool finite(const V& v){return std::all_of(v.begin(),v.end(),[](double n){return std::isfinite(n);});}
} // namespace

struct SpringBones::Impl {
    struct Joint {SpringJoint settings;V local_tail{};std::vector<std::size_t> descendants;};
    struct State {V current{},previous{};};
    const Avatar& avatar;
    const SpringMotion motion;
    std::vector<Joint> joints;
    std::vector<State> state;
    std::vector<M> baseline,world;
    std::vector<bool> changed;
    std::string error;
    bool initialized=false;
    std::uint64_t time=0,epoch=0,actor=0;
    V previous_anchor{};
    double accumulator=0;
    explicit Impl(const Avatar& a,SpringMotion preference):avatar(a),motion(preference) {}
    bool ancestor(int a,int b)const {
        for(std::size_t i=0;b>=0&&i<=avatar.nodes.size();++i){if(a==b)return true;b=avatar.nodes[b].parent;}return false;
    }
    void compile() {
        require(motion==SpringMotion::Authored||motion==SpringMotion::Relaxed,"Invalid spring motion preference");
        require(avatar.nodes.size()<=2048&&avatar.spring_joints.size()<=256&&avatar.spring_colliders.size()<=256,"Spring topology limit");
        const auto count=avatar.nodes.size();baseline.resize(count);world.resize(count);changed.resize(count);
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
            if(motion==SpringMotion::Relaxed) {
                // Resolve attachment once from the validated humanoid hierarchy,
                // so unfamiliar bone names do not restore excessive body sway.
                const auto head_bone=avatar.humanoid.find("head");
                const bool hair=head_bone!=avatar.humanoid.end()&&ancestor(head_bone->second,source.node);
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
        for(const auto& c:avatar.spring_colliders) {
            require(valid_node(c.node)&&std::isfinite(c.radius)&&c.radius>=0&&c.radius<=100,"Invalid spring collider");
            for(int k=0;k<3;++k)require(std::isfinite(c.offset[k])&&std::isfinite(c.tail[k]),"Invalid spring collider vector");
            for(const auto& j:joints)require(!ancestor(j.settings.node,c.node),"Spring collider depends on simulated subtree");
        }
        for(const auto& j:joints)if(j.settings.center>=0)
            for(const auto& owner:joints)if(ancestor(owner.settings.node,j.settings.center))
                require(owner.settings.node==j.settings.center&&owner.settings.center==j.settings.center,"Spring center depends on another simulation chain");
        std::stable_sort(joints.begin(),joints.end(),[&](const Joint& a,const Joint& b){
            auto depth=[&](int n){unsigned d=0;for(;n>=0;n=avatar.nodes[n].parent)++d;return d;};return depth(a.settings.node)<depth(b.settings.node);
        });
        state.resize(joints.size());
    }
    M center(const Joint& j)const {return j.settings.center<0?identity:baseline[j.settings.center];}
    V tail(const Joint& j,const std::vector<M>& transforms)const {
        return j.settings.child>=0?position(transforms[j.settings.child]):point(transforms[j.settings.node],j.local_tail);
    }
    void reset(const SpringFrame& frame,const V& anchor) {
        for(std::size_t i=0;i<joints.size();++i) {
            const auto& j=joints[i];
            state[i].current=state[i].previous=point(inverse(center(j)),tail(j,baseline));
        }
        time=frame.timestamp_ms;epoch=frame.source_epoch;actor=frame.actor_id;previous_anchor=anchor;accumulator=0;initialized=true;
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
        for(std::size_t n=0;n<baseline.size();++n){const auto m=matrix(pose.node_world[n]);inverse(m);baseline[n]=multiply(actor_matrix,m);}
        const auto hips=avatar.humanoid.find("hips");const auto anchor=hips==avatar.humanoid.end()?position(actor_matrix):position(baseline[hips->second]);
        if(initialized) {
            previous_anchor=sub(previous_anchor,frame.origin_delta);
            for(std::size_t i=0;i<state.size();++i)if(joints[i].settings.center<0){state[i].current=sub(state[i].current,frame.origin_delta);state[i].previous=sub(state[i].previous,frame.origin_delta);}
        }
        if(!initialized||frame.source_epoch!=epoch||frame.actor_id!=actor||frame.timestamp_ms<time||frame.timestamp_ms-time>250||norm(sub(anchor,previous_anchor))>3) {
            reset(frame,anchor);result.reset=true;return result;
        }
        accumulator+=static_cast<double>(frame.timestamp_ms-time)*.001;time=frame.timestamp_ms;previous_anchor=anchor;
        constexpr double step=1.0/60.0;
        result.steps=std::min(4u,static_cast<unsigned>(std::floor((accumulator+1e-10)/step)));
        if(result.steps==4)accumulator=std::fmod(accumulator,step);else accumulator-=result.steps*step;
        // Even with zero new steps, render the stored tails against this pose.
        for(unsigned iteration=0;iteration<std::max(1u,result.steps);++iteration) {
            world=baseline;std::fill(changed.begin(),changed.end(),false);
            for(std::size_t i=0;i<joints.size();++i) {
                const auto& j=joints[i];const auto& settings=j.settings;auto& s=state[i];const auto ctr=center(j),inv_center=inverse(ctr);
                const auto root=position(world[settings.node]);const auto axis=sub(tail(j,world),root);const auto axis_unit=unit(axis,{0,1,0});const auto length=norm(axis);
                require(std::isfinite(length)&&length>1e-10,"Degenerate spring length");
                V next=point(ctr,s.current);
                if(result.steps) {
                    const auto inertia=scale(sub(s.current,s.previous),1-settings.drag);
                    next=point(ctr,add(s.current,inertia));next=add(next,scale(axis_unit,step*settings.stiffness));
                    next=add(next,scale({settings.gravity[0],settings.gravity[1],settings.gravity[2]},step*settings.gravity_power));
                }
                next=add(root,scale(unit(sub(next,root),axis_unit),length));
                for(unsigned pass=0;pass<(result.steps?2u:0u);++pass)for(auto index:settings.colliders) {
                    const auto& c=avatar.spring_colliders[index];const auto& cm=baseline[c.node];const auto start=point(cm,{c.offset[0],c.offset[1],c.offset[2]});V closest=start;
                    if(c.capsule){const auto end=point(cm,{c.tail[0],c.tail[1],c.tail[2]});const auto segment=sub(end,start);const double d=dot(segment,segment);if(d>1e-16)closest=add(start,scale(segment,std::clamp(dot(sub(next,start),segment)/d,0.0,1.0)));}
                    const auto away=sub(next,closest);const auto distance=norm(away);const double radius=c.radius*max_scale(cm)+settings.hit_radius*max_scale(world[settings.node]);
                    if(distance<radius){next=add(closest,scale(unit(away,axis_unit),radius));next=add(root,scale(unit(sub(next,root),axis_unit),length));}
                }
                require(finite(next),"Nonfinite spring tail");
                if(result.steps){s.previous=s.current;s.current=point(inv_center,next);}
                const auto rotation=swing(axis_unit,unit(sub(next,root),axis_unit));
                for(auto n:j.descendants){rotate_about(world[n],rotation,root);changed[n]=true;}
                result.max_offset=std::max(result.max_offset,norm(sub(next,tail(j,baseline))));
            }
        }
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
        pose=std::move(revised);result.applied=true;return result;
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
