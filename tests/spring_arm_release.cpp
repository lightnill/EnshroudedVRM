#include "../src/spring_bones.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using M=vrm::Matrix;
const M I{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
M at(float x,float y=0){auto m=I;m[12]=x;m[13]=y;return m;}
vrm::Avatar fixture(bool hair,const char* arm,bool helper){
 vrm::Avatar a;a.version=vrm::VrmVersion::V0;a.nodes.resize(6);a.humanoid={{"hips",0},{"head",4},{arm,3}};
 for(auto& n:a.nodes)n.local=n.world=I,n.active=true;
 a.nodes[0].children={4,3};a.nodes[4].parent=0;a.nodes[4].children={1};a.nodes[1].parent=4;a.nodes[1].children={2};a.nodes[2].parent=1;a.nodes[2].local=a.nodes[2].world=at(.3f);
 a.nodes[3].parent=0;a.nodes[3].children={5};a.nodes[5].parent=3;
 if(!hair){a.nodes[4].children.clear();a.nodes[0].children.push_back(1);a.nodes[1].parent=0;}
 vrm::SpringCollider c;c.node=helper?5:3;c.offset={.3f,0,0};c.radius=.08f;a.spring_colliders={c};
 vrm::SpringJoint j;j.node=1;j.child=2;j.drag=1;j.stiffness=0;j.colliders={0};a.spring_joints={j};return a;
}
vrm::RetargetResult pose(const vrm::Avatar& a,float arm_y){vrm::RetargetResult p;for(const auto& n:a.nodes)p.node_world.push_back(n.world);for(int n:{3,5})p.node_world[n][13]=arm_y;return p;}
vrm::SpringFrame frame(unsigned time){vrm::SpringFrame f;f.timestamp_ms=time;f.actor_id=1;f.source_epoch=1;return f;}
double separation(const M& a,const M& b){double s=0;for(int k=0;k<3;++k)s+=std::pow(double(a[12+k])-b[12+k],2);return std::sqrt(s);}
void moving_arm_releases_hair(const char* side,bool helper,int fps){
 const auto a=fixture(true,side,helper);const auto original_colliders=a.spring_joints[0].colliders;vrm::SpringBones solver(a,vrm::SpringMotion::Relaxed);
 auto p=pose(a,.2f);solver.update(p,frame(1000));
 for(int i=1;i<=fps*3;++i){const float y=float(.15*std::cos(double(i)*12/fps));const unsigned time=1000+unsigned(i)*1000/unsigned(fps);p=pose(a,y);const auto r=solver.update(p,frame(time));
  check(r.applied&&!r.error,"Arm-release solve failed");check(separation(p.node_world[2],at(.3f))<1e-6,"Moving arm catches or twitches relaxed hair");
  auto duplicate=pose(a,y);const auto repeated=solver.update(duplicate,frame(time));check(!repeated.steps&&duplicate.node_world==p.node_world,"Repeated arm pose changes the released hair");
 }
 check(a.spring_joints[0].colliders==original_colliders&&a.spring_colliders[0].radius==.08f,"Arm release mutates authored collider metadata");
 // A changed render pose before the first physics tick must use the same policy.
 vrm::SpringBones early(a,vrm::SpringMotion::Relaxed);p=pose(a,.2f);early.update(p,frame(1000));p=pose(a,0);const auto r=early.update(p,frame(1008));
 check(r.applied&&!r.steps&&separation(p.node_world[2],at(.3f))<1e-6,"Between-tick arm pose still catches relaxed hair");
}
void retained_contact(bool hair,vrm::SpringMotion mode,bool torso){
 auto a=fixture(hair,"rightUpperArm",false);if(torso)a.humanoid.erase("rightUpperArm");vrm::SpringBones solver(a,mode);auto p=pose(a,.2f);solver.update(p,frame(1000));p=pose(a,0);const auto r=solver.update(p,frame(1017));
 check(r.applied&&!r.error,"Retained body contact failed");check(separation(p.node_world[2],at(.3f))>=.08-1e-6,"Hair arm release disabled a retained body collision");
}
int main(){unsigned failed=0,scenarios=0;for(const char* side:{"leftUpperArm","rightUpperArm"})for(bool helper:{false,true})for(int fps:{25,60,120})try{++scenarios;moving_arm_releases_hair(side,helper,fps);}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<side<<" helper="<<helper<<" fps="<<fps<<": "<<e.what()<<"\n";}
 for(int control=0;control<3;++control)try{++scenarios;if(control==0)retained_contact(true,vrm::SpringMotion::Authored,false);else if(control==1)retained_contact(false,vrm::SpringMotion::Relaxed,false);else retained_contact(true,vrm::SpringMotion::Relaxed,true);}catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<e.what()<<"\n";}
 if(!failed)std::cout<<"PASS "<<scenarios<<" arm-release scenarios: both arms/helpers, 25/60/120 FPS, duplicate and between-tick poses, immutable metadata, retained torso/Authored/body contacts\n";
 return failed?1:0;
}
