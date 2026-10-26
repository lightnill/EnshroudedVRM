#include "tool_grip.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
namespace evrm { namespace {
using V=std::array<double,3>;
using R=std::array<V,3>; // rows
V cross(const V&a,const V&b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
double dot(const V&a,const V&b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
bool unit(V& v){const auto n=std::sqrt(dot(v,v));if(!std::isfinite(n)||n<1e-7)return false;for(auto&x:v)x/=n;return true;}
R transpose(const R&a){R b{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)b[i][j]=a[j][i];return b;}
R mul(const R&a,const R&b){R c{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)c[i][j]+=a[i][k]*b[k][j];return c;}
V mul(const R&a,const V&b){return {dot(a[0],b),dot(a[1],b),dot(a[2],b)};}
bool finite(const vrm::Matrix&m){for(auto x:m)if(!std::isfinite(x))return false;return std::abs(m[3])<1e-5&&std::abs(m[7])<1e-5&&std::abs(m[11])<1e-5&&std::abs(m[15]-1)<1e-5;}
V position(const vrm::Matrix&m){return {m[12],m[13],m[14]};}
V subtract(const V&a,const V&b){return {a[0]-b[0],a[1]-b[1],a[2]-b[2]};}
bool anatomical_offset(const vrm::Matrix&rest,const vrm::Matrix&current,const V&root,V&out){
 const R rows{V{rest[0],rest[4],rest[8]},V{rest[1],rest[5],rest[9]},V{rest[2],rest[6],rest[10]}};
 const R cof{cross(rows[1],rows[2]),cross(rows[2],rows[0]),cross(rows[0],rows[1])};const auto det=dot(rows[0],cof[0]);
 if(!std::isfinite(det)||det<1e-10)return false;
 const auto from_wrist=subtract(root,position(rest));V local{};
 for(int i=0;i<3;++i)for(int j=0;j<3;++j)local[i]+=cof[j][i]*from_wrist[j]/det;
 for(int i=0;i<3;++i){out[i]=0;for(int j=0;j<3;++j)out[i]+=current[j*4+i]*local[j];if(!std::isfinite(out[i]))return false;}
 return true;
}
bool rotation(const vrm::Matrix&m,R& r){
 if(!finite(m))return false;
 for(int i=0;i<3;++i)for(int j=0;j<3;++j)r[i][j]=m[j*4+i];
 // Newton polar factor removes scale and shear without changing wrist axes.
 for(int n=0;n<40;++n){const auto a=cross(r[1],r[2]),b=cross(r[2],r[0]),c=cross(r[0],r[1]);const auto det=dot(r[0],a);
  if(!std::isfinite(det)||det<1e-10)return false;
  const R invt{a,b,c};double change=0;
  for(int i=0;i<3;++i)for(int j=0;j<3;++j){const auto x=.5*(r[i][j]+invt[i][j]/det);change=std::max(change,std::abs(x-r[i][j]));r[i][j]=x;}
  if(change<1e-9)return true;
 }return false;
}
bool palm(const std::map<std::string,vrm::Matrix>& joints,const std::string&side,R& frame,V& root){
 const char* names[]={"Hand","MiddleProximal","IndexProximal","LittleProximal"};std::array<V,4> p;
 for(int i=0;i<4;++i){const auto it=joints.find(side+names[i]);if(it==joints.end()||!finite(it->second))return false;p[i]=position(it->second);}
 auto y=subtract(p[1],p[0]),x=subtract(p[3],p[2]);if(!unit(y))return false;
 const auto projection=dot(x,y);for(int i=0;i<3;++i)x[i]-=projection*y[i];if(!unit(x))return false;
 auto z=cross(x,y);if(!unit(z))return false;
 for(int i=0;i<3;++i)frame[i]={x[i],y[i],z[i]};
 for(int i=0;i<3;++i)root[i]=.5*p[2][i]+.5*p[3][i];
 return true;
}
// Absence permits a less detailed reference; a supplied invalid mapping does not.
bool target_joint(const vrm::Avatar&avatar,const std::string&name,const vrm::Matrix*&out){
 out=nullptr;const auto it=avatar.humanoid.find(name);if(it==avatar.humanoid.end())return true;
 if(it->second<0||size_t(it->second)>=avatar.nodes.size())return false;
 out=&avatar.nodes[it->second].world;return finite(*out);
}
bool target_palm(const vrm::Avatar&avatar,const std::string&side,const vrm::Matrix&hand,R&frame,V&root,bool&measured){
 const vrm::Matrix *middle=nullptr,*index=nullptr,*little=nullptr;
 if(!target_joint(avatar,side+"MiddleProximal",middle)||!target_joint(avatar,side+"IndexProximal",index)||!target_joint(avatar,side+"LittleProximal",little))return false;
 measured=index&&little;
 if(measured){
  const auto ip=position(*index),lp=position(*little);for(int i=0;i<3;++i)root[i]=.5*ip[i]+.5*lp[i];
  auto y=subtract(middle?position(*middle):root,position(hand)),x=subtract(lp,ip);if(!unit(y))return false;
  const auto projection=dot(x,y);for(int i=0;i<3;++i)x[i]-=projection*y[i];if(!unit(x))return false;
  auto z=cross(x,y);if(!unit(z))return false;
  for(int i=0;i<3;++i)frame[i]={x[i],y[i],z[i]};
  return true;
 }
 const vrm::Matrix *lower=nullptr,*hips=nullptr,*head=nullptr;
 if(!target_joint(avatar,side+"LowerArm",lower)||!target_joint(avatar,"hips",hips)||!target_joint(avatar,"head",head)||!lower||!hips||!head)return false;
 // VRM's rest T-pose has hands along the forearms and palms facing body-down.
 // World-space landmarks avoid assuming any particular authored wrist axes.
 auto y=subtract(position(hand),position(*lower)),down=subtract(position(*hips),position(*head));if(!unit(y)||!unit(down))return false;
 auto x=cross(y,down);if(!unit(x))return false;
 // The measured index->little frame has opposite palm-normal sign on the
 // right hand. Preserve that convention when the landmarks are absent.
 if(side=="right")for(auto&v:x)v=-v;
 auto z=cross(x,y);if(!unit(z))return false;
 for(int i=0;i<3;++i)frame[i]={x[i],y[i],z[i]};
 return true;
}
}
std::array<ToolHandCorrection,2> make_tool_hand_corrections(
 const vrm::Avatar&avatar,const vrm::SourcePose&source,const vrm::RetargetResult&pose,double scale,double basis) noexcept {
 std::array<ToolHandCorrection,2> result{};
 if(!pose||pose.node_world.size()!=avatar.nodes.size()||!std::isfinite(scale)||scale<=0||scale>100||!(basis==1||basis==-1))return result;
 try{for(int hand=0;hand<2;++hand){const std::string side=hand?"left":"right",name=side+"Hand";
  const auto sr=source.rest.find(name),sc=source.current.find(name);
  const auto node=avatar.humanoid.find(name);
  if(sr==source.rest.end()||sc==source.current.end()||node==avatar.humanoid.end()||node->second<0||size_t(node->second)>=avatar.nodes.size())continue;
  R sp{},tp{},srRot{},scRot{},trRot{},tcRot{};V source_root{},target_root{};
  bool measured=false;
  if(!palm(source.rest,side,sp,source_root)||!target_palm(avatar,side,avatar.nodes[node->second].world,tp,target_root,measured)||!rotation(sr->second,srRot)||!rotation(sc->second,scRot)||
     !rotation(avatar.nodes[node->second].world,trRot)||!rotation(pose.node_world[node->second],tcRot))continue;
  const auto source_turn=mul(scRot,transpose(srRot)),target_turn=mul(tcRot,transpose(trRot));
  sp=mul(source_turn,sp);tp=mul(target_turn,tp);
  for(int j=0;j<3;++j){tp[0][j]*=basis;tp[2][j]*=basis;}
  const auto delta=mul(tp,transpose(sp));auto desired=position(pose.node_world[node->second]);desired[0]*=basis*scale;desired[1]*=scale;desired[2]*=basis*scale;
  const auto offset=subtract(desired,mul(delta,position(sc->second)));auto& out=result[hand].model_delta;
  for(int j=0;j<3;++j)for(int i=0;i<3;++i)out[j*4+i]=float(delta[i][j]);
  for(int i=0;i<3;++i)out[12+i]=float(offset[i]);
  auto& absolute=result[hand].target_native_hand;
  const auto native_axes=mul(delta,scRot);
  for(int j=0;j<3;++j)for(int i=0;i<3;++i)absolute[j*4+i]=float(native_axes[i][j]);
  for(int i=0;i<3;++i)absolute[12+i]=float(desired[i]);
  result[hand].valid=finite(out)&&finite(absolute);
  if(!measured)continue;
  // Rest finger roots follow the wrist's affine change, including character
  // customization. Only the item's translation changes; its size stays rigid.
  V source_offset{},target_offset{};
  if(!anatomical_offset(sr->second,sc->second,source_root,source_offset)||
     !anatomical_offset(avatar.nodes[node->second].world,pose.node_world[node->second],target_root,target_offset))continue;
  source_offset=mul(delta,source_offset);
  bool item_valid=result[hand].valid;V item_offset{};
  for(int i=0;i<3;++i){item_offset[i]=target_offset[i]*scale*(i==1?1:basis)-source_offset[i];item_valid=item_valid&&std::isfinite(item_offset[i])&&std::abs(item_offset[i])<=std::numeric_limits<float>::max();}
  if(item_valid){for(int i=0;i<3;++i)result[hand].item_offset_native[i]=float(item_offset[i]);result[hand].item_offset_valid=true;}
 }}catch(...){return {};}
 return result;
}
}
