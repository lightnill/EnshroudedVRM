#include "../src/tool_grip.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
using M=vrm::Matrix;
M I{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void check(bool x,const char* s){if(!x)throw std::runtime_error(s);}
void near(double a,double b){check(std::abs(a-b)<1e-6,"coordinate differs");}
M point(double x,double y,double z){auto m=I;m[12]=x;m[13]=y;m[14]=z;return m;}
M product(const M&a,const M&b){M out{};for(int i=0;i<4;++i)for(int j=0;j<4;++j)for(int k=0;k<4;++k)out[j*4+i]+=a[k*4+i]*b[j*4+k];return out;}
std::array<double,3> transform(const M&m,std::array<double,3> p){return {m[0]*p[0]+m[4]*p[1]+m[8]*p[2]+m[12],m[1]*p[0]+m[5]*p[1]+m[9]*p[2]+m[13],m[2]*p[0]+m[6]*p[1]+m[10]*p[2]+m[14]};}
struct Fixture{
 vrm::Avatar avatar;vrm::SourcePose source;vrm::RetargetResult pose;
 void add(const std::string&name,M world){avatar.humanoid[name]=int(avatar.nodes.size());avatar.nodes.emplace_back();avatar.nodes.back().world=world;pose.node_world.push_back(world);}
 Fixture(){for(const auto&side:{std::string("right"),std::string("left")}){
  for(const auto&entry:std::array<std::pair<std::string,M>,4>{{{"Hand",point(0,1,0)},{"MiddleProximal",point(0,1.1,0)},{"IndexProximal",point(-.04,1.09,0)},{"LittleProximal",point(.04,1.09,0)}}}){
   auto name=side+entry.first;avatar.humanoid[name]=int(avatar.nodes.size());avatar.nodes.emplace_back();avatar.nodes.back().world=entry.second;
   source.rest[name]=entry.second;source.current[name]=entry.second;pose.node_world.push_back(entry.second);
  }
 }}
};
void optional_landmark_tests(){
 // Both T-pose hands point outward with palms down. Index->little is -Z
 // on both hands, so the preferred right frame's normal points body-up.
 // A wrist fallback must preserve that bilateral frame convention.
 Fixture anatomy;anatomy.add("hips",point(0,1,0));anatomy.add("head",point(0,1.8,0));
 for(const auto&side:{std::string("right"),std::string("left")}){
  const double sign=side=="right"?-1:1;const auto wrist=point(sign*.8,1.5,0);const int hand=anatomy.avatar.humanoid.at(side+"Hand");anatomy.avatar.nodes[hand].world=anatomy.pose.node_world[hand]=wrist;anatomy.add(side+"LowerArm",point(sign*.5,1.5,0));
  for(const auto&entry:{std::pair<const char*,double>{"MiddleProximal",0},{"IndexProximal",.04},{"LittleProximal",-.04}}){const auto n=anatomy.avatar.humanoid.at(side+entry.first);const auto m=point(wrist[12]+sign*(entry.second==0?.1:.09),1.5,entry.second);anatomy.avatar.nodes[n].world=anatomy.pose.node_world[n]=m;}
 }
 const auto measured_anatomy=evrm::make_tool_hand_corrections(anatomy.avatar,anatomy.source,anatomy.pose,1,1);
 for(const auto&side:{std::string("right"),std::string("left")})for(const char*suffix:{"MiddleProximal","IndexProximal","LittleProximal"})anatomy.avatar.humanoid.erase(side+suffix);
 const auto wrist_anatomy=evrm::make_tool_hand_corrections(anatomy.avatar,anatomy.source,anatomy.pose,1,1);
 for(int h=0;h<2;++h){check(measured_anatomy[h].valid&&wrist_anatomy[h].valid,"bilateral anatomical fixture invalid");for(int i=0;i<16;++i)check(std::abs(measured_anatomy[h].model_delta[i]-wrist_anatomy[h].model_delta[i])<1e-6,"fingerless T-pose flipped preferred anatomical hand orientation");}
 Fixture partial;const auto preferred=evrm::make_tool_hand_corrections(partial.avatar,partial.source,partial.pose,1,1);
 partial.avatar.humanoid.erase("rightMiddleProximal");
 auto c=evrm::make_tool_hand_corrections(partial.avatar,partial.source,partial.pose,1,1);
 check(c[0].valid&&c[0].item_offset_valid&&c[1].valid,"missing middle mapping disabled a measurable palm");
 for(int i=0;i<16;++i)near(c[0].model_delta[i],preferred[0].model_delta[i]);
 for(int i=0;i<3;++i)near(c[0].item_offset_native[i],preferred[0].item_offset_native[i]);
 Fixture wrist;
 for(const auto&side:{std::string("right"),std::string("left")}){
  for(const char*suffix:{"MiddleProximal","IndexProximal","LittleProximal"})wrist.avatar.humanoid.erase(side+suffix);
  const double sign=side=="right"?-1:1;const auto hand=wrist.avatar.humanoid.at(side+"Hand");
  auto rest=point(sign*.8,1.5,.2);rest[0]=0;rest[1]=1;rest[4]=-1;rest[5]=0;
  wrist.avatar.nodes[hand].world=rest;wrist.pose.node_world[hand]=rest;wrist.pose.node_world[hand][12]+=float(.1);wrist.pose.node_world[hand][13]+=float(.1);wrist.pose.node_world[hand][14]+=float(.1);
  wrist.add(side+"LowerArm",point(sign*.5,1.5,.2));
 }
 wrist.add("hips",point(0,1,0));wrist.add("head",point(0,1.8,0));
 c=evrm::make_tool_hand_corrections(wrist.avatar,wrist.source,wrist.pose,1,1);
 check(c[0].valid&&c[1].valid&&!c[0].item_offset_valid&&!c[1].item_offset_valid,"fingerless valid T-pose declined or invented palm calibration");
 auto tip=transform(c[0].model_delta,{0,1.5,0});near(tip[0],-1.2);near(tip[1],1.6);near(tip[2],.3);
 const auto absolute=c[0].target_native_hand;
 auto phase=point(.1,.8,-.1);phase[0]=0;phase[1]=1;phase[4]=-1;phase[5]=0;wrist.source.current["rightHand"]=phase;
 auto fresh=evrm::make_tool_hand_corrections(wrist.avatar,wrist.source,wrist.pose,1,1);
 check(fresh[0].valid,"fingerless fresh source phase declined");for(int i=0;i<16;++i)near(fresh[0].target_native_hand[i],absolute[i]);
 wrist.source.current["rightHand"]=wrist.source.rest["rightHand"];
 auto body=point(2,3,.4);body[0]=0;body[1]=1;body[4]=-1;body[5]=0;
 for(auto&n:wrist.avatar.nodes)n.world=product(body,n.world);
 for(auto&m:wrist.pose.node_world)m=product(body,m);
 fresh=evrm::make_tool_hand_corrections(wrist.avatar,wrist.source,wrist.pose,2,-1);
 check(fresh[0].valid,"rotated fingerless body declined");const auto expected=transform(body,tip);auto actual=transform(fresh[0].model_delta,{0,1.5,0});
 const auto base_wrist=transform(body,{- .7,1.6,.3});
 // Height scales the wrist only; a native tool's half-unit length stays rigid.
 near(actual[0],-2*base_wrist[0]-(expected[0]-base_wrist[0]));near(actual[1],2*base_wrist[1]+expected[1]-base_wrist[1]);near(actual[2],-2*base_wrist[2]-(expected[2]-base_wrist[2]));
 for(int bad=0;bad<5;++bad){auto invalid=wrist;
  if(bad==0)invalid.avatar.humanoid["rightIndexProximal"]=-1;
  if(bad==1)invalid.avatar.humanoid["rightIndexProximal"]=int(invalid.avatar.nodes.size());
  if(bad==2){invalid.avatar.humanoid["rightMiddleProximal"]=1;invalid.avatar.nodes[1].world[12]=std::numeric_limits<float>::quiet_NaN();}
  if(bad==3)invalid.avatar.nodes[invalid.avatar.humanoid.at("rightLowerArm")].world=invalid.avatar.nodes[0].world;
  if(bad==4)invalid.avatar.humanoid.erase("rightLowerArm");
  const auto rejected=evrm::make_tool_hand_corrections(invalid.avatar,invalid.source,invalid.pose,1,1);
  check(!rejected[0].valid&&rejected[1].valid,"invalid supplied fallback data accepted or other hand lost");
 }
 Fixture corrupt;corrupt.avatar.nodes[3].world=corrupt.avatar.nodes[2].world;
 check(!evrm::make_tool_hand_corrections(corrupt.avatar,corrupt.source,corrupt.pose,1,1)[0].valid,"degenerate supplied palm downgraded silently");
 // A nonidentity bind and symmetric affine stretch have a known polar rotation.
 Fixture affine;auto rest=point(0,1,0);rest[0]=-.2f;rest[1]=2;rest[4]=-3;rest[5]=.2f;rest[10]=4;
 affine.avatar.nodes[0].world=rest;auto current=point(.3,1.2,.1);current[0]=4;current[1]=.3f;current[4]=.3f;current[5]=2;current[10]=3;affine.pose.node_world[0]=current;
 const auto calibrated=evrm::make_tool_hand_corrections(affine.avatar,affine.source,affine.pose,1,1);check(calibrated[0].valid&&calibrated[0].item_offset_valid,"combined bind axes and shear declined");
 // Independently invert the 2x2 authored block and apply the current block to the measured root (0,.09).
 const double det=(-.2*.2)-(-3*2),lx=3*.09/det,ly=-.2*.09/det;
 const std::array<double,3> target_offset{4*lx+.3*ly,.3*lx+2*ly,0};
 auto mapped=transform(calibrated[0].model_delta,{0,1.09,0});
 for(int i=0;i<3;++i)near(mapped[i]+calibrated[0].item_offset_native[i],current[12+i]+target_offset[i]);
}
int main(){try{
 optional_landmark_tests();
 Fixture f;f.pose.node_world[0][12]=.2;f.pose.node_world[0][13]=1.15;
 auto c=evrm::make_tool_hand_corrections(f.avatar,f.source,f.pose,1,1);check(c[0].valid&&c[1].valid,"valid semantic hands declined");
 auto p=transform(c[0].model_delta,{0,1,0});near(p[0],.2);near(p[1],1.15);near(p[2],0);
 near(c[0].target_native_hand[12],.2);near(c[0].target_native_hand[13],1.15);
 // The absolute target follows the buffered VRM pose, independently of the
 // source hand phase used to build the legacy relative delta.
 const auto target=c[0].target_native_hand;
 auto newer=point(.1,1.04,.03);newer[0]=0;newer[1]=1;newer[4]=-1;newer[5]=0;
 f.source.current["rightHand"]=newer;
 const auto held=evrm::make_tool_hand_corrections(f.avatar,f.source,f.pose,1,1);
 check(held[0].valid,"absolute target declined at different source phase");
 for(unsigned i=0;i<16;++i)near(held[0].target_native_hand[i],target[i]);
 f.source.current["rightHand"]=f.source.rest["rightHand"];
 auto tip=transform(c[0].model_delta,{0,1.5,0});near(std::hypot(tip[0]-p[0],tip[1]-p[1]),.5);
 // Different authored wrist axes must not rotate tools when palm anatomy agrees.
 auto r=point(0,1,0);r[0]=0;r[1]=1;r[4]=-1;r[5]=0;
 f.avatar.nodes[0].world=r;f.pose.node_world[0]=r;
 c=evrm::make_tool_hand_corrections(f.avatar,f.source,f.pose,1,1);check(c[0].valid,"rest axes declined");near(c[0].model_delta[0],1);near(c[0].model_delta[5],1);
 // A current 90-degree wrist turn rotates a native grip offset rigidly.
 f.avatar.nodes[0].world=point(0,1,0);
 c=evrm::make_tool_hand_corrections(f.avatar,f.source,f.pose,1,1);p=transform(c[0].model_delta,{0,1.5,0});near(p[0],-.5);near(p[1],1);
 // VRM0 basis and height affect the target wrist, never object length.
 f.pose.node_world[0]=point(.2,1,.3);
 c=evrm::make_tool_hand_corrections(f.avatar,f.source,f.pose,2,-1);p=transform(c[0].model_delta,{0,1,0});near(p[0],-.4);near(p[1],2);near(p[2],-.6);
 near(c[0].target_native_hand[12],-.4);near(c[0].target_native_hand[13],2);near(c[0].target_native_hand[14],-.6);
 tip=transform(c[0].model_delta,{0,1.5,0});near(std::hypot(tip[0]-p[0],tip[1]-p[1]),.5);
 f.avatar.humanoid.erase("rightLittleProximal");c=evrm::make_tool_hand_corrections(f.avatar,f.source,f.pose,1,1);check(!c[0].valid&&c[1].valid,"missing hand must fail independently");
 Fixture d;d.source.rest["rightLittleProximal"]=d.source.rest["rightIndexProximal"];c=evrm::make_tool_hand_corrections(d.avatar,d.source,d.pose,1,1);check(!c[0].valid,"collinear palm accepted");
 Fixture n;n.pose.node_world[0][12]=std::numeric_limits<double>::quiet_NaN();c=evrm::make_tool_hand_corrections(n.avatar,n.source,n.pose,1,1);check(!c[0].valid,"NaN accepted");
 check(!evrm::make_tool_hand_corrections(f.avatar,f.source,f.pose,0,1)[1].valid,"zero scale accepted");
 check(!evrm::make_tool_hand_corrections(f.avatar,f.source,f.pose,1,0)[1].valid,"invalid basis accepted");
 // Identical hand proportions need no item-only offset. A shorter palm must
 // bring the item back to its own finger roots without moving the wrist.
 Fixture size;for(int i:{1,2,3})size.avatar.nodes[i].world[13]=1+(size.avatar.nodes[i].world[13]-1)*.5f;
 auto sized=evrm::make_tool_hand_corrections(size.avatar,size.source,size.pose,1,1);
 check(sized[0].valid&&sized[0].item_offset_valid,"anatomical item offset missing");near(sized[0].item_offset_native[0],0);near(sized[0].item_offset_native[1],-.045);near(sized[0].item_offset_native[2],0);near(sized[0].target_native_hand[13],1);
 check(sized[1].item_offset_valid,"equal-size hand lost item calibration");for(auto x:sized[1].item_offset_native)near(x,0);
 // Rotation/basis/height rotate and scale anatomical lengths once. Native
 // items remain rigid; target wrist stays at the actual buffered VRM wrist.
 auto turned=point(.2,1.15,.3);turned[0]=0;turned[1]=1;turned[4]=-1;turned[5]=0;size.pose.node_world[0]=turned;
 sized=evrm::make_tool_hand_corrections(size.avatar,size.source,size.pose,3,-1);check(sized[0].item_offset_valid,"turned anatomical offset declined");near(sized[0].item_offset_native[0],.045);near(sized[0].item_offset_native[1],0);near(sized[0].target_native_hand[12],-.6);near(sized[0].target_native_hand[13],3.45);
 const auto calibrated=sized[0];size.source.current["rightHand"]=turned;
 for(const char*suffix:{"MiddleProximal","IndexProximal","LittleProximal"})size.source.current[std::string("right")+suffix]=point(7,8,9);
 sized=evrm::make_tool_hand_corrections(size.avatar,size.source,size.pose,3,-1);check(sized[0].item_offset_valid,"fresh source phase lost anatomical offset");
 for(int i=0;i<3;++i)near(sized[0].item_offset_native[i],calibrated.item_offset_native[i]);
 for(int i=0;i<16;++i)near(sized[0].target_native_hand[i],calibrated.target_native_hand[i]);
 // Native character customization can resize the hand after the profile rest
 // pose. That changes the grip distance even though tools themselves stay rigid.
 Fixture stretched;for(int i:{1,2,3})stretched.avatar.nodes[i].world[13]=1+(stretched.avatar.nodes[i].world[13]-1)*.5f;
 stretched.source.current["rightHand"][5]=.5f;
 auto custom=evrm::make_tool_hand_corrections(stretched.avatar,stretched.source,stretched.pose,1,1);check(custom[0].item_offset_valid,"custom hand offset missing");near(custom[0].item_offset_native[1],0);
 stretched.source.current["rightHand"]=stretched.source.rest["rightHand"];stretched.pose.node_world[0][5]=2;
 custom=evrm::make_tool_hand_corrections(stretched.avatar,stretched.source,stretched.pose,1,1);check(custom[0].item_offset_valid,"stretched target hand offset missing");near(custom[0].item_offset_native[1],0);
 std::cout<<"Tool grip: rigid wrist/palm mapping, rest axes, VRM0, scale and invalid data passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
