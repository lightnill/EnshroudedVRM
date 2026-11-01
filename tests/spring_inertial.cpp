#include "../src/spring_bones.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using M=vrm::Matrix;
const M I{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
constexpr auto inertia=vrm::SpringMotion::Inertial;
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
M at(float x,float y=0,float z=0){auto m=I;m[12]=x;m[13]=y;m[14]=z;return m;}
vrm::Avatar fixture(bool hair=true){
 vrm::Avatar a;a.version=vrm::VrmVersion::V0;a.nodes.resize(5);a.humanoid={{"hips",0},{"head",4}};
 for(auto& n:a.nodes)n.local=n.world=I,n.active=true;
 a.nodes[0].children={4,3};a.nodes[4].parent=0;a.nodes[4].local=a.nodes[4].world=at(0,1);a.nodes[4].children={1};a.nodes[1].parent=4;a.nodes[1].children={2};a.nodes[1].world=at(0,1);a.nodes[2].parent=1;a.nodes[2].local=at(0,-.3f);a.nodes[2].world=at(0,.7f);a.nodes[3].parent=0;
 if(!hair){a.nodes[4].children.clear();a.nodes[0].children.push_back(1);a.nodes[1].parent=0;}
 vrm::SpringJoint j;j.node=1;j.child=2;j.center=0;j.drag=.4f;j.stiffness=.4f;a.spring_joints={j};return a;
}
vrm::RetargetResult pose(const vrm::Avatar& a){vrm::RetargetResult p;for(const auto& n:a.nodes)p.node_world.push_back(n.world);return p;}
vrm::SpringFrame frame(unsigned time,double x=0,double z=0){vrm::SpringFrame f;f.timestamp_ms=time;f.actor_id=1;f.source_epoch=1;f.model_to_world=at(float(x),0,float(z));return f;}
double distance(const M& a,const M& b){double d=0;for(int k=0;k<3;++k)d+=std::pow(double(a[12+k])-b[12+k],2);return std::sqrt(d);}
vrm::RetargetResult run(vrm::Avatar a,double speed,int fps=60,vrm::SpringMotion mode=inertia,bool rebased=false,double yaw=0,bool stopping=false){
 vrm::SpringBones solver(a,mode);check(solver.ready(),"Inertial profile disabled");auto p=pose(a);double previous_origin=0;
 for(int i=0;i<=fps*(stopping?7:4);++i){const unsigned ms=unsigned(i)*1000/unsigned(fps);const double t=ms*.001,origin=rebased?std::floor(t*5)*.5:0;
  auto f=frame(1000+ms,speed*(stopping?std::min(t,4.):t)-origin);f.origin_delta={origin-previous_origin,0,0};previous_origin=origin;
  f.model_to_world[0]=f.model_to_world[10]=float(std::cos(yaw));f.model_to_world[2]=float(-std::sin(yaw));f.model_to_world[8]=float(std::sin(yaw));
  p=pose(a);const auto r=solver.update(p,f);check(!r.error,"Inertial solve failed");check(std::abs(distance(p.node_world[1],p.node_world[2])-.3)<3e-6,"Inertia stretches hair");
  if(r.applied){auto duplicate=pose(a);f.origin_delta={};const auto repeated=solver.update(duplicate,f);check(!repeated.steps&&duplicate.node_world==p.node_world,"Repeated movement sample advances inertia");}
 }
 return p;
}
void world_motion(){const auto a=fixture();check(run(a,5).node_world[2][12]<-.03f,"Centered hair does not respond to travel through world-space inertia");check(a.spring_joints[0].center==0,"Inertial preference changes authored center metadata");}
void no_force_reference(){
 auto a=fixture(),reference=a;reference.spring_joints[0].drag=.7f;reference.spring_joints[0].stiffness=2;
 vrm::SpringBones control(reference,vrm::SpringMotion::Authored),trial(a,inertia);auto p=pose(reference),q=pose(a);control.update(p,frame(1000));trial.update(q,frame(1000));
 // Independent existing spring solver with a 57%-following center at 5 m/s,
 // first at the fixed tick and then at render time.
 constexpr double tick_position=5./60,render_position=.085;p=pose(reference);p.node_world[0][12]=float(-.43*tick_position);control.update(p,frame(1017,tick_position));
 p=pose(reference);p.node_world[0][12]=float(-.43*render_position);control.update(p,frame(1017,render_position));q=pose(a);trial.update(q,frame(1017,render_position));
 check(distance(p.node_world[2],q.node_world[2])<2e-6,"Inertial profile adds a force beyond a partially following spring center");
}
void idle_and_body(){check(std::abs(run(fixture(),0).node_world[2][12])<1e-6,"Idle inertia creates artificial wind");check(std::abs(run(fixture(false),5).node_world[2][12])<1e-6,"World-space hair preference changes a body spring's authored center");}
void stop_settles(){check(std::abs(run(fixture(),5,60,inertia,false,0,true).node_world[2][12])<.001,"Inertial hair does not settle after stopping");}
void origins(){const auto a=fixture();check(distance(run(a,5).node_world[2],run(a,5,60,inertia,true).node_world[2])<2e-5,"Centered-source world tails fail to follow camera-origin shifts");check(std::abs(run(a,0,60,inertia,true).node_world[2][12])<1e-6,"Camera movement drives stationary inertial hair");}
void headings(){const auto a=fixture();const auto reference=run(a,5);for(double yaw:{.31,.73,1.57}){const auto p=run(a,5,60,inertia,false,yaw);const double x=p.node_world[2][12],z=p.node_world[2][14];check(std::abs(std::cos(yaw)*x+std::sin(yaw)*z-reference.node_world[2][12])<2e-5&&std::abs(-std::sin(yaw)*x+std::cos(yaw)*z)<2e-5,"World-space inertia changes with actor facing");}check(run(a,-5).node_world[2][12]>.03f,"Reverse travel does not reverse inertial lag");}
void frame_rates(){const auto a=fixture();const auto reference=run(a,5);for(int fps:{25,120}){const auto p=run(a,5,fps);if(distance(p.node_world[2],reference.node_world[2])>=.001)std::cerr<<"fps="<<fps<<" tail="<<p.node_world[2][12]<<","<<p.node_world[2][13]<<" reference="<<reference.node_world[2][12]<<","<<reference.node_world[2][13]<<"\n";check(distance(p.node_world[2],reference.node_world[2])<.001,"Inertial running response varies too much across gameplay FPS");}}
void render_only_samples(){const auto a=fixture();vrm::SpringBones with_render(a,inertia),direct(a,inertia);auto p=pose(a),q=p;with_render.update(p,frame(1000));direct.update(q,frame(1000));p=pose(a);const auto r=with_render.update(p,frame(1008,.04));check(!r.steps&&!r.error,"Intermediate render advances inertial physics");p=pose(a);q=pose(a);with_render.update(p,frame(1017,.085));direct.update(q,frame(1017,.085));check(distance(p.node_world[2],q.node_world[2])<1e-6,"Render-only sample changes subsequent inertial state");}
void same_timestamp_render(){const auto a=fixture();vrm::SpringBones with_render(a,inertia),direct(a,inertia);auto p=pose(a),q=p;with_render.update(p,frame(1000));direct.update(q,frame(1000));p=pose(a);const auto r=with_render.update(p,frame(1000,.04));check(!r.steps&&!r.error,"Changed same-timestamp pose advances inertial physics");p=pose(a);q=pose(a);with_render.update(p,frame(1017,.085));direct.update(q,frame(1017,.085));check(distance(p.node_world[2],q.node_world[2])<1e-6,"Same-timestamp render changes the inertial translation history");const auto solved=p;auto f=frame(1017,-.915);f.origin_delta={1,0,0};p=pose(a);const auto rebased=with_render.update(p,f);check(!rebased.steps&&!rebased.reset&&distance(p.node_world[2],solved.node_world[2])<1e-6,"Origin-only duplicate changes the inertial hair pose");}
void resets(){const auto a=fixture();for(int kind=0;kind<4;++kind){vrm::SpringBones solver(a,inertia);auto p=pose(a);for(unsigned i=0;i<=60;++i){p=pose(a);solver.update(p,frame(1000+i*17,i*.085));}auto f=frame(2037,5.1);if(kind==0)++f.actor_id;else if(kind==1)++f.source_epoch;else if(kind==2)f.timestamp_ms+=300;else f.model_to_world[12]+=4;p=pose(a);check(solver.update(p,f).reset,"Inertial discontinuity does not reset history");p=pose(a);f.timestamp_ms+=17;solver.update(p,f);check(std::abs(p.node_world[2][12])<1e-6,"Inertial reset retains stale motion");}}
void existing_profiles(){for(auto mode:{vrm::SpringMotion::Authored,vrm::SpringMotion::Relaxed})check(std::abs(run(fixture(),5,60,mode).node_world[2][12])<1e-6,"Inertial profile changes existing centered spring behavior");}
void damping_and_speed(){const auto a=fixture();check(run(a,5).node_world[2][12]<run(a,1.5).node_world[2][12]-.03f,"Inertial drag does not respond to faster movement");}
void virtual_tips(){auto a=fixture();a.spring_joints[0].child=-1;a.nodes[1].local=at(0,-.3f);check(std::abs(run(a,5).node_world[1][1])>.01,"Legacy virtual tails do not receive world-space inertia");}
void torso_clearance(){
 auto a=fixture();vrm::SpringCollider c;c.node=3;c.offset={-.15f,.9f,0};c.radius=.08f;a.spring_colliders={c};a.spring_joints[0].colliders={0};vrm::SpringBones solver(a,inertia);auto p=pose(a);
 for(unsigned i=0;i<=240;++i){p=pose(a);const auto r=solver.update(p,frame(1000+i*1000/60,i*5./60));check(!r.error,"Inertial torso contact failed");if(!r.applied)continue;const auto& root=p.node_world[1];const auto& end=p.node_world[2];double along=0,len2=0;for(int k=0;k<3;++k){const double axis=end[12+k]-root[12+k];len2+=axis*axis;along+=axis*(c.offset[k]-root[12+k]);}along=std::fmax(0.,std::fmin(1.,along/len2));double gap2=0;for(int k=0;k<3;++k)gap2+=std::pow(root[12+k]+along*(end[12+k]-root[12+k])-c.offset[k],2);check(std::sqrt(gap2)>=.08-1e-6,"World-space inertia lets hair penetrate the torso");}
}
void turn_and_catchup(){
 const auto a=fixture();vrm::SpringBones solver(a,inertia);auto p=pose(a);for(unsigned i=0;i<=180;++i){p=pose(a);solver.update(p,frame(1000+i*17,i*.085));}
 p=pose(a);solver.update(p,frame(4077,15.3,.085));check(p.node_world[2][12]<-.03f&&p.node_world[2][14]<0,"Turning loses inertial lag");
 for(unsigned i=2;i<=90;++i){p=pose(a);solver.update(p,frame(4060+i*17,15.3,i*.085));}
 check(std::abs(p.node_world[2][12])<.01&&p.node_world[2][14]<-.03f,"Inertial hair fails to follow the new direction");
 auto f=frame(5690,15.3,8.15);p=pose(a);const auto r=solver.update(p,f);check(r.steps==4&&!r.error&&std::abs(distance(p.node_world[1],p.node_world[2])-.3)<3e-6,"Capped inertial catch-up advances too many ticks or stretches hair");
}
void damped_release(){
 const auto a=fixture();vrm::SpringBones solver(a,inertia);auto p=pose(a);solver.update(p,frame(1000));p=pose(a);solver.update(p,frame(1017,.05));const double displaced=std::abs(p.node_world[2][12]);
 for(unsigned i=2;i<=150;++i){p=pose(a);solver.update(p,frame(1000+i*17,.05));}
 check(displaced>.01&&std::abs(p.node_world[2][12])<displaced*.01,"Inertia does not damp a movement impulse back to rest");
}
void gentler_running(){
 const auto a=fixture();const double walk=-run(a,1.5).node_world[2][12],running=-run(a,5).node_world[2][12];
 check(walk>.135&&walk<.145,"Running attenuation changes the established walking inertia");
 check(running>.16&&running<.19,"Running inertia still exposes too much separation or has lost its response");
}
void speed_ramp(){double previous=0;for(double speed:{2.,2.5,3.,4.,5.,6.,8.}){const double trail=-run(fixture(),speed).node_world[2][12];check(trail>previous+.0001,"Running attenuation makes faster travel trail less than slower travel");previous=trail;}}
void head_motion_control(){
 auto a=fixture(),reference=a;reference.spring_joints[0].center=-1;reference.spring_joints[0].drag=.7f;reference.spring_joints[0].stiffness=2;vrm::SpringBones control(reference,vrm::SpringMotion::Authored),trial(a,inertia);auto p=pose(reference),q=pose(a);control.update(p,frame(1000));trial.update(q,frame(1000));p=pose(reference);q=pose(a);for(int n:{4,1,2})p.node_world[n][12]=q.node_world[n][12]=.05f;control.update(p,frame(1017));trial.update(q,frame(1017));check(distance(p.node_world[2],q.node_world[2])<2e-6,"Running attenuation changes stationary head-motion inertia");
}
void vertical_motion_control(){
 auto a=fixture(),reference=a;a.nodes[2].world=reference.nodes[2].world=at(.18f,.76f);a.nodes[2].local=reference.nodes[2].local=at(.18f,-.24f);reference.spring_joints[0].center=-1;reference.spring_joints[0].drag=.7f;reference.spring_joints[0].stiffness=2;vrm::SpringBones control(reference,vrm::SpringMotion::Authored),trial(a,inertia);auto p=pose(reference),q=pose(a);control.update(p,frame(1000));trial.update(q,frame(1000));auto f=frame(1017);f.model_to_world[13]=float(5./60);p=pose(reference);control.update(p,f);const auto fixed=p;f.model_to_world[13]=.085f;q=pose(a);trial.update(q,f);const double x=fixed.node_world[2][12],y=fixed.node_world[2][13]+5./60-.085-1,z=fixed.node_world[2][14],length=std::sqrt(x*x+y*y+z*z);check(std::abs(q.node_world[2][12]-x*.3/length)<2e-6&&std::abs(q.node_world[2][13]-(1+y*.3/length))<2e-6,"Horizontal running attenuation changes vertical inertia");
}
void carry_cache(){
 const auto a=fixture();vrm::SpringBones cached(a,inertia),changed(a,inertia);auto p=pose(a),q=p;cached.update(p,frame(1000));changed.update(q,frame(1000));p=pose(a);q=pose(a);cached.update(p,frame(1008,.04));changed.update(q,frame(1008,.04));p=pose(a);q=pose(a);q.node_world[3][12]=.000001f;
 const auto first=cached.update(p,frame(1016,.04)),second=changed.update(q,frame(1016,.04));check(!first.steps&&!second.steps&&distance(p.node_world[2],q.node_world[2])<1e-7,"Unrelated pose change invalidates a stale running-carry render cache");
}
void changing_speed_samples(){
 auto a=fixture(),reference=a;reference.spring_joints[0].drag=.7f;reference.spring_joints[0].stiffness=2;vrm::SpringBones dense(a,inertia),sparse(a,inertia),control(reference,vrm::SpringMotion::Authored);auto p=pose(a),q=p,r=pose(reference);dense.update(p,frame(1000));sparse.update(q,frame(1000));control.update(r,frame(1000));p=pose(a);dense.update(p,frame(1016,.08));p=pose(a);q=pose(a);dense.update(p,frame(1017,.081));sparse.update(q,frame(1017,.081));
 // 16 ms at 5 m/s carries .08*.57=.0456 m. The last 1 ms at walking
 // speed contributes zero carry; it must not reweight the earlier travel.
 constexpr double fixed_actor=.08+(1./60-.016);r=pose(reference);r.node_world[0][12]=float(.0456-fixed_actor);control.update(r,frame(1017,fixed_actor));r=pose(reference);r.node_world[0][12]=float(.0456-.081);control.update(r,frame(1017,.081));
 check(distance(p.node_world[2],r.node_world[2])<2e-6,"Latest sample reweights carry from an earlier movement interval");
 check(distance(p.node_world[2],q.node_world[2])<.002,"Subtick speed change creates a large dense-versus-sparse jolt");
}
int main(){unsigned failed=0;for(int test=0;test<22;++test)try{
 if(test==0)world_motion();else if(test==1)no_force_reference();else if(test==2)idle_and_body();else if(test==3)stop_settles();else if(test==4)origins();else if(test==5)headings();else if(test==6)frame_rates();else if(test==7)render_only_samples();else if(test==8)resets();else if(test==9)existing_profiles();else if(test==10)damping_and_speed();else if(test==11)virtual_tips();else if(test==12)torso_clearance();else if(test==13)same_timestamp_render();else if(test==14)turn_and_catchup();else if(test==15)damped_release();else if(test==16)gentler_running();else if(test==17)speed_ramp();else if(test==18)head_motion_control();else if(test==19)vertical_motion_control();else if(test==20)carry_cache();else changing_speed_samples();
 }catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<e.what()<<"\n";}
 if(!failed)std::cout<<"PASS 22 inertial scenarios: gentler running, preserved walking/head/vertical motion, monotone speed ramp, no added force, damping/stops/turns, FPS, catch-up, origin, duplicate/render-only samples, changing speed/cache controls, resets, lengths and body/torso isolation\n";
 return failed?1:0;
}
