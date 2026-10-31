#include "../src/spring_bones.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using M=vrm::Matrix;
const M I{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
constexpr auto flow=vrm::SpringMotion::Airflow;
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
vrm::RetargetResult travel(double speed,int fps=60,vrm::SpringMotion mode=flow,bool hair=true,bool rebased=false,double yaw=0){
 const auto a=fixture(hair);vrm::SpringBones solver(a,mode);check(solver.ready(),"Airflow profile disabled");auto p=pose(a);double previous_origin=0;
 for(int i=0;i<=fps*4;++i){const unsigned elapsed=unsigned(i)*1000/unsigned(fps);const double t=elapsed*.001,origin=rebased?std::floor(t*5)*.5:0;
  auto f=frame(1000+elapsed,speed*t-origin);f.origin_delta={origin-previous_origin,0,0};previous_origin=origin;
  f.model_to_world[0]=f.model_to_world[10]=float(std::cos(yaw));f.model_to_world[2]=float(-std::sin(yaw));f.model_to_world[8]=float(std::sin(yaw));
  p=pose(a);const auto r=solver.update(p,f);check(!r.error,"Travel solve failed");
  check(std::abs(distance(p.node_world[1],p.node_world[2])-.3)<2e-6,"Airflow stretches hair");
  auto duplicate=pose(a);f.origin_delta={};const auto repeated=solver.update(duplicate,f);check(!repeated.steps&&duplicate.node_world==p.node_world,"Duplicate travel pose advances airflow");
 }
 return p;
}
void steady_flow(){const auto idle=travel(0),run=travel(5);check(std::abs(idle.node_world[2][12])<1e-6,"Idle hair has artificial wind");check(run.node_world[2][12]<-.02f,"Steady forward travel does not create sustained backward hair flow");}
void speed_response(){check(travel(6).node_world[2][12]<travel(2).node_world[2][12]-.01f,"Running airflow does not increase with travel speed");}
void direction_response(){
 const auto reference=travel(5);check(travel(-5).node_world[2][12]>.02f,"Reverse travel does not reverse hair flow");
 for(double yaw:{.31,.73,1.57}){const auto p=travel(5,60,flow,true,false,yaw);const double x=p.node_world[2][12],z=p.node_world[2][14];check(std::abs(std::cos(yaw)*x+std::sin(yaw)*z-reference.node_world[2][12])<2e-5&&std::abs(-std::sin(yaw)*x+std::cos(yaw)*z)<2e-5,"Airflow changes with actor facing instead of actual travel direction");}
}
void frame_rates(){const auto reference=travel(5);for(int fps:{25,120})check(distance(travel(5,fps).node_world[2],reference.node_world[2])<2e-5,"Steady airflow depends on rendering FPS");}
void origins(){const auto reference=travel(5),rebased=travel(5,60,flow,true,true);check(distance(reference.node_world[2],rebased.node_world[2])<2e-5,"Camera-origin changes produce false travel wind");check(std::abs(travel(0,60,flow,true,true).node_world[2][12])<1e-6,"Moving camera creates wind on a stationary actor");}
void stop_settles(){const auto a=fixture();vrm::SpringBones solver(a,flow);auto p=pose(a);for(int i=0;i<=360;++i){const unsigned elapsed=unsigned(i)*1000/60;p=pose(a);solver.update(p,frame(1000+elapsed,5*std::min(elapsed*.001,4.)));}check(std::abs(p.node_world[2][12])<1e-4,"Airflow fails to settle after stopping");}
void animated_body_is_not_wind(){
 auto a=fixture();a.spring_joints[0].center=4;vrm::SpringBones solver(a,flow);auto p=pose(a);
 for(int i=0;i<=240;++i){p=pose(a);const float x=float(.025*std::sin(i*.2));for(int n:{0,4,1,2})p.node_world[n][12]=x;solver.update(p,frame(1000+unsigned(i)*1000/60));check(std::abs(p.node_world[2][12]-x)<1e-6,"Animated hips/head bob is misread as travel airflow");}
}
void resets_clear_wind(){
 const auto a=fixture();for(int kind=0;kind<4;++kind){vrm::SpringBones solver(a,flow);auto p=pose(a);for(unsigned i=0;i<=60;++i){p=pose(a);solver.update(p,frame(1000+i*17,i*.085));}
  auto f=frame(2037,5.1);if(kind==0)++f.actor_id;else if(kind==1)++f.source_epoch;else if(kind==2)f.timestamp_ms+=300;else f.model_to_world[12]+=4;
  p=pose(a);const auto reset=solver.update(p,f);check(reset.reset&&!reset.error,"Travel discontinuity does not reset airflow");f.timestamp_ms+=17;p=pose(a);solver.update(p,f);check(std::abs(p.node_world[2][12])<1e-6,"Reset preserves a stale travel gust");
 }
}
void other_profiles_unchanged(){for(auto mode:{vrm::SpringMotion::Authored,vrm::SpringMotion::Relaxed})check(std::abs(travel(5,60,mode).node_world[2][12])<1e-6,"Airflow changes an existing spring profile");}
void body_springs_are_isolated(){check(std::abs(travel(5,60,flow,false).node_world[2][12])<1e-6,"Hair airflow affects body springs");}
void loose_r2_response(){const auto a=fixture();vrm::SpringBones solver(a,flow);auto p=pose(a);solver.update(p,frame(1000));p=pose(a);for(int n:{4,1,2})p.node_world[n][12]=.5f;solver.update(p,frame(1017));check(p.node_world[2][12]<.32f,"Airflow accidentally retains the r4 hard shape preference");}
void arm_comparison_and_torso(){
 for(int variant=0;variant<3;++variant){auto a=fixture();if(variant<2)a.humanoid["rightUpperArm"]=3;vrm::SpringCollider c;c.node=3;c.offset={0,.85f,0};c.radius=.05f;a.spring_colliders={c};if(variant!=1)a.spring_joints[0].colliders={0};
  vrm::SpringBones solver(a,flow);auto p=pose(a);solver.update(p,frame(1000));p=pose(a);solver.update(p,frame(1017));
  if(variant==1)check(distance(p.node_world[2],at(0,.7f))<1e-6,"Arm-off comparison moves otherwise idle hair");
  else{const auto& root=p.node_world[1];const auto& tail=p.node_world[2];double axis2=0,along=0;for(int k=0;k<3;++k){const double axis=double(tail[12+k])-root[12+k];axis2+=axis*axis;along+=axis*(c.offset[k]-root[12+k]);}along=std::fmax(0.,std::fmin(1.,along/axis2));double gap2=0;for(int k=0;k<3;++k)gap2+=std::pow(root[12+k]+along*(tail[12+k]-root[12+k])-c.offset[k],2);check(std::sqrt(gap2)>=.05-1e-6,"Airflow loses full-strand arm or torso clearance");}
 }
}
void tip_emphasis(){
 auto a=fixture();a.nodes.resize(6);a.nodes[0].children={4};a.nodes[2].children={3};a.nodes[3].parent=2;a.nodes[3].world=at(0,.4f);a.nodes[3].local=at(0,-.3f);a.nodes[3].children={5};a.nodes[5].parent=3;a.nodes[5].active=true;a.nodes[5].local=at(0,-.3f);a.nodes[5].world=at(0,.1f);
 auto j=a.spring_joints[0];j.node=2;j.child=3;a.spring_joints.push_back(j);j.node=3;j.child=5;a.spring_joints.push_back(j);vrm::SpringBones solver(a,flow);auto p=pose(a);
 for(unsigned i=0;i<=240;++i){p=pose(a);solver.update(p,frame(1000+i*1000/60,i*5./60));}
 const double root=std::atan2(p.node_world[1][1],p.node_world[1][0]),parent=std::atan2(p.node_world[2][1],p.node_world[2][0]),tip=std::atan2(p.node_world[3][1],p.node_world[3][0]);check(std::abs(tip-parent)>std::abs(root)*1.1,"Airflow does not give the tips more response than the roots");
}
void subtick_and_dead_zone(){
 const auto a=fixture();vrm::SpringBones solver(a,flow);auto p=pose(a);solver.update(p,frame(1000));p=pose(a);const auto r=solver.update(p,frame(1008,.04));check(!r.steps&&std::abs(p.node_world[2][12])<1e-6,"Render-only sample applies airflow without a physics tick");check(std::abs(travel(.1).node_world[2][12])<1e-6,"Tiny travel noise creates a persistent hair gust");
}
void speed_is_bounded(){check(distance(travel(8).node_world[2],travel(12).node_world[2])<2e-5,"Airflow force exceeds its configured speed cap");}
void virtual_tips(){auto a=fixture();a.spring_joints[0].child=-1;a.nodes[1].local=at(0,-.3f);vrm::SpringBones solver(a,flow);auto p=pose(a);for(unsigned i=0;i<=240;++i){p=pose(a);solver.update(p,frame(1000+i*1000/60,i*5./60));}check(std::abs(p.node_world[1][1])>.01,"Legacy virtual hair tips do not receive airflow");}
void turn_transition(){
 const auto a=fixture();vrm::SpringBones solver(a,flow);auto p=pose(a);for(unsigned i=0;i<=180;++i){p=pose(a);solver.update(p,frame(1000+i*17,i*.085));}
 const auto before=p;for(unsigned i=1;i<=60;++i){const auto previous=p;p=pose(a);solver.update(p,frame(4060+i*17,15.3,i*.085));check(distance(p.node_world[2],previous.node_world[2])<.01,"Turning travel direction snaps the hair");if(i==1)check(p.node_world[2][12]<-.01f&&p.node_world[2][14]<0,"Hair does not retain lag when travel direction turns");}
 check(before.node_world[2][12]<-.02f&&std::abs(p.node_world[2][12])<.004f&&p.node_world[2][14]<-.02f,"Hair does not follow the new travel direction after a turn");
}
void running_sweep(){
 const double walking=-travel(1.5).node_world[2][12],running=-travel(5).node_world[2][12];
 check(walking>.0069&&walking<.0075,"Running boost changes the established walking response");
 check(running>.028&&running<.033,"Running hair needs a modestly stronger backward sweep");
}
int main(){unsigned failed=0;for(int test=0;test<18;++test)try{
 if(test==0)steady_flow();else if(test==1)speed_response();else if(test==2)direction_response();else if(test==3)frame_rates();else if(test==4)origins();else if(test==5)stop_settles();else if(test==6)animated_body_is_not_wind();else if(test==7)resets_clear_wind();else if(test==8)other_profiles_unchanged();else if(test==9)body_springs_are_isolated();else if(test==10)loose_r2_response();else if(test==11)arm_comparison_and_torso();else if(test==12)tip_emphasis();else if(test==13)subtick_and_dead_zone();else if(test==14)speed_is_bounded();else if(test==15)virtual_tips();else if(test==16)turn_transition();else running_sweep();
 }catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<e.what()<<"\n";}
 if(!failed)std::cout<<"PASS 18 airflow scenarios: running/walking sweep, direction/turn lag, tips, loose response, stop/reset, camera origin, duplicates, FPS, body isolation and arm/torso controls\n";
 return failed?1:0;
}
