#include "../src/secondary_hand_contact.hpp"
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <chrono>
using M=vrm::Matrix;using Mode=evrm::SecondaryHandContactMode;
const M I{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void check(bool yes,const char*why){if(!yes)throw std::runtime_error(why);}
void near(double a,double b){check(std::abs(a-b)<2e-5,"contact coordinate differs");}
M at(float x,float y,float z=0){auto m=I;m[12]=x;m[13]=y;m[14]=z;return m;}
double length(const M&a,const M&b){return std::sqrt(std::pow(a[12]-b[12],2)+std::pow(a[13]-b[13],2)+std::pow(a[14]-b[14],2));}
struct Fixture {
 vrm::Avatar avatar;vrm::SourcePose source;vrm::RetargetResult pose;std::array<evrm::ToolHandCorrection,2> hands;
 int add(const std::string&name,int parent,M world){int id=int(avatar.nodes.size());vrm::Node n;n.parent=parent;n.world=n.local=world;n.active=true;avatar.nodes.push_back(n);pose.node_world.push_back(world);if(!name.empty())avatar.humanoid[name]=id;return id;}
 Fixture(bool twist=false){
  add("hips",-1,at(0,1));add("rightHand",0,at(-.2f,1.2f));
  int upper=add("leftUpperArm",0,at(.2f,1.6f));
  int optional=twist?add("",upper,at(.3f,1.5f)):upper;
  int lower=add("leftLowerArm",optional,at(.4f,1.4f));int hand=add("leftHand",lower,at(.2f,1.2f));
  add("leftIndexProximal",hand,at(.16f,1.29f));add("leftLittleProximal",hand,at(.24f,1.29f));
  add("leftIndexDistal",hand,at(.18f,1.34f));add("leftThumbDistal",hand,at(.22f,1.30f));
  int mesh=add("",0,I);avatar.nodes[mesh].mesh=0;avatar.nodes[mesh].skin=0;
  vrm::Skin skin;skin.joints={hand};skin.inverse_bind={I};avatar.skins.push_back(skin);
  vrm::SkinPalette palette;palette.mesh_node=mesh;palette.skin=0;palette.matrices={pose.node_world[hand]};pose.palettes={palette};
  source.current["leftHand"]=at(.3f,1.2f);source.current["leftIndexProximal"]=at(.26f,1.29f);source.current["leftLittleProximal"]=at(.34f,1.29f);
  source.current["leftIndexDistal"]=at(.28f,1.34f);source.current["leftThumbDistal"]=at(.32f,1.30f);
  hands[0].valid=hands[1].valid=true;hands[0].model_delta[12]=.1f;hands[1].model_delta[12]=-.1f;
 }
 int node(const char*n){return avatar.humanoid.at(n);}
 evrm::SecondaryHandContactResult solve(evrm::SecondaryHandContact&s,Mode mode=Mode::TwoHanded,double scale=1,double basis=1){return s.update(source,hands,scale,basis,mode,pose);}
};
void optional_contact_tests(){
 {Fixture f;f.avatar.humanoid.erase("leftIndexProximal");f.avatar.humanoid.erase("leftLittleProximal");evrm::SecondaryHandContact s(f.avatar);check(s.ready(),"missing proximal mappings disabled valid distal contact");check(f.solve(s).applied,"available distal contact declined");near(f.pose.node_world[f.node("leftHand")][12],.4);}
 for(int kind=0;kind<4;++kind){Fixture f;
  if(kind==0){for(const char*name:{"leftIndexProximal","leftLittleProximal","leftIndexDistal","leftThumbDistal"})f.avatar.humanoid.erase(name);}
  if(kind==1){for(const char*name:{"leftIndexProximal","leftLittleProximal","leftIndexDistal","leftThumbDistal"})f.source.current.erase(name);}
  if(kind==2){f.avatar.humanoid.erase("leftThumbDistal");f.source.current.erase("leftLittleProximal");}
  if(kind==3){f.source.current["leftIndexProximal"]=at(8,9);f.source.current["leftLittleProximal"]=at(8,9);}
  evrm::SecondaryHandContact s(f.avatar);check(s.ready(),"optional contact mappings disabled solver");const auto before=f.pose.node_world;
  const auto r=f.solve(s);check(r.applied&&!r.clamped,"shared contact priority or wrist fallback declined");near(r.residual,0);near(f.pose.node_world[f.node("leftHand")][12],.4);
  check(f.pose.node_world[f.node("rightHand")]==before[f.node("rightHand")],"fallback changed primary hand");
 }
 for(int invalid=0;invalid<4;++invalid){Fixture f;
  if(invalid==0)f.avatar.humanoid["leftIndexProximal"]=-1;
  if(invalid==1)f.avatar.humanoid["leftThumbDistal"]=int(f.avatar.nodes.size());
  if(invalid==2)f.avatar.humanoid["leftIndexProximal"]=f.node("rightHand");
  if(invalid==3)f.source.current["leftThumbDistal"][12]=std::numeric_limits<float>::quiet_NaN();
  evrm::SecondaryHandContact s(f.avatar);const auto before=f.pose;
  check(!f.solve(s).applied&&f.pose.node_world==before.node_world&&f.pose.palettes[0].matrices==before.palettes[0].matrices,"invalid mapped/shared contact bypassed validation");
 }
}
void generated_correction_tests(){
 for(int detail=0;detail<3;++detail){Fixture f;
  f.add("head",0,at(0,1.8f));f.add("rightLowerArm",0,at(0,1.2f));
  for(const auto&side:{std::string("right"),std::string("left")}){
   const auto hand=f.node((side+"Hand").c_str());const auto wrist=f.pose.node_world[hand];
   const double q=std::sqrt(.5);const std::array<double,3> y=side=="right"?std::array<double,3>{-1,0,0}:std::array<double,3>{-q,-q,0};const double xz=side=="right"?-1:1;
   for(const auto&entry:{std::pair<const char*,double>{"MiddleProximal",0},{"IndexProximal",-.04},{"LittleProximal",.04}}){
    const auto name=side+entry.first;const double distance=entry.second==0?.1:.09;auto p=at(float(wrist[12]+distance*y[0]),float(wrist[13]+distance*y[1]),float(entry.second*xz));
    if(!f.avatar.humanoid.count(name))f.add(name,hand,p);else{const auto n=f.node(name.c_str());f.avatar.nodes[n].world=p;f.pose.node_world[n]=p;}
   }
  }
  f.source.rest.clear();f.source.current.clear();for(const auto&entry:f.avatar.humanoid){f.source.rest[entry.first]=f.avatar.nodes[entry.second].world;f.source.current[entry.first]=f.avatar.nodes[entry.second].world;if(entry.first.rfind("left",0)==0)f.source.current[entry.first][12]+=.2f;}
  if(detail==1){f.avatar.humanoid.erase("leftMiddleProximal");f.avatar.humanoid.erase("rightMiddleProximal");}
  if(detail==2){for(const auto&side:{std::string("right"),std::string("left")})for(const char*suffix:{"MiddleProximal","IndexProximal","LittleProximal","IndexDistal","ThumbDistal"})f.avatar.humanoid.erase(side+suffix);}
  f.hands=evrm::make_tool_hand_corrections(f.avatar,f.source,f.pose,1,1);check(f.hands[0].valid&&f.hands[1].valid,"generated optional grip invalid");const auto primary=f.hands[0];const auto before=f.pose.node_world;evrm::SecondaryHandContact s(f.avatar);
  const auto r=f.solve(s);check(r.applied&&!r.clamped,"generated grip-to-contact integration failed");near(r.residual,0);near(f.pose.node_world[f.node("leftHand")][12],.4);check(f.pose.node_world[f.node("rightHand")]==before[f.node("rightHand")],"generated contact moved primary");
  const auto final=evrm::make_tool_hand_corrections(f.avatar,f.source,f.pose,1,1);check(final[0].valid&&final[1].valid,"regenerated grip invalid");
  for(int i=0;i<16;++i){near(final[0].model_delta[i],primary.model_delta[i]);near(final[0].target_native_hand[i],primary.target_native_hand[i]);}
  for(int i=0;i<3;++i)near(final[0].item_offset_native[i],primary.item_offset_native[i]);
 }
}
int main(){try{
 optional_contact_tests();generated_correction_tests();
 check(evrm::classify_secondary_hand_contact({4,3},{0,0})==Mode::TwoHanded,"tool state missing");
 check(evrm::classify_secondary_hand_contact({4,1},{0,0})==Mode::TwoHanded,"tool after bow retained off-hand state and lost contact");
 for(unsigned offhand=0;offhand<256;++offhand){
  check(evrm::classify_secondary_hand_contact({4,std::uint8_t(offhand)},{0,0})==Mode::TwoHanded,"unused off-hand state changed two-handed activation");
  check(evrm::classify_secondary_hand_contact({5,std::uint8_t(offhand)},{2,0})==Mode::BowDrawn,"unused off-hand state changed bow draw activation");
 }
 check(evrm::classify_secondary_hand_contact({5,1},{2,0})==Mode::BowDrawn,"draw state missing");
 for(auto equipment:{std::array<std::uint8_t,2>{1,3},{7,3},{5,1},{6,3},{0,0}})check(evrm::classify_secondary_hand_contact(equipment,{0,0})==Mode::None,"rest/unknown state enabled");
 check(evrm::classify_secondary_hand_contact({5,1},{2,1})==Mode::None,"unknown ranged state enabled");
 evrm::SecondaryContactActivation gate;
 check(gate.observe(Mode::BowDrawn,90,100,1,1)==Mode::None,"state applied to older GPU pose");
 check(gate.observe(Mode::BowDrawn,100,110,1,1)==Mode::BowDrawn,"stable state never enabled");
 check(gate.observe(Mode::None,110,120,1,1)==Mode::None,"release retained correction");
 check(gate.observe(Mode::BowDrawn,115,125,1,1)==Mode::None,"redraw reused old activation");
 check(gate.observe(Mode::BowDrawn,125,130,2,1)==Mode::None,"owner switch reused activation");
 check(gate.observe(Mode::BowDrawn,130,135,2,2)==Mode::None,"epoch switch reused activation");
 for(bool twist:{false,true}){
  Fixture f(twist);evrm::SecondaryHandContact solver(f.avatar);check(solver.ready(),"solver not ready");
  const auto before=f.pose.node_world;int u=f.node("leftUpperArm"),e=f.node("leftLowerArm"),w=f.node("leftHand");
  const double l1=length(before[u],before[e]),l2=length(before[e],before[w]);
  auto result=f.solve(solver);check(result.applied&&!result.clamped,"reachable target declined");near(result.residual,0);
  near(f.pose.node_world[w][12],.4);near(f.pose.node_world[w][13],1.2);near(f.pose.node_world[w][14],0);
  near(length(f.pose.node_world[u],f.pose.node_world[e]),l1);near(length(f.pose.node_world[e],f.pose.node_world[w]),l2);
  check(f.pose.node_world[0]==before[0]&&f.pose.node_world[1]==before[1]&&f.pose.node_world.back()==before.back(),"primary/body/mesh changed");
  near(f.pose.palettes[0].matrices[0][12],.4);near(f.pose.palettes[0].matrices[0][13],1.2);
 }
 {Fixture f;evrm::SecondaryHandContact s(f.avatar);auto before=f.pose;check(!f.solve(s,Mode::None).applied&&f.pose.node_world==before.node_world&&f.pose.palettes[0].matrices==before.palettes[0].matrices,"inactive pose changed");}
 {Fixture f;f.hands[0].model_delta=at(1,1);evrm::SecondaryHandContact s(f.avatar);auto r=f.solve(s,Mode::BowDrawn);check(r.applied&&r.clamped&&r.residual>.1,"unreachable target not clamped");near(length(f.pose.node_world[2],f.pose.node_world[3]),std::sqrt(.08));near(length(f.pose.node_world[3],f.pose.node_world[4]),std::sqrt(.08));}
 {Fixture f;auto&d=f.hands[0].model_delta;d=at(1,1.3f);d[0]=0;d[1]=1;d[4]=-1;d[5]=0;evrm::SecondaryHandContact s(f.avatar);auto r=f.solve(s);check(r.applied&&!r.clamped,"rotated contact failed");auto&w=f.pose.node_world[f.node("leftHand")];near(w[12],-.2);near(w[13],1.6);near(w[0],0);near(w[1],1);near(w[4],-1);}
 {Fixture f;for(auto&item:f.source.current){item.second[12]*=-2;item.second[13]*=2;item.second[14]*=-2;}f.hands[0].model_delta=at(-.2f,0);f.hands[1].model_delta=at(.2f,0);evrm::SecondaryHandContact s(f.avatar);auto r=f.solve(s,Mode::TwoHanded,2,-1);check(r.applied&&!r.clamped,"VRM0 conversion failed");near(f.pose.node_world[f.node("leftHand")][12],.4);near(r.residual,0);}
 {Fixture f;f.source.current.erase("leftThumbDistal");evrm::SecondaryHandContact s(f.avatar);check(f.solve(s).applied,"proximal contact fallback failed");near(f.pose.node_world[f.node("leftHand")][12],.4);}
 {Fixture f;f.hands[0].item_offset_valid=true;f.hands[0].item_offset_native={.02f,0,0};evrm::SecondaryHandContact s(f.avatar);auto r=f.solve(s);check(r.applied&&!r.clamped,"translated primary item contact declined");near(f.pose.node_world[f.node("leftHand")][12],.42);near(r.residual,0);}
 {Fixture f;f.hands[0].item_offset_native[0]=std::numeric_limits<float>::quiet_NaN();evrm::SecondaryHandContact s(f.avatar);check(f.solve(s).applied,"disabled optional contact data rejected baseline");const auto before=f.pose;f.hands[0].item_offset_valid=true;check(!f.solve(s).applied&&f.pose.node_world==before.node_world&&f.pose.palettes[0].matrices==before.palettes[0].matrices,"invalid primary item offset failed atomic rejection");}
 {Fixture f;for(auto&item:f.source.current){item.second[12]*=-2;item.second[13]*=2;item.second[14]*=-2;}f.hands[0].model_delta=at(-.2f,0);f.hands[1].model_delta=at(.2f,0);f.hands[0].item_offset_valid=true;f.hands[0].item_offset_native={-.04f,0,0};evrm::SecondaryHandContact s(f.avatar);auto r=f.solve(s,Mode::TwoHanded,2,-1);check(r.applied&&!r.clamped,"translated item basis conversion declined");near(f.pose.node_world[f.node("leftHand")][12],.42);near(r.residual,0);}
 {Fixture f;auto&mesh=f.pose.node_world.back();mesh=at(3,4,5);mesh[0]=0;mesh[1]=2;mesh[4]=-3;mesh[5]=0;mesh[10]=4;f.avatar.skins[0].inverse_bind[0]=at(-.2f,-1.2f);evrm::SecondaryHandContact s(f.avatar);check(f.solve(s).applied,"nonidentity mesh/bind declined");const auto&p=f.pose.palettes[0].matrices[0];near(p[0],0);near(p[1],-1.0/3);near(p[4],.5);near(p[12],-2);near(p[13],2.8/3);near(p[14],-1.25);}
 {Fixture f;f.pose.node_world[f.node("leftLowerArm")]=f.pose.node_world[f.node("leftUpperArm")];evrm::SecondaryHandContact s(f.avatar);auto before=f.pose;check(!f.solve(s).applied&&f.pose.node_world==before.node_world,"zero length arm accepted or changed");}
 {Fixture f;f.avatar.skins[0].joints.resize(8193,f.node("leftHand"));f.avatar.skins[0].inverse_bind.resize(8193,I);f.pose.palettes[0].matrices.resize(8193,I);evrm::SecondaryHandContact s(f.avatar);auto before=f.pose;check(!f.solve(s).applied&&f.pose.node_world==before.node_world&&f.pose.palettes[0].matrices==before.palettes[0].matrices,"palette work limit failed atomically");}
 for(int invalid=0;invalid<7;++invalid){Fixture f;if(invalid==0)f.avatar.nodes[2].parent=3;if(invalid==1)f.avatar.humanoid.erase("leftLowerArm");if(invalid==2)f.avatar.nodes[1].parent=2;if(invalid==3)f.avatar.constraints.push_back({vrm::NodeConstraintType::Rotation,3,2,vrm::ConstraintAxis::X,1});evrm::SecondaryHandContact s(f.avatar);if(invalid==4)f.pose.node_world.back()={};if(invalid==5)f.hands[0].model_delta[0]=std::numeric_limits<float>::quiet_NaN();if(invalid==6)f.hands[1].valid=false;auto before=f.pose;check(!f.solve(s).applied,"invalid contact input accepted");check(f.pose.node_world==before.node_world&&f.pose.palettes[0].matrices==before.palettes[0].matrices,"failed solve changed pose");}
 {Fixture f;evrm::SecondaryHandContact s(f.avatar);const auto base=f.pose;const auto started=std::chrono::steady_clock::now();for(int n=0;n<2000;++n){f.pose=base;check(f.solve(s,n%2?Mode::TwoHanded:Mode::BowDrawn).applied,"repeated fresh-pose solve failed");}const auto ms=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count()/2000;std::cout<<"Synthetic small-rig update ms="<<ms<<" (not a live FPS measurement)\n";}
 std::cout<<"Secondary contact: state/age gates, shared contacts, rigid lengths, palettes, basis, clamping and atomic rejection passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
