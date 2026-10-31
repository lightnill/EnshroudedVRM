#include "../src/spring_bones.hpp"
#include <algorithm>
#include <cmath>
#include <iostream>
#include <stdexcept>

using M=vrm::Matrix;
using V=std::array<double,3>;
constexpr M identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
constexpr auto inertial=vrm::SpringMotion::Inertial;
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
M at(float x,float y=0,float z=0){auto m=identity;m[12]=x;m[13]=y;m[14]=z;return m;}
V point(const M& m,V v){V p{m[12],m[13],m[14]};for(int c=0;c<3;++c)for(int k=0;k<3;++k)p[c]+=m[k*4+c]*v[k];return p;}
double distance(V a,V b){double d=0;for(int k=0;k<3;++k)d+=(a[k]-b[k])*(a[k]-b[k]);return std::sqrt(d);}
V position(const M& m){return {m[12],m[13],m[14]};}
vrm::Avatar strips(bool spread=true,float separation=.06f){
    vrm::Avatar a;a.version=vrm::VrmVersion::V0;a.nodes.resize(7);a.humanoid={{"hips",0},{"head",1}};
    for(auto& n:a.nodes){n.active=true;n.local=n.world=identity;}
    a.nodes[0].children={1,6};a.nodes[1].parent=0;a.nodes[1].children={2,4};a.nodes[1].local=a.nodes[1].world=at(0,1);
    for(int side=0;side<2;++side){const int root=2+side*2;const float x=(side?1:-1)*separation*.5f;
        a.nodes[root].parent=1;a.nodes[root].children={root+1};a.nodes[root].local=at(x);a.nodes[root].world=at(x,1);
        a.nodes[root+1].parent=root;a.nodes[root+1].local=at(0,-.3f);a.nodes[root+1].world=at(x,.7f);
        vrm::SpringJoint j;j.node=root;j.child=root+1;j.center=0;j.drag=.4f;j.stiffness=.4f;j.gravity_power=spread?.35f:0;j.gravity={side?1.f:-1.f,0,0};a.spring_joints.push_back(j);
    }
    a.nodes[6].parent=0;a.nodes[6].mesh=0;a.nodes[6].skin=0;a.meshes.resize(1);
    vrm::Skin skin;skin.joints={2,4};skin.inverse_bind={at(separation*.5f,-1),at(-separation*.5f,-1)};a.skins={skin};return a;
}
vrm::RetargetResult pose(const vrm::Avatar& a){vrm::RetargetResult p;for(const auto& n:a.nodes)p.node_world.push_back(n.world);vrm::SkinPalette s;s.mesh_node=6;s.skin=0;s.matrices.assign(2,identity);p.palettes.push_back(s);return p;}
vrm::SpringFrame frame(unsigned ms,double travel=0,double origin=0,double yaw=0,double actor_scale=1){
    vrm::SpringFrame f;f.timestamp_ms=ms;f.actor_id=1;f.source_epoch=1;f.model_to_world=identity;
    f.model_to_world[0]=f.model_to_world[10]=float(std::cos(yaw)*actor_scale);f.model_to_world[2]=float(-std::sin(yaw)*actor_scale);f.model_to_world[8]=float(std::sin(yaw)*actor_scale);f.model_to_world[5]=float(actor_scale);
    f.model_to_world[12]=float(std::sin(yaw)*travel-origin);f.model_to_world[14]=float(std::cos(yaw)*travel);return f;
}
void valid_lengths(const vrm::Avatar& a,const vrm::RetargetResult& p){
    for(const auto& m:p.node_world)for(float value:m)check(std::isfinite(value),"Cohesion output is nonfinite");
    for(const auto& j:a.spring_joints)if(j.child>=0)check(std::abs(distance(position(p.node_world[j.node]),position(p.node_world[j.child]))-distance(position(a.nodes[j.node].world),position(a.nodes[j.child].world)))<3e-6,"Cohesion stretched a bone");
}
vrm::RetargetResult run(const vrm::Avatar& a,int fps=60,vrm::SpringMotion mode=inertial,double yaw=0,bool rebased=false,bool stopping=false,double actor_scale=1,double speed=5){
    vrm::SpringBones solver(a,mode);check(solver.ready(),"Cohesion fixture disabled");auto p=pose(a);double previous_origin=0;
    for(int i=0;i<=fps*(stopping?7:4);++i){const unsigned ms=unsigned(i)*1000/unsigned(fps);const double t=ms*.001,origin=rebased?std::floor(t*4)*.5:0;
        auto f=frame(1000+ms,speed*(stopping?std::min(t,4.):t),origin,yaw,actor_scale);f.origin_delta={origin-previous_origin,0,0};previous_origin=origin;p=pose(a);const auto r=solver.update(p,f);check(!r.error,"Cohesion update failed");valid_lengths(a,p);
        if(r.applied){auto duplicate=pose(a);f.origin_delta={};const auto held=solver.update(duplicate,f);check(!held.steps&&duplicate.node_world==p.node_world,"Cohesion advances duplicate samples");}
    }return p;
}
vrm::RetargetResult independent(const vrm::Avatar& a,int side,int fps=60,vrm::SpringMotion mode=inertial){auto single=a;single.spring_joints.erase(single.spring_joints.begin()+(side?0:1));return run(single,fps,mode);}
double visible_gap(const vrm::RetargetResult& p,float separation=.06f){const auto left=point(p.palettes[0].matrices[0],{-separation*.5+.02,.7,0}),right=point(p.palettes[0].matrices[1],{separation*.5-.02,.7,0});return right[0]-left[0];}
void closer_sections(){
    const auto a=strips();const auto joined=run(a),left=independent(a,0),right=independent(a,1);auto separate=left;separate.palettes[0].matrices[1]=right.palettes[0].matrices[1];const double gap=visible_gap(joined),unlinked=visible_gap(separate);
    check(unlinked>.035,"Synthetic strips do not expose an excess visible gap");
    check(gap>=.019&&gap-.02<(unlinked-.02)*.75,"Neighbouring skinned hair sections still separate as independent strips");
    const double mean_trail=(joined.node_world[3][14]+joined.node_world[5][14])*.5,reference=(left.node_world[3][14]+right.node_world[5][14])*.5;
    check(std::abs(mean_trail-reference)<.025,"Section cohesion removes the common running sweep");
}
void common_motion(){const auto a=strips(false);const auto joined=run(a);for(int side=0;side<2;++side){const auto control=independent(a,side);for(int n:{2+side*2,3+side*2})for(int k=0;k<16;++k)check(std::abs(joined.node_world[n][k]-control.node_world[n][k])<3e-6,"Cohesion changes already-coherent common motion");}}
void rest_spacing(){const auto a=strips(false);vrm::SpringBones solver(a,inertial);auto p=pose(a);for(unsigned i=0;i<180;++i){p=pose(a);solver.update(p,frame(1000+i*17));}check(std::abs(visible_gap(p)-.02)<1e-6,"Cohesion shrinks the authored section spacing");}
void profile_isolation(){const auto a=strips();for(auto mode:{vrm::SpringMotion::Authored,vrm::SpringMotion::Relaxed,vrm::SpringMotion::Airflow}){const auto joined=run(a,60,mode);for(int side=0;side<2;++side){const auto control=independent(a,side,60,mode);for(int n:{2+side*2,3+side*2})for(int k=0;k<16;++k)check(std::abs(joined.node_world[n][k]-control.node_world[n][k])<1e-6,"Cohesion changes an older spring profile");}}}
void distant_sections(){const auto a=strips(true,1);const auto joined=run(a);for(int side=0;side<2;++side){const auto control=independent(a,side);for(int n:{2+side*2,3+side*2})for(int k=0;k<16;++k)check(std::abs(joined.node_world[n][k]-control.node_world[n][k])<1e-6,"Cohesion links unrelated distant sections");}}
void body_isolation(){auto a=strips();a.nodes[1].children={2};a.nodes[0].children.push_back(4);a.nodes[4].parent=0;const auto joined=run(a);for(int side=0;side<2;++side){const auto control=independent(a,side);for(int n:{2+side*2,3+side*2})for(int k=0;k<16;++k)check(std::abs(joined.node_world[n][k]-control.node_world[n][k])<1e-6,"Cohesion pulls a body spring or uses it as a hair neighbour");}}
void frame_rates(){const auto a=strips();const auto reference=run(a);for(int fps:{25,120}){const auto p=run(a,fps);check(std::abs(visible_gap(p)-visible_gap(reference))<.001,"Section spacing varies excessively with gameplay FPS");for(int n:{3,5})check(distance(position(p.node_world[n]),position(reference.node_world[n]))<.001,"Cohesion motion varies excessively with gameplay FPS");}}
void headings_and_origins(){auto a=strips(false);a.spring_joints[1].drag=.65f;const auto reference=run(a);for(double yaw:{.31,.73,1.57,3.14159265,-2.7}){const auto p=run(a,60,inertial,yaw);for(int n:{2,3,4,5})for(int k=0;k<16;++k)check(std::abs(p.node_world[n][k]-reference.node_world[n][k])<2e-5,"Cohesion depends on actor heading");}const auto p=run(a,60,inertial,0,true);for(int n:{2,3,4,5})for(int k=0;k<16;++k)check(std::abs(p.node_world[n][k]-reference.node_world[n][k])<2e-5,"Origin rebasing changes section cohesion");}
void render_history(){const auto a=strips();vrm::SpringBones extra(a,inertial),direct(a,inertial);auto p=pose(a),q=p;for(unsigned i=0;i<=120;++i){p=pose(a);q=pose(a);extra.update(p,frame(1000+i*17,i*.085));direct.update(q,frame(1000+i*17,i*.085));}p=pose(a);extra.update(p,frame(3048,10.24));p=pose(a);q=pose(a);extra.update(p,frame(3057,10.285));direct.update(q,frame(3057,10.285));for(int n:{2,3,4,5})for(int k=0;k<16;++k)check(std::abs(p.node_world[n][k]-q.node_world[n][k])<1e-6,"Render-only cohesion changes future physics history");}
void stop_and_reset(){const auto a=strips();const auto p=run(a,60,inertial,0,false,true);for(int n:{3,5})check(std::abs(p.node_world[n][14])<.001,"Cohesion keeps a running force after stopping");vrm::SpringBones solver(a,inertial),fresh(a,inertial);auto q=pose(a);solver.update(q,frame(1000));q=pose(a);solver.update(q,frame(1017,.085));auto f=frame(1034,.17);++f.actor_id;q=pose(a);check(solver.update(q,f).reset,"Cohesion fails to reset on player switch");auto control=pose(a);fresh.update(control,f);q=pose(a);control=pose(a);f.timestamp_ms+=17;solver.update(q,f);fresh.update(control,f);check(q.node_world==control.node_world,"Cohesion retains stale running history after reset");}
void virtual_and_hierarchy(){auto a=strips();for(int n:{3,5}){vrm::SpringJoint j=a.spring_joints[n==3?0:1];j.node=n;j.child=-1;a.spring_joints.push_back(j);}valid_lengths(a,run(a));a.nodes.resize(9);for(int side=0;side<2;++side){const int n=7+side,owner=3+side*2;a.nodes[n].active=true;a.nodes[n].parent=owner;a.nodes[owner].children={n};a.nodes[n].local=at(0,-.3f,.04f);a.nodes[n].world=at(side?.03f:-.03f,.4f,.04f);a.spring_joints[2+side].child=n;}valid_lengths(a,run(a));}
double segment_gap(const M& a,const M& b,V c){const auto root=position(a),tip=position(b);V axis{};double n=0,t=0;for(int k=0;k<3;++k){axis[k]=tip[k]-root[k];n+=axis[k]*axis[k];t+=(c[k]-root[k])*axis[k];}t=std::clamp(t/n,0.,1.);V q=root;for(int k=0;k<3;++k)q[k]+=t*axis[k];return distance(q,c);}
void collision_priority(){auto a=strips();vrm::SpringCollider c;c.node=0;c.offset={0,.85f,-.1f};c.radius=.09f;a.spring_colliders={c};for(auto& j:a.spring_joints)j.colliders={0};const auto p=run(a);for(const auto& j:a.spring_joints)check(segment_gap(p.node_world[j.node],p.node_world[j.child],{c.offset[0],c.offset[1],c.offset[2]})>=c.radius-1e-6,"Cohesion pulls a section through torso protection");}
void scales_and_metadata(){const auto a=strips();for(double scale:{.5,2.})valid_lengths(a,run(a,60,inertial,0,false,false,scale));check(a.spring_joints[0].center==0&&a.spring_joints[0].stiffness==.4f&&a.spring_joints[0].drag==.4f,"Cohesion changes avatar metadata");}
void walking_and_head_control(){
    const auto a=strips();const auto joined=run(a,60,inertial,0,false,false,1,1.5);
    for(int side=0;side<2;++side){auto single=a;single.spring_joints.erase(single.spring_joints.begin()+(side?0:1));const auto control=run(single,60,inertial,0,false,false,1,1.5);for(int n:{2+side*2,3+side*2})for(int k=0;k<16;++k)check(std::abs(joined.node_world[n][k]-control.node_world[n][k])<1e-6,"Cohesion changes walking sections");
        vrm::SpringBones trial(a,inertial),separate(single,inertial);for(unsigned i=0;i<120;++i){auto p=pose(a),q=pose(single);for(int n=1;n<=5;++n){p.node_world[n][12]+=float(.04*std::sin(i*.1));p.node_world[n][14]+=float(.03*std::cos(i*.1));q.node_world[n]=p.node_world[n];}const auto f=frame(1000+i*17);trial.update(p,f);separate.update(q,f);for(int n:{2+side*2,3+side*2})for(int k=0;k<16;++k)check(std::abs(p.node_world[n][k]-q.node_world[n][k])<1e-6,"Cohesion links sections during stationary head motion");}
    }
}
void neighbour_controls(){for(bool opposite:{false,true}){auto a=strips();a.nodes[5].local=at(0,opposite?.3f:-.06f);a.nodes[5].world=at(.03f,opposite?1.3f:.94f);const auto p=run(a);for(int side=0;side<2;++side){const auto control=independent(a,side);for(int n:{2+side*2,3+side*2})for(int k=0;k<16;++k)check(std::abs(p.node_world[n][k]-control.node_world[n][k])<1e-6,"Cohesion links incompatible section lengths or opposite directions");}}}
void turns_and_order(){auto a=strips();const auto reference=run(a);std::reverse(a.spring_joints.begin(),a.spring_joints.end());const auto reordered=run(a);check(reference.node_world==reordered.node_world,"Section cohesion depends on source joint order");
    for(int axis:{0,2}){vrm::SpringBones solver(a,inertial);for(unsigned i=0;i<180;++i){auto p=pose(a);if(i>=90)for(int n=1;n<=5;++n){for(int col=0;col<3;++col){p.node_world[n][col*4+1]*=-1;p.node_world[n][col*4+(axis==0?2:0)]*=-1;}p.node_world[n][13]=2-p.node_world[n][13];p.node_world[n][axis==0?14:12]*=-1;}const auto r=solver.update(p,frame(1000+i*17,i*.085));check(!r.error,"Near-antiparallel section rotations fail");valid_lengths(a,p);}}
}
int main(){unsigned failed=0;for(int test=0;test<16;++test)try{
    if(test==0)closer_sections();else if(test==1)common_motion();else if(test==2)rest_spacing();else if(test==3)profile_isolation();else if(test==4)distant_sections();else if(test==5)body_isolation();else if(test==6)frame_rates();else if(test==7)headings_and_origins();else if(test==8)render_history();else if(test==9)stop_and_reset();else if(test==10)virtual_and_hierarchy();else if(test==11)collision_priority();else if(test==12)scales_and_metadata();else if(test==13)walking_and_head_control();else if(test==14)neighbour_controls();else turns_and_order();
}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<e.what()<<"\n";}
if(!failed)std::cout<<"PASS 16 cohesion scenarios: skinned gap/common sweep, rest spacing, profiles/body/isolated sections, FPS, headings/origin, render history/reset, virtual hierarchy, contacts, scale/metadata, walking/head controls and antiparallel/order cases\n";
return failed?1:0;}
