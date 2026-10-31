#include "../src/spring_bones.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using M=vrm::Matrix;
const M I{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void check(bool yes,const char* message){if(!yes)throw std::runtime_error(message);}
M at(float x,float y,float z=0){auto m=I;m[12]=x;m[13]=y;m[14]=z;return m;}
vrm::Avatar fixture(bool capsule=false){
 vrm::Avatar a;a.nodes.resize(4);a.humanoid["hips"]=0;
 for(auto& n:a.nodes)n.active=true,n.local=n.world=I;
 a.nodes[0].children={1,3};a.nodes[1].parent=0;a.nodes[1].children={2};a.nodes[2].parent=1;a.nodes[3].parent=0;
 a.nodes[2].local=a.nodes[2].world=at(1,0);
 vrm::SpringCollider c;c.node=3;c.offset={.5f,capsule?-.3f:0,0};c.tail={.5f,.3f,0};c.radius=.2f;c.capsule=capsule;
 a.spring_colliders={c};vrm::SpringJoint j;j.node=1;j.child=2;j.drag=1;j.stiffness=0;j.hit_radius=.03f;j.colliders={0};a.spring_joints={j};return a;
}
vrm::RetargetResult base(const vrm::Avatar& a){vrm::RetargetResult p;for(const auto& n:a.nodes)p.node_world.push_back(n.world);return p;}
vrm::SpringFrame frame(unsigned time){vrm::SpringFrame f;f.timestamp_ms=time;f.actor_id=1;f.source_epoch=1;return f;}
double length(const M& a,const M& b){double r=0;for(int i=0;i<3;++i)r+=std::pow(double(a[12+i])-b[12+i],2);return std::sqrt(r);}
double line_distance_to_center(const vrm::RetargetResult& p){
 const auto& tail=p.node_world[2];double dot=.5*tail[12],length2=0;for(int i=0;i<3;++i)length2+=double(tail[12+i])*tail[12+i];
 const double t=std::fmax(0.,std::fmin(1.,dot/length2));return std::sqrt(std::pow(t*tail[12]-.5,2)+std::pow(t*tail[13],2)+std::pow(t*tail[14],2));
}
void between_tips(bool capsule){
 auto a=fixture(capsule);vrm::SpringBones solver(a);check(solver.ready(),"Collision fixture disabled");auto p=base(a);solver.update(p,frame(1000));p=base(a);
 const auto r=solver.update(p,frame(1017));check(r.applied&&!r.error,"Collision solve failed");
 if(!capsule)check(line_distance_to_center(p)>=.23-1e-6,"Sphere intersects hair between two outside tips");
 else{
  // The capsule axis lies on x=.5, z=0. Any crossing of that plane must
  // keep at least the combined 23 cm radius away from its finite y span.
  const auto& tail=p.node_world[2];const double t=std::fmax(0.,std::fmin(1.,.5/double(tail[12])));
  const double y=t*tail[13],z=t*tail[14];const double dy=std::fmax(0.,std::abs(y)-.3);
  check(std::sqrt(dy*dy+z*z)>=.23-1e-6,"Capsule intersects hair between two outside tips");
 }
 check(std::abs(length(p.node_world[1],p.node_world[2])-1)<1e-6,"Collision stretched hair");
 check(p.node_world[0]==I&&p.node_world[1][12]==0&&p.node_world[3]==I,"Collision moved body/root");
 auto duplicate=base(a);const auto repeated=solver.update(duplicate,frame(1017));
 check(repeated.steps==0&&duplicate.node_world==p.node_world,"Repeated frame advances collision response");
}
void between_physics_steps(){
 auto a=fixture();a.spring_colliders[0].offset={1,.4f,0};a.spring_joints[0].hit_radius=0;vrm::SpringBones solver(a);
 auto p=base(a);solver.update(p,frame(1000));p=base(a);p.node_world[3]=at(0,-.35f);const auto r=solver.update(p,frame(1008));
 check(r.steps==0&&r.applied&&!r.error,"Substep collision advanced physics");
 const auto& tail=p.node_world[2];const double gap=std::sqrt(std::pow(double(tail[12])-1,2)+std::pow(double(tail[13])-.05,2)+std::pow(double(tail[14]),2));
 check(gap>=.2-1e-6,"Moving body collider penetrates hair between physics ticks");
 check(std::abs(length(p.node_world[1],p.node_world[2])-1)<1e-6,"Substep response stretched hair");
 auto duplicate=base(a);duplicate.node_world[3]=at(0,-.35f);solver.update(duplicate,frame(1008));
 check(duplicate.node_world==p.node_world,"Repeated changed pose gives a different collision result");
}
void root_inside(){
 auto a=fixture();a.spring_colliders[0].radius=.6f;a.spring_joints[0].hit_radius=0;vrm::SpringBones solver(a);auto p=base(a);solver.update(p,frame(1000));p=base(a);
 const auto r=solver.update(p,frame(1017));check(r.applied&&!r.error,"Embedded root disabled hair");
 const auto& tail=p.node_world[2];const double gap=std::sqrt(std::pow(double(tail[12])-.5,2)+std::pow(double(tail[13]),2)+std::pow(double(tail[14]),2));
 check(gap>=.6-1e-6,"Fixed-length projection puts hair tip back inside collider");
 check(std::abs(length(p.node_world[1],p.node_world[2])-1)<1e-6,"Embedded-root response stretched hair");
}
double capsule_gap(const M& tail,const vrm::SpringCollider& c){
 double minimum=1000;const double x=c.capsule?c.tail[0]-c.offset[0]:0,y=c.capsule?c.tail[1]-c.offset[1]:0,z=c.capsule?c.tail[2]-c.offset[2]:0,axis2=x*x+y*y+z*z;
 // Independent dense surface check rather than the production closest-pair helper.
 for(int i=0;i<=10000;++i){const double t=double(i)/10000,px=t*tail[12],py=t*tail[13],pz=t*tail[14];
  const double u=axis2?std::fmax(0.,std::fmin(1.,((px-c.offset[0])*x+(py-c.offset[1])*y+(pz-c.offset[2])*z)/axis2)):0;
  minimum=std::fmin(minimum,std::sqrt(std::pow(px-c.offset[0]-u*x,2)+std::pow(py-c.offset[1]-u*y,2)+std::pow(pz-c.offset[2]-u*z,2)));
 }return minimum;
}
vrm::Avatar oblique_fixture(){auto a=fixture(true);a.nodes[2].local=a.nodes[2].world=at(.3f,0);a.spring_joints[0].hit_radius=0;a.spring_colliders[0].offset={.36f,.3f,0};a.spring_colliders[0].tail={.42f,-.3f,0};a.spring_colliders[0].radius=.21f;return a;}
void oblique_capsule(){
 auto a=oblique_fixture();vrm::SpringBones solver(a);auto p=base(a);solver.update(p,frame(1000));p=base(a);solver.update(p,frame(1017));
 check(capsule_gap(p.node_world[2],a.spring_colliders[0])>=.21-1e-6,"Oblique capsule remains penetrated after collision solve");
 check(std::abs(length(p.node_world[1],p.node_world[2])-.3)<1e-6,"Capsule fallback stretches strand");
}
void overlapping_colliders(){
 auto a=fixture();a.spring_joints[0].hit_radius=0;a.spring_colliders[0].offset={.5f,.2f,0};a.spring_colliders[0].radius=.3f;
 auto c=a.spring_colliders[0];c.offset[1]=-.2f;a.spring_colliders.push_back(c);a.spring_joints[0].colliders={0,1};vrm::SpringBones solver(a);auto p=base(a);solver.update(p,frame(1000));p=base(a);solver.update(p,frame(1017));
 for(const auto& collider:a.spring_colliders)check(capsule_gap(p.node_world[2],collider)>=.3-1e-6,"Later collider correction re-enters earlier sphere");
}
void rebase_duplicate(){
 auto a=oblique_fixture();vrm::SpringBones solver(a);auto p=base(a);solver.update(p,frame(1000));p=base(a);solver.update(p,frame(1017));const auto solved=p;
 auto f=frame(1017);f.model_to_world=at(-1,0);f.origin_delta={1,0,0};p=base(a);const auto r=solver.update(p,f);
 check(r.steps==0&&!r.error,"Origin rebase advances physics");
 for(int k=0;k<16;++k)check(std::abs(p.node_world[2][k]-solved.node_world[2][k])<1e-6,"Origin-only change repeats collision response at same timestamp");
}
void surface_root(bool capsule){
 auto a=fixture(capsule);a.spring_joints[0].hit_radius=0;a.spring_colliders[0].radius=.5f;vrm::SpringBones solver(a);auto p=base(a);solver.update(p,frame(1000));p=base(a);solver.update(p,frame(1017));
 check(capsule_gap(p.node_world[2],a.spring_colliders[0])>=.5-1e-6,"Surface attachment incorrectly permits hair through collider");
}
void single_reachable_tip(){
 auto a=fixture();a.nodes[2].local=a.nodes[2].world=at(.5f,0);a.spring_joints[0].hit_radius=0;a.spring_colliders[0].offset={.25f,0,0};a.spring_colliders[0].radius=.75f;
 vrm::SpringBones solver(a);auto p=base(a);solver.update(p,frame(1000));p=base(a);solver.update(p,frame(1017));
 check(std::abs(p.node_world[2][12]+.5)<1e-6,"Unique reachable surface tip is skipped");
}
vrm::Avatar hair_arm_fixture(){
 auto a=fixture();a.nodes.resize(5);auto& head=a.nodes[4];head.active=true;head.local=head.world=I;head.parent=0;head.children={1};
 a.nodes[0].children={4,3};a.nodes[1].parent=4;a.humanoid["head"]=4;a.humanoid["rightUpperArm"]=3;return a;
}
void hair_arm_contact(){
 auto a=hair_arm_fixture();vrm::SpringBones solver(a);auto p=base(a);solver.update(p,frame(1000));p=base(a);solver.update(p,frame(1017));
 check(length(p.node_world[2],at(1,0))<1e-6,"Arm collider anchors the middle of an otherwise clear hair strand");
 a.spring_colliders[0].offset={1,.05f,0};vrm::SpringBones touching(a);p=base(a);touching.update(p,frame(1000));p=base(a);touching.update(p,frame(1017));
 const auto tail=p.node_world[2];const double gap=std::sqrt(std::pow(double(tail[12])-1,2)+std::pow(double(tail[13])-.05,2)+std::pow(double(tail[14]),2));
 check(gap>=.23-1e-6,"Lighter hair-arm contact disables the authored tip collision");
}
void hair_arm_helper_contact(){
 auto a=hair_arm_fixture();a.nodes.resize(6);a.nodes[5].active=true;a.nodes[5].local=a.nodes[5].world=I;a.nodes[5].parent=3;a.nodes[3].children={5};a.spring_colliders[0].node=5;
 vrm::SpringBones solver(a);auto p=base(a);solver.update(p,frame(1000));p=base(a);solver.update(p,frame(1017));
 check(length(p.node_world[2],at(1,0))<1e-6,"Arm helper bypasses the hair tip-only contact policy");
}
void heading_invariance(){
 for(int shape=0;shape<3;++shape){
 auto a=fixture();a.spring_joints[0].hit_radius=0;a.spring_colliders[0].offset={.5f,.2f,0};a.spring_colliders[0].radius=.3f;
 if(shape==0){auto c=a.spring_colliders[0];c.offset[1]=-.2f;a.spring_colliders.push_back(c);a.spring_joints[0].colliders={0,1};}
 else if(shape==1){a.spring_colliders[0].offset={.5f,0,0};a.spring_colliders[0].radius=.2f;}
 else{a.nodes[2].local=a.nodes[2].world=at(.5f,0);a.spring_colliders[0].offset={.25f,0,0};a.spring_colliders[0].radius=.75f;}
 M reference{};
 for(int turn=0;turn<4;++turn){
  const double angle=std::array<double,4>{0.,.31,.73,1.11}[turn];
  vrm::SpringBones solver(a);auto p=base(a);auto f=frame(1000);f.model_to_world[5]=f.model_to_world[10]=float(std::cos(angle));f.model_to_world[6]=float(std::sin(angle));f.model_to_world[9]=float(-std::sin(angle));
  // The unique-touch boundary has zero geometric slack. Use exactly
  // representable rigid rotations there so float scale error cannot make
  // the sphere physically larger than the entire reachable hair-tip sphere.
  if(shape==2){f.model_to_world[5]=f.model_to_world[10]=std::array<float,4>{1,0,-1,0}[turn];f.model_to_world[6]=std::array<float,4>{0,1,0,-1}[turn];f.model_to_world[9]=-f.model_to_world[6];}
  solver.update(p,f);p=base(a);f.timestamp_ms=1017;solver.update(p,f);
  if(turn==0)reference=p.node_world[2];else for(int k=0;k<16;++k){
   if(std::abs(p.node_world[2][k]-reference[k])>=2e-5)std::cerr<<"heading shape="<<shape<<" angle="<<angle<<" matrix="<<k<<" got="<<p.node_world[2][k]<<" expected="<<reference[k]<<"\n";
   check(std::abs(p.node_world[2][k]-reference[k])<2e-5,"Turning the actor changes the local hair collision result");
  }
 }
 }
}
void collision_velocity(){
 auto a=fixture();a.spring_joints[0].hit_radius=0;a.spring_joints[0].drag=0;a.spring_colliders[0].offset={1,.05f,0};
 vrm::SpringBones solver(a);auto p=base(a);p.node_world[3]=at(0,.4f);solver.update(p,frame(1000));p=base(a);solver.update(p,frame(1017));const auto contact=p.node_world[2];
 p=base(a);p.node_world[3]=at(0,.4f);solver.update(p,frame(1034));
 check(length(p.node_world[2],contact)<1e-6,"Position-only contact correction becomes free-flight hair velocity");
}
int main(){
 unsigned failed=0;
 for(int test=0;test<14;++test)try{
  if(test<2)between_tips(test==1);else if(test==2)between_physics_steps();else if(test==3)root_inside();
  else if(test==4)oblique_capsule();else if(test==5)overlapping_colliders();else if(test==6)rebase_duplicate();else if(test<9)surface_root(test==8);else if(test==9)single_reachable_tip();
  else if(test==10)hair_arm_contact();else if(test==11)hair_arm_helper_contact();else if(test==12)heading_invariance();else collision_velocity();
 }catch(const std::exception&e){++failed;std::cerr<<"FAIL "<<e.what()<<"\n";}
 if(!failed)std::cout<<"PASS 14 collision scenarios: geometry, attachment boundaries, hair-arm release, heading invariance, contact velocity, render poses, duplicates and origin rebasing\n";
 return failed?1:0;
}
