#include "native_tool_slots.hpp"
#include <algorithm>
#include <cmath>
namespace evrm { namespace {
using R=std::array<std::array<double,3>,3>;
bool proper_rotation(const std::array<float,12>&m,R&r) noexcept {
 for(auto v:m)if(!std::isfinite(v))return false;
 for(int i=0;i<3;++i)for(int j=0;j<3;++j)r[i][j]=m[j*3+i];
 for(int iteration=0;iteration<40;++iteration){
  R cofactor{{{{r[1][1]*r[2][2]-r[1][2]*r[2][1],r[1][2]*r[2][0]-r[1][0]*r[2][2],r[1][0]*r[2][1]-r[1][1]*r[2][0]}},
              {{r[2][1]*r[0][2]-r[2][2]*r[0][1],r[2][2]*r[0][0]-r[2][0]*r[0][2],r[2][0]*r[0][1]-r[2][1]*r[0][0]}},
              {{r[0][1]*r[1][2]-r[0][2]*r[1][1],r[0][2]*r[1][0]-r[0][0]*r[1][2],r[0][0]*r[1][1]-r[0][1]*r[1][0]}}}};
  const auto determinant=r[0][0]*cofactor[0][0]+r[0][1]*cofactor[0][1]+r[0][2]*cofactor[0][2];
  if(!std::isfinite(determinant)||determinant<1e-10)return false;
  double change=0;
  for(int i=0;i<3;++i)for(int j=0;j<3;++j){const auto next=.5*(r[i][j]+cofactor[i][j]/determinant);change=std::max(change,std::abs(next-r[i][j]));r[i][j]=next;}
  if(change<1e-9)return true;
 }return false;
}
}
bool make_native_tool_slot_edits(const NativeVisualSlots& slots,const std::array<float,4>& q,
 const std::array<float,3>& scale,const std::array<ToolHandCorrection,2>& corrections,std::uint64_t sampled,std::uint64_t now,NativeToolSlotEdits&out)noexcept {
 out={};if(!sampled||now<sampled||now-sampled>250)return false;
 double norm=0;for(auto v:q){if(!std::isfinite(v))return false;norm+=double(v)*v;}
 if(std::abs(norm-1)>1e-3)return false;
 for(auto v:scale)if(!std::isfinite(v)||v<.01f||v>100||std::abs(v-scale[0])>1e-5)return false;
 const double x=q[0],y=q[1],z=q[2],w=q[3],s=scale[0];
 const double a[3][3]={{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},
  {2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},
  {2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};
 bool any=false;
 // Pinned native attachment names use the opposite side to the semantic rig
 // profile: HandPalmR14 tracks source leftHand74; HandPalmL15 rightHand44.
 // Slot12 is the independent shield grip attached to the palm15 limb. Keep
 // its native rigid offset instead of moving the avatar's arm or scaling it.
 constexpr std::array<unsigned,NativeToolSlotEdits::slots.size()> palms{14,15,14,15,15};
 for(std::size_t n=0;n<NativeToolSlotEdits::slots.size();++n){const auto slot=NativeToolSlotEdits::slots[n];
  const unsigned palm=palms[n];const auto&c=corrections[palm==14?1:0];
  if(!c.valid||!(slots.active&(std::uint64_t(1)<<slot))||!(slots.active&(std::uint64_t(1)<<palm)))continue;
  auto m=c.target_native_hand;
  if((slot==8||slot==9)&&c.item_offset_valid)for(int i=0;i<3;++i)m[12+i]+=c.item_offset_native[i];
  bool valid=true;for(auto v:m)valid=valid&&std::isfinite(v);
  valid=valid&&std::abs(m[3])<1e-5&&std::abs(m[7])<1e-5&&std::abs(m[11])<1e-5&&std::abs(m[15]-1)<1e-5;
  // Accept only a proper rigid correction, including after publication copying.
  for(int i=0;i<3;++i)for(int j=0;j<3;++j){double d=0;for(int k=0;k<3;++k)d+=double(m[i*4+k])*m[j*4+k];valid=valid&&std::abs(d-(i==j?1:0))<1e-4;}
  const auto det=m[0]*(m[5]*m[10]-m[9]*m[6])-m[4]*(m[1]*m[10]-m[9]*m[2])+m[8]*(m[1]*m[6]-m[5]*m[2]);valid=valid&&det>.999f;
  const auto&src=slots.matrices[slot];for(auto v:src)valid=valid&&std::isfinite(v);
  const auto&anchor=slots.matrices[palm];R current{};
  if(!valid||!proper_rotation(anchor,current))continue;
  double r[3][3]{},t[3]{};
  // Anchor to the VRM's buffered pose, removing this publisher's fresh native
  // wrist motion first. Applying a buffered delta directly leaves phase sway.
  // Proper rotations preserve the original grip's scale/shear and rigid offset.
  for(int i=0;i<3;++i){
   for(int j=0;j<3;++j)for(int k=0;k<3;++k)for(int l=0;l<3;++l)r[i][j]+=a[i][k]*m[l*4+k]*current[j][l];
   for(int k=0;k<3;++k)t[i]+=s*a[i][k]*m[12+k]-r[i][k]*anchor[9+k];
  }
  auto&dst=out.matrices[n];
  for(int col=0;col<4;++col)for(int i=0;i<3;++i){double value=col==3?t[i]:0;for(int k=0;k<3;++k)value+=r[i][k]*src[col*3+k];dst[col*3+i]=float(value);valid=valid&&std::isfinite(dst[col*3+i]);}
  if(valid){out.enabled[n]=true;any=true;}
 }return any;
}
}
