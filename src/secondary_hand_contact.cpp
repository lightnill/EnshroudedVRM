#include "secondary_hand_contact.hpp"
#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>
namespace evrm { namespace {
using V=std::array<double,3>;using M=std::array<double,16>;using R=std::array<V,3>;
const M identity{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};
void need(bool yes){if(!yes)throw std::runtime_error("Invalid secondary contact");}
V add(V a,const V&b){for(int i=0;i<3;++i)a[i]+=b[i];return a;}
V sub(V a,const V&b){for(int i=0;i<3;++i)a[i]-=b[i];return a;}
V scale(V a,double s){for(auto&x:a)x*=s;return a;}
double dot(const V&a,const V&b){return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
double norm(const V&a){return std::sqrt(dot(a,a));}
V unit(const V&a){const auto n=norm(a);need(std::isfinite(n)&&n>1e-8);return scale(a,1/n);}
V cross(const V&a,const V&b){return {a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]};}
V position(const M&m){return {m[12],m[13],m[14]};}
M matrix(const vrm::Matrix&in){M m{};for(int i=0;i<16;++i){need(std::isfinite(in[i]));m[i]=in[i];}need(std::abs(m[3])+std::abs(m[7])+std::abs(m[11])+std::abs(m[15]-1)<1e-5);return m;}
vrm::Matrix output(const M&m){vrm::Matrix r{};for(int i=0;i<16;++i){need(std::isfinite(m[i])&&std::abs(m[i])<std::numeric_limits<float>::max());r[i]=float(m[i]);}return r;}
R transpose(const R&a){R b{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)b[i][j]=a[j][i];return b;}
R mul(const R&a,const R&b){R c{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)for(int k=0;k<3;++k)c[i][j]+=a[i][k]*b[k][j];return c;}
V mul(const R&a,const V&b){return {dot(a[0],b),dot(a[1],b),dot(a[2],b)};}
M mul(const M&a,const M&b){M c{};for(int i=0;i<4;++i)for(int j=0;j<4;++j)for(int k=0;k<4;++k)c[j*4+i]+=a[k*4+i]*b[j*4+k];return c;}
R rotation(const M&m,bool rigid=false){R r{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)r[i][j]=m[j*4+i];
 if(rigid){const auto product=mul(r,transpose(r));for(int i=0;i<3;++i)for(int j=0;j<3;++j)need(std::abs(product[i][j]-(i==j?1:0))<2e-4);need(dot(r[0],cross(r[1],r[2]))>.999);return r;}
 for(int n=0;n<40;++n){const R cof{cross(r[1],r[2]),cross(r[2],r[0]),cross(r[0],r[1])};const auto det=dot(r[0],cof[0]);need(std::isfinite(det)&&det>1e-10);double change=0;
  for(int i=0;i<3;++i)for(int j=0;j<3;++j){const auto x=.5*(r[i][j]+cof[i][j]/det);change=std::max(change,std::abs(x-r[i][j]));r[i][j]=x;}
  if(change<1e-9)return r;
 }need(false);return {};}
M inverse(const M&m){const R rows{V{m[0],m[4],m[8]},V{m[1],m[5],m[9]},V{m[2],m[6],m[10]}};
 const R cof{cross(rows[1],rows[2]),cross(rows[2],rows[0]),cross(rows[0],rows[1])};const auto det=dot(rows[0],cof[0]);const auto magnitude=norm(rows[0])*norm(rows[1])*norm(rows[2]);need(std::isfinite(det)&&magnitude>0&&std::abs(det)>magnitude*1e-8);
 M r=identity;for(int i=0;i<3;++i)for(int j=0;j<3;++j)r[j*4+i]=cof[j][i]/det;V p{};for(int i=0;i<3;++i)for(int j=0;j<3;++j)p[i]-=r[j*4+i]*m[12+j];for(int i=0;i<3;++i)r[12+i]=p[i];return r;}
R swing(V from,V to){from=unit(from);to=unit(to);const auto c=std::clamp(dot(from,to),-1.0,1.0);auto v=cross(from,to);double w=1+c;
 if(c<-.99999999){v=unit(cross(from,std::abs(from[0])<.8?V{1,0,0}:V{0,1,0}));w=0;}else {const auto n=std::sqrt(dot(v,v)+w*w);need(n>1e-8);v=scale(v,1/n);w/=n;}
 const auto x=v[0],y=v[1],z=v[2];return {V{1-2*(y*y+z*z),2*(x*y-z*w),2*(x*z+y*w)},V{2*(x*y+z*w),1-2*(x*x+z*z),2*(y*z-x*w)},V{2*(x*z-y*w),2*(y*z+x*w),1-2*(x*x+y*y)}};}
void rotate(M&m,const R&r,const V&pivot){for(int j=0;j<3;++j){const auto v=mul(r,V{m[j*4],m[j*4+1],m[j*4+2]});for(int i=0;i<3;++i)m[j*4+i]=v[i];}const auto p=add(pivot,mul(r,sub(position(m),pivot)));for(int i=0;i<3;++i)m[12+i]=p[i];}
V point(const M&m,const V&p){V r=position(m);for(int i=0;i<3;++i)for(int j=0;j<3;++j)r[i]+=m[j*4+i]*p[j];return r;}
}
struct SecondaryHandContact::Impl {
 const vrm::Avatar&avatar;int upper=-1,lower=-1,hand=-1;
 std::array<int,2> distal{-1,-1},proximal{-1,-1};
 std::vector<std::size_t> upper_nodes,lower_nodes,hand_nodes;
 explicit Impl(const vrm::Avatar&a):avatar(a){compile();}
 bool valid(int n)const{return n>=0&&std::size_t(n)<avatar.nodes.size();}
 int named(const char*name)const{const auto it=avatar.humanoid.find(name);return it==avatar.humanoid.end()?-1:it->second;}
 bool ancestor(int a,int b)const{for(;b>=0;b=avatar.nodes[b].parent)if(a==b)return true;return false;}
 int optional_contact(const char*name)const{const auto it=avatar.humanoid.find(name);if(it==avatar.humanoid.end())return -1;const auto n=it->second;need(valid(n)&&n!=hand&&ancestor(hand,n));return n;}
 void compile(){
  need(!avatar.nodes.empty()&&avatar.nodes.size()<=2048);
  for(const auto&n:avatar.nodes)need(n.parent==-1||valid(n.parent));
  for(std::size_t n=0;n<avatar.nodes.size();++n){int at=int(n);std::size_t depth=0;for(;at>=0&&depth<=avatar.nodes.size();++depth)at=avatar.nodes[at].parent;need(depth<=avatar.nodes.size());}
  upper=named("leftUpperArm");lower=named("leftLowerArm");hand=named("leftHand");const auto primary=named("rightHand");
  need(valid(upper)&&valid(lower)&&valid(hand)&&valid(primary)&&upper!=lower&&lower!=hand&&upper!=hand&&ancestor(upper,lower)&&ancestor(lower,hand)&&!ancestor(upper,primary));
  for(const auto&c:avatar.constraints){need(valid(c.target)&&valid(c.source));need(!ancestor(upper,c.target)&&!ancestor(upper,c.source));}
  proximal={optional_contact("leftIndexProximal"),optional_contact("leftLittleProximal")};distal={optional_contact("leftIndexDistal"),optional_contact("leftThumbDistal")};
  for(std::size_t n=0;n<avatar.nodes.size();++n){if(ancestor(upper,int(n)))upper_nodes.push_back(n);if(ancestor(lower,int(n)))lower_nodes.push_back(n);if(ancestor(hand,int(n)))hand_nodes.push_back(n);}
 }
 SecondaryHandContactResult update(const vrm::SourcePose&source,const std::array<ToolHandCorrection,2>&hands,double height,double basis,SecondaryHandContactMode mode,vrm::RetargetResult&pose){
  if(mode==SecondaryHandContactMode::None)return {};
  need(mode==SecondaryHandContactMode::TwoHanded||mode==SecondaryHandContactMode::BowDrawn);
  need(pose&&pose.node_world.size()==avatar.nodes.size()&&pose.palettes.size()<=avatar.nodes.size()&&hands[0].valid&&hands[1].valid&&std::isfinite(height)&&height>0&&height<=100&&(basis==1||basis==-1));
  auto primary=matrix(hands[0].model_delta);const auto secondary=matrix(hands[1].model_delta);
  if(hands[0].item_offset_valid)for(int i=0;i<3;++i){need(std::isfinite(hands[0].item_offset_native[i]));primary[12+i]+=hands[0].item_offset_native[i];need(std::isfinite(primary[12+i]));}
  const auto rp=rotation(primary,true),rs=rotation(secondary,true);
  const R b{V{basis,0,0},V{0,1,0},V{0,0,basis}};const auto orientation_delta=mul(mul(b,mul(rp,transpose(rs))),b);
  // Bounds and shape checks precede copying; no per-frame hierarchy scan.
  std::size_t work=0;for(const auto&p:pose.palettes){need(valid(p.mesh_node)&&p.skin>=0&&std::size_t(p.skin)<avatar.skins.size());const auto&s=avatar.skins[p.skin];need(s.joints.size()==s.inverse_bind.size()&&p.matrices.size()==s.joints.size());work+=s.joints.size();need(work<=8192);for(auto n:s.joints)need(valid(n));}
  std::vector<M> world;world.reserve(pose.node_world.size());for(const auto&m:pose.node_world)world.push_back(matrix(m));
  // Prefer shared fingertip contact, then shared palm roots, then the wrist.
  // Supplied bad data is never treated as an absent optional landmark.
  for(const char*name:{"leftIndexDistal","leftThumbDistal","leftIndexProximal","leftLittleProximal"}){const auto it=source.current.find(name);if(it!=source.current.end())matrix(it->second);}
  const char* names[2]={"leftHand",nullptr};std::array<int,2> contact_nodes{hand,-1};int contact_count=1;
  if(valid(distal[0])&&valid(distal[1])&&source.current.count("leftIndexDistal")&&source.current.count("leftThumbDistal")){names[0]="leftIndexDistal";names[1]="leftThumbDistal";contact_nodes=distal;contact_count=2;}
  else if(valid(proximal[0])&&valid(proximal[1])&&source.current.count("leftIndexProximal")&&source.current.count("leftLittleProximal")){names[0]="leftIndexProximal";names[1]="leftLittleProximal";contact_nodes=proximal;contact_count=2;}
  V source_contact{},target_contact{};for(int i=0;i<contact_count;++i){const auto it=source.current.find(names[i]);need(it!=source.current.end());source_contact=add(source_contact,scale(position(matrix(it->second)),1.0/contact_count));target_contact=add(target_contact,scale(position(world[contact_nodes[i]]),1.0/contact_count));}
  const auto desired_contact=scale(mul(b,point(primary,source_contact)),1/height);
  const auto shoulder=position(world[upper]),elbow=position(world[lower]),wrist=position(world[hand]);
  const auto desired_rotation=mul(orientation_delta,rotation(world[hand]));
  const auto desired_wrist=sub(desired_contact,mul(orientation_delta,sub(target_contact,wrist)));
  const auto l1=norm(sub(elbow,shoulder)),l2=norm(sub(wrist,elbow));need(l1>1e-6&&l2>1e-6&&std::isfinite(l1+l2));
  auto direction=sub(desired_wrist,shoulder);const auto distance=norm(direction);need(std::isfinite(distance));
  if(distance<1e-8)direction=unit(sub(wrist,shoulder));else direction=scale(direction,1/distance);
  const auto epsilon=std::min(l1,l2)*1e-5;const auto reach=std::clamp(distance,std::abs(l1-l2)+epsilon,l1+l2-epsilon);
  auto bend=sub(sub(elbow,shoulder),scale(direction,dot(sub(elbow,shoulder),direction)));
  if(norm(bend)<1e-7){const auto r=rotation(world[upper]);for(int column:{2,1,0}){V axis{r[0][column],r[1][column],r[2][column]};bend=sub(axis,scale(direction,dot(axis,direction)));if(norm(bend)>1e-7)break;}}
  bend=unit(bend);const auto along=(l1*l1-l2*l2+reach*reach)/(2*reach);const auto off=std::sqrt(std::max(0.0,l1*l1-along*along));
  const auto new_elbow=add(shoulder,add(scale(direction,along),scale(bend,off)));const auto new_wrist=add(shoulder,scale(direction,reach));
  auto r=swing(sub(elbow,shoulder),sub(new_elbow,shoulder));for(auto n:upper_nodes)rotate(world[n],r,shoulder);
  r=swing(sub(position(world[hand]),position(world[lower])),sub(new_wrist,position(world[lower])));const auto lower_pivot=position(world[lower]);for(auto n:lower_nodes)rotate(world[n],r,lower_pivot);
  r=mul(desired_rotation,transpose(rotation(world[hand])));const auto hand_pivot=position(world[hand]);for(auto n:hand_nodes)rotate(world[n],r,hand_pivot);
  auto revised=pose;for(auto n:upper_nodes)revised.node_world[n]=output(world[n]);
  for(auto&p:revised.palettes){const auto inv_mesh=inverse(world[p.mesh_node]);const auto&s=avatar.skins[p.skin];for(std::size_t j=0;j<s.joints.size();++j)p.matrices[j]=output(mul(mul(inv_mesh,world[s.joints[j]]),matrix(s.inverse_bind[j])));}
  V final_contact{};for(int i=0;i<contact_count;++i)final_contact=add(final_contact,scale(position(world[contact_nodes[i]]),1.0/contact_count));
  SecondaryHandContactResult result{true,std::abs(reach-distance)>epsilon*.5,norm(sub(desired_contact,final_contact))};
  need(std::isfinite(result.residual));pose=std::move(revised);return result;
 }
};
SecondaryHandContact::SecondaryHandContact(const vrm::Avatar&a)noexcept{try{impl=std::make_unique<Impl>(a);}catch(...){}}
SecondaryHandContact::~SecondaryHandContact()=default;
bool SecondaryHandContact::ready()const noexcept{return bool(impl);}
SecondaryHandContactResult SecondaryHandContact::update(const vrm::SourcePose&s,const std::array<ToolHandCorrection,2>&h,double scale,double basis,SecondaryHandContactMode mode,vrm::RetargetResult&p)noexcept{try{return impl?impl->update(s,h,scale,basis,mode,p):SecondaryHandContactResult{};}catch(...){return {};}}
}
