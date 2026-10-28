#include "../src/spring_bones.hpp"
#include <cmath>
#include <iostream>
#include <stdexcept>

using M=vrm::Matrix;const M I{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void check(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
M at(float x,float y=0){auto m=I;m[12]=x;m[13]=y;return m;}
vrm::Avatar hair(float length,bool leaf=true){
 vrm::Avatar a;a.version=vrm::VrmVersion::V0;a.nodes.resize(5);a.humanoid={{"hips",0},{"head",4}};
 for(auto& n:a.nodes){n.local=n.world=I;n.active=true;}
 a.nodes[0].children={4,3};a.nodes[4].parent=0;a.nodes[4].children={1};a.nodes[1].parent=4;a.nodes[1].children={2};a.nodes[2].parent=1;a.nodes[3].parent=0;a.nodes[2].local=a.nodes[2].world=at(length);
 vrm::SpringJoint j;j.node=1;j.child=2;j.drag=1;j.stiffness=0;a.spring_joints.push_back(j);
 if(leaf){j.node=2;j.child=-1;a.spring_joints.push_back(j);}
 return a;
}
vrm::RetargetResult pose(const vrm::Avatar& a,float x=0,float y=0){vrm::RetargetResult p;for(const auto& n:a.nodes)p.node_world.push_back(n.world);for(int n:{4,1,2}){p.node_world[n][12]+=x;p.node_world[n][13]+=y;}if(!a.skins.empty()){vrm::SkinPalette s;s.mesh_node=5;s.skin=0;s.matrices.assign(a.skins[0].joints.size(),I);p.palettes.push_back(s);}return p;}
vrm::SpringFrame frame(unsigned time){vrm::SpringFrame f;f.timestamp_ms=time;f.actor_id=1;f.source_epoch=1;return f;}
double local_cos(const vrm::Avatar& a,int node,const vrm::RetargetResult& p){
 const auto& m=p.node_world[node];const auto& parent=p.node_world[a.nodes[node].parent];double trace=0;for(int k=0;k<3;++k)for(int row=0;row<3;++row)trace+=double(m[k*4+row])*parent[k*4+row];return (trace-1)*.5;
}
void no_backward_folds(float length){
 const auto a=hair(length);vrm::SpringBones solver(a,vrm::SpringMotion::Relaxed);check(solver.ready(),"Hair shape fixture disabled");auto p=pose(a);solver.update(p,frame(1000));p=pose(a,.5f);const auto result=solver.update(p,frame(1017));
 check(result.applied&&!result.error,"Hair shape solve failed");
 for(int n:{1,2})check(local_cos(a,n,p)>=.5-1e-5,"Hair or virtual end bone folds backward after attachment motion");
 double length2=0;for(int k=0;k<3;++k)length2+=std::pow(double(p.node_world[2][12+k])-p.node_world[1][12+k],2);check(std::abs(std::sqrt(length2)-length)<1e-6,"Shape retention stretches a bone");
 auto duplicate=pose(a,.5f);const auto repeated=solver.update(duplicate,frame(1017));check(!repeated.steps&&duplicate.node_world==p.node_world,"Shape retention advances duplicate poses");
}
void motion_is_retained(){
 const auto a=hair(.15f);vrm::SpringBones solver(a,vrm::SpringMotion::Relaxed);auto p=pose(a);solver.update(p,frame(1000));p=pose(a,0,.03f);solver.update(p,frame(1017));
 check(p.node_world[2][13]<.025f&&p.node_world[2][13]>-.1f,"Shape retention freezes normal hair lag");
}
double distance_to_sphere(const M& end,float cy){
 const double x=end[12],y=end[13],z=end[14],length2=x*x+y*y+z*z,t=std::fmax(0.,std::fmin(1.,(.5*x+cy*y)/length2));return std::sqrt(std::pow(t*x-.5,2)+std::pow(t*y-cy,2)+std::pow(t*z,2));
}
void nearby_clear_bend(){
 auto a=hair(1,false);vrm::SpringCollider c;c.node=3;c.offset={.5f,.2f,0};c.radius=.3f;a.spring_colliders.push_back(c);c.offset[1]=-.2f;a.spring_colliders.push_back(c);a.spring_joints[0].colliders={0,1};
 vrm::SpringBones solver(a,vrm::SpringMotion::Relaxed);auto p=pose(a);solver.update(p,frame(1000));p=pose(a);solver.update(p,frame(1017));
 for(float y:{-.2f,.2f})check(distance_to_sphere(p.node_world[2],y)>=.3-1e-6,"Shape preference loses body clearance");
 // A 30-degree bend towards Z clears both spheres (distance > .32 m).
 // Prefer a nearby clear shape over a roughly 56-degree sideways escape.
 check(local_cos(a,1,p)>std::cos(35.*3.141592653589793/180),"Collision fallback discards a nearby clear hair shape");
}
void collision_has_priority(){
 auto a=hair(1,false);vrm::SpringCollider c;c.node=3;c.offset={.5f,0,0};c.radius=.45f;a.spring_colliders={c};a.spring_joints[0].colliders={0};
 vrm::SpringBones solver(a,vrm::SpringMotion::Relaxed);auto p=pose(a);solver.update(p,frame(1000));p=pose(a);solver.update(p,frame(1017));
 check(distance_to_sphere(p.node_world[2],0)>=.45-1e-6,"Soft bend preference lets hair penetrate the body");
}
void authored_mode_is_preserved(){
 const auto a=hair(.15f);vrm::SpringBones solver(a,vrm::SpringMotion::Authored);auto p=pose(a);solver.update(p,frame(1000));p=pose(a,.5f);solver.update(p,frame(1017));
 check(local_cos(a,1,p)<0,"Runtime shape preference changes authored spring mode");
}
void skinned_curve_is_retained(){
 auto a=hair(.15f);a.nodes.resize(6);a.nodes[5].active=true;a.nodes[5].local=a.nodes[5].world=I;a.nodes[5].mesh=0;a.nodes[5].skin=0;
 vrm::Skin skin;skin.joints={1,2};skin.inverse_bind={I,at(-.15f)};a.skins={skin};
 vrm::SpringBones solver(a,vrm::SpringMotion::Relaxed);auto p=pose(a);solver.update(p,frame(1000));p=pose(a,0,-.5f);solver.update(p,frame(1017));
 // Each individual bend can be small while their accumulated rotations fold
 // the visible strip. Check a vertex 25 cm beyond its weighted joint too.
 const auto& matrix=p.palettes[0].matrices[1];const double skinned_y=.4*matrix[1]+matrix[13];
 check(skinned_y<-.25,"Individually limited bones still splay the skinned hair strip");
 check(p.node_world[2][0]>.8f,"Hair curve loses its posed direction down the chain");
}
void posed_shape_heading(){
 const auto a=hair(.15f);std::vector<M> reference;
 for(double angle:{0.,.31,.73,1.11}){
  vrm::SpringBones solver(a,vrm::SpringMotion::Relaxed);auto f=frame(1000);f.model_to_world[5]=f.model_to_world[10]=float(std::cos(angle));f.model_to_world[6]=float(std::sin(angle));f.model_to_world[9]=float(-std::sin(angle));
  auto p=pose(a);solver.update(p,f);p=pose(a,0,-.5f);f.timestamp_ms=1017;solver.update(p,f);
  if(angle==0)reference=p.node_world;else for(int n:{1,2})for(int k=0;k<16;++k)check(std::abs(p.node_world[n][k]-reference[n][k])<2e-5,"Posed hair shape depends on world heading");
 }
}
void shape_render_cache(){
 const auto a=hair(.15f);vrm::SpringBones solver(a,vrm::SpringMotion::Relaxed);auto p=pose(a);solver.update(p,frame(1000));p=pose(a,0,-.5f);solver.update(p,frame(1017));const auto solved=p;
 auto f=frame(1017);f.model_to_world=at(-1);f.origin_delta={1,0,0};p=pose(a,0,-.5f);const auto rebased=solver.update(p,f);
 check(!rebased.steps&&!rebased.reset&&!rebased.error,"Shape origin rebase resets or advances physics");for(int n:{1,2})for(int k=0;k<16;++k)check(std::abs(p.node_world[n][k]-solved.node_world[n][k])<1e-6,"Shape render cache is not rebased with the camera");
 f.origin_delta={};p=pose(a,0,-.3f);const auto changed=solver.update(p,f);check(!changed.steps&&std::abs(p.node_world[1][13]+.3f)<1e-6,"Shape render cache hides a changed animation pose");
 const auto projected=p;auto duplicate=pose(a,0,-.3f);solver.update(duplicate,f);check(duplicate.node_world==projected.node_world,"Repeated interpolated shape pose is unstable");
}
using Sphere=std::array<double,4>;
void narrow_collision_escape(bool preserve_clear_input){
 const std::array<double,3> desired=preserve_clear_input?std::array<double,3>{-.478973907557,-.445800366997,.756205017614}:std::array<double,3>{.958892423232,.232818496804,.162237074104};
 const std::vector<Sphere> spheres=preserve_clear_input?std::vector<Sphere>{
  {-.382613090211,-.046870929490,-.266766857827,.467527326771},
  {.616038651888,.477507837731,.279668294376,.821885052574},
  {.304550966341,-.563138377345,.507653639530,.544635662432},
  {.504946656915,-.451178641373,-.381387862602,.547567410066}}:std::vector<Sphere>{
  {.432646731248,-.094503027883,.190941002533,.277913517079},
  {.427687962632,.219063753532,-.163275756108,.379961662489}};
 auto a=hair(1,false);for(const auto& sphere:spheres){vrm::SpringCollider c;c.node=3;for(int k=0;k<3;++k)c.offset[k]=float(sphere[k]);c.radius=float(sphere[3]);a.spring_joints[0].colliders.push_back(a.spring_colliders.size());a.spring_colliders.push_back(c);}
 vrm::SpringBones solver(a,vrm::SpringMotion::Relaxed);auto p=pose(a);
 // Reset at a direction whose next stiffness step produces the desired input.
 constexpr double h=2./60;const double t=h*desired[0]+std::sqrt(1-h*h*(1-desired[0]*desired[0]));std::array<double,3> before{};
 for(int k=0;k<3;++k)before[k]=t*desired[k]-(k==0?h:0);
 const double side_length=std::hypot(before[0],before[1]);const std::array<double,3> side{-before[1]/side_length,before[0]/side_length,0};
 const std::array<double,3> normal{-before[2]*side[1],before[2]*side[0],before[0]*side[1]-before[1]*side[0]};
 for(int k=0;k<3;++k){p.node_world[1][k]=p.node_world[2][k]=float(before[k]);p.node_world[1][4+k]=p.node_world[2][4+k]=float(side[k]);p.node_world[1][8+k]=p.node_world[2][8+k]=float(normal[k]);p.node_world[2][12+k]=float(before[k]);}
 solver.update(p,frame(1000));p=pose(a);const auto result=solver.update(p,frame(1017));check(result.applied&&!result.error,"Narrow collision escape failed");
 std::array<double,3> end{};double length2=0;for(int k=0;k<3;++k){end[k]=p.node_world[2][12+k];length2+=end[k]*end[k];}
 for(const auto& sphere:spheres){double along=0;for(int k=0;k<3;++k)along+=end[k]*sphere[k];along=std::fmax(0.,std::fmin(1.,along/length2));double distance2=0;for(int k=0;k<3;++k)distance2+=std::pow(end[k]*along-sphere[k],2);check(std::sqrt(distance2)>=sphere[3]-1e-6,"Bend preference discarded a clear input and penetrated the body");}
 if(!preserve_clear_input)check(end[0]/std::sqrt(length2)>=std::cos(25.*3.141592653589793/180)-1e-6,"Cone samples miss a feasible preferred hair shape");
}
int main(){unsigned failed=0;for(int test=0;test<11;++test)try{
 if(test==0)no_backward_folds(.15f);else if(test==1)no_backward_folds(1);else if(test==2)motion_is_retained();else if(test==3)nearby_clear_bend();else if(test==4)collision_has_priority();else if(test==5)authored_mode_is_preserved();else if(test==6)skinned_curve_is_retained();else if(test==7)posed_shape_heading();else if(test==8)shape_render_cache();else narrow_collision_escape(test==9);
 }catch(const std::exception& e){++failed;std::cerr<<"FAIL "<<e.what()<<"\n";}
 if(!failed)std::cout<<"PASS 11 hair-shape scenarios: skinned curve, virtual tips, live motion, narrow clearance, safe fallback, collision priority, authored mode, headings and render-cache rebasing\n";
 return failed?1:0;
}
