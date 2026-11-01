#include "../src/retarget.hpp"
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <new>
#include <stdexcept>

namespace {bool count_allocations=false;std::size_t allocations=0,allocation_bytes=0;}
[[gnu::noinline]] void* operator new(std::size_t size){if(count_allocations){++allocations;allocation_bytes+=size;}if(auto* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
[[gnu::noinline]] void* operator new[](std::size_t size){return ::operator new(size);}
[[gnu::noinline]] void operator delete(void* p)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete[](void* p)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete(void* p,std::size_t)noexcept{std::free(p);}
[[gnu::noinline]] void operator delete[](void* p,std::size_t)noexcept{std::free(p);}

using M=vrm::Matrix;
const M identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
M at(float x,float y=0,float z=0){auto m=identity;m[12]=x;m[13]=y;m[14]=z;return m;}
M turn(double angle){auto m=identity;m[0]=m[5]=float(std::cos(angle));m[1]=float(std::sin(angle));m[4]=-m[1];return m;}
M mul(const M& a,const M& b){M out{};for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)out[c*4+r]+=a[k*4+r]*b[c*4+k];return out;}
M rigid_inverse(const M& m){auto r=identity;for(int c=0;c<3;++c)for(int k=0;k<3;++k)r[c*4+k]=m[k*4+c];for(int k=0;k<3;++k)r[12+k]=-(r[k]*m[12]+r[4+k]*m[13]+r[8+k]*m[14]);return r;}
void worlds(vrm::Avatar& a){for(std::size_t n=0;n<a.nodes.size();++n){const int p=a.nodes[n].parent;a.nodes[n].world=p<0?a.nodes[n].local:mul(a.nodes[p].world,a.nodes[n].local);}}
vrm::Avatar rig(){
    vrm::Avatar a;a.nodes.resize(13);a.meshes.resize(1);for(auto& n:a.nodes)n.active=true,n.local=identity;
    a.nodes[1].parent=0;a.nodes[1].local=at(0,1);
    a.nodes[2].parent=1;a.nodes[2].local=mul(at(.5f,.5f),turn(.3));
    a.nodes[3].parent=2;a.nodes[3].local=at(.3f);
    a.nodes[4].parent=3;a.nodes[4].local=at(.3f);
    a.nodes[5].parent=4;a.nodes[5].local=at(.2f);
    a.nodes[6].parent=1;a.nodes[6].local=at(0,.8f);
    for(int n:{7,8}){a.nodes[n].parent=0;a.nodes[n].local=mul(at(n==7?2.f:-2.f),turn(n==7?.1:-.2));a.nodes[n].mesh=0;a.nodes[n].skin=0;}
    a.nodes[9].parent=1;a.nodes[9].local=at(.1f,-.1f);a.nodes[10].parent=9;a.nodes[10].local=at(0,-.4f);
    a.nodes[11].parent=10;a.nodes[11].local=at(0,-.4f);a.nodes[12].parent=11;a.nodes[12].local=at(0,0,.15f);
    a.humanoid={{"hips",1},{"leftUpperArm",2},{"leftLowerArm",4},{"leftHand",5},{"head",6},{"leftUpperLeg",9},{"leftLowerLeg",10},{"leftFoot",11},{"leftToes",12}};
    worlds(a);vrm::Skin skin;skin.joints={1,2,3,4,5,6,9,10,11,12};for(int n:skin.joints)skin.inverse_bind.push_back(rigid_inverse(a.nodes[n].world));a.skins={skin};return a;
}
vrm::SourcePose source(const vrm::Avatar& a){vrm::SourcePose s;for(const auto& h:a.humanoid)s.rest[h.first]=a.nodes[h.second].world;s.current=s.rest;return s;}
void animate(vrm::SourcePose& s,unsigned frame){unsigned index=0;for(auto& entry:s.current){const auto r=turn(.25*std::sin(frame*.07+index++*.2));entry.second=s.rest.at(entry.first);for(int c=0;c<3;++c)for(int k=0;k<3;++k){entry.second[c*4+k]=0;for(int j=0;j<3;++j)entry.second[c*4+k]+=r[j*4+k]*s.rest.at(entry.first)[c*4+j];}}s.current.at("hips")[12]+=.02f*float(std::sin(frame*.1));}
void equal(const vrm::RetargetResult& a,const vrm::RetargetResult& b){
    check(bool(a)==bool(b),"Compiled retarget acceptance differs from one-shot validation");
    if(!a){check(a.node_world.empty()&&a.palettes.empty(),"Compiled failure leaks a partial pose");check(a.error==b.error,"Compiled failure changes its diagnostic");return;}
    check(a.node_world.size()==b.node_world.size()&&a.palettes.size()==b.palettes.size(),"Compiled retarget changes output layout");
    for(std::size_t n=0;n<a.node_world.size();++n)check(std::memcmp(a.node_world[n].data(),b.node_world[n].data(),sizeof(M))==0,"Compiled retarget changes node matrix bits");
    for(std::size_t p=0;p<a.palettes.size();++p){const auto& x=a.palettes[p];const auto& y=b.palettes[p];check(x.mesh_node==y.mesh_node&&x.skin==y.skin&&x.matrices.size()==y.matrices.size(),"Compiled retarget changes skin metadata");for(std::size_t j=0;j<x.matrices.size();++j)check(std::memcmp(x.matrices[j].data(),y.matrices[j].data(),sizeof(M))==0,"Compiled retarget changes palette matrix bits");}
}
vrm::RetargetOptions aligned(){vrm::RetargetOptions o;o.align_rest_directions=true;return o;}
void dynamic_motion(){const auto a=rig();auto s=source(a);vrm::Retargeter cache(a);for(unsigned i=0;i<150;++i){animate(s,i);equal(cache.update(s,aligned()),vrm::retarget(a,s,aligned()));}}
void rest_changes(){const auto a=rig();auto s=source(a);vrm::Retargeter cache(a);cache.update(s,aligned());for(int i=0;i<5;++i){s.rest.at("leftLowerArm")[13]+=.03f;s.rest.at("leftLowerLeg")[12]+=.04f;animate(s,20+i);equal(cache.update(s,aligned()),vrm::retarget(a,s,aligned()));}}
void availability_changes(){const auto a=rig();auto s=source(a);vrm::Retargeter cache(a);for(const char* name:{"leftLowerLeg","leftHand","leftLowerArm"}){cache.update(s,aligned());auto changed=s;changed.current.erase(name);equal(cache.update(changed,aligned()),vrm::retarget(a,changed,aligned()));changed.rest.erase(name);equal(cache.update(changed,aligned()),vrm::retarget(a,changed,aligned()));equal(cache.update(s,aligned()),vrm::retarget(a,s,aligned()));}}
void option_changes(){const auto a=rig();auto s=source(a);animate(s,12);vrm::Retargeter cache(a);auto o=aligned();for(int i=0;i<5;++i){cache.update(s,o);if(i==0)o.align_rest_directions=false;else if(i==1)o.source_to_avatar_basis[0]=o.source_to_avatar_basis[10]=-1;else if(i==2)o.source_to_avatar_basis[10]=1;else if(i==3)o.source_rest_height=2;else o.avatar_rest_height=.7f;equal(cache.update(s,o),vrm::retarget(a,s,o));}}
void invalid_dynamic(){const auto a=rig();const auto valid=source(a);vrm::Retargeter cache(a);cache.update(valid,aligned());for(int i=0;i<5;++i){auto bad=valid;if(i==0)bad.current.at("head")[0]=std::numeric_limits<float>::quiet_NaN();else if(i==1)bad.current.at("hips")[0]=0;else if(i==2)bad.current.at("head")[3]=.5f;else if(i==3)bad.current.erase("hips");else bad.current["unknown"]=identity;equal(cache.update(bad,aligned()),vrm::retarget(a,bad,aligned()));equal(cache.update(valid,aligned()),vrm::retarget(a,valid,aligned()));}}
void invalid_rest_and_options(){const auto a=rig();const auto valid=source(a);vrm::Retargeter cache(a);cache.update(valid,aligned());auto bad=valid;bad.rest.at("head")[0]=std::numeric_limits<float>::infinity();equal(cache.update(bad,aligned()),vrm::retarget(a,bad,aligned()));for(int i=0;i<3;++i){auto o=aligned();if(i==0)o.source_rest_height=0;else if(i==1)o.source_to_avatar_basis[0]=2;else o.source_to_avatar_basis[12]=1;equal(cache.update(valid,o),vrm::retarget(a,valid,o));}equal(cache.update(valid,aligned()),vrm::retarget(a,valid,aligned()));}
void unmapped_validation(){const auto a=rig();auto s=source(a);s.rest["unmapped"]=s.current["unmapped"]=identity;vrm::Retargeter cache(a);cache.update(s,aligned());s.current["unmapped"][15]=0;equal(cache.update(s,aligned()),vrm::retarget(a,s,aligned()));s.current["unmapped"]=identity;s.rest["unmapped"][0]=0;equal(cache.update(s,aligned()),vrm::retarget(a,s,aligned()));}
void constraints(){for(auto kind:{vrm::NodeConstraintType::Aim,vrm::NodeConstraintType::Roll,vrm::NodeConstraintType::Rotation}){auto a=rig();a.constraints.push_back({kind,3,6,vrm::ConstraintAxis::X,.6f});auto s=source(a);vrm::Retargeter cache(a);for(unsigned i=0;i<50;++i){animate(s,i);equal(cache.update(s,aligned()),vrm::retarget(a,s,aligned()));}}}
void immutable_snapshot(){auto a=rig();const auto original=a;auto s=source(a);vrm::Retargeter cache(a);cache.update(s,aligned());a.nodes[1].parent=-2;a.skins.clear();a.humanoid.clear();for(unsigned i=0;i<12;++i){animate(s,i);equal(cache.update(s,aligned()),vrm::retarget(original,s,aligned()));}}
void invalid_rigs(){for(int kind=0;kind<5;++kind){auto a=rig();const auto s=source(a);if(kind==0)a.nodes[1].parent=99;else if(kind==1)a.nodes[0].parent=5;else if(kind==2)a.nodes[7].skin=99;else if(kind==3)a.skins[0].inverse_bind.pop_back();else a.humanoid["head"]=1;vrm::Retargeter cache(a);equal(cache.update(s,aligned()),vrm::retarget(a,s,aligned()));equal(cache.update(s,aligned()),vrm::retarget(a,s,aligned()));}}
void affine_rigs(){for(float scale:{.01f,.5f,2.f,-1.f}){auto a=rig();a.nodes[0].local[0]=scale;a.nodes[0].local[5]=std::abs(scale)*1.2f;a.nodes[0].local[10]=std::abs(scale)*.8f;worlds(a);auto s=source(a);vrm::Retargeter cache(a);for(unsigned i=0;i<20;++i){animate(s,i);equal(cache.update(s,aligned()),vrm::retarget(a,s,aligned()));}}}
void warm_allocations(){const auto a=rig();auto s=source(a);vrm::Retargeter cache(a);cache.update(s,aligned());allocations=0;count_allocations=true;for(int i=0;i<30;++i)check(bool(cache.update(s,aligned())),"Warm compiled update failed");count_allocations=false;const auto compiled=allocations;allocations=0;count_allocations=true;for(int i=0;i<30;++i)check(bool(vrm::retarget(a,s,aligned())),"One-shot reference failed");count_allocations=false;check(compiled*2<allocations,"Compiled retarget repeats preparation allocations each frame");}
void metadata_only_snapshot(){auto a=rig();a.meshes[0].primitives.resize(1);a.meshes[0].primitives[0].vertices.resize(8192);a.images.resize(1);a.images[0].rgba.resize(1024*1024);allocation_bytes=0;count_allocations=true;vrm::Retargeter cache(a);count_allocations=false;check(allocation_bytes<256*1024,"Compiled rig duplicates avatar geometry or textures");check(bool(cache.update(source(a),aligned())),"Metadata-only rig snapshot lost mesh/skin validation");}
void valid_guard_allocations(){const auto a=rig();const auto s=source(a);allocations=0;count_allocations=true;const auto result=vrm::retarget(a,s,aligned());count_allocations=false;check(bool(result),"Guard allocation fixture failed");check(allocations<250,"Valid matrix guards eagerly allocate diagnostic strings");}
int main(){unsigned failed=0;for(int i=0;i<14;++i)try{if(i==0)dynamic_motion();else if(i==1)rest_changes();else if(i==2)availability_changes();else if(i==3)option_changes();else if(i==4)invalid_dynamic();else if(i==5)invalid_rest_and_options();else if(i==6)unmapped_validation();else if(i==7)constraints();else if(i==8)immutable_snapshot();else if(i==9)invalid_rigs();else if(i==10)affine_rigs();else if(i==11)warm_allocations();else if(i==12)metadata_only_snapshot();else valid_guard_allocations();}catch(const std::exception& e){count_allocations=false;++failed;std::cerr<<"FAIL "<<e.what()<<'\n';}
if(!failed)std::cout<<"PASS 14 retarget-cache scenarios: bit-identical motion/palettes, rest/options/availability invalidation, dynamic/unmapped validation, constraints, immutable metadata, invalid rigs, affine frames, reduced warm allocations and lazy diagnostic construction\n";
return failed?1:0;}
