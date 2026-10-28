#pragma once
#include "native_publication.hpp"
#include <cmath>
namespace evrm {
// Pose delivery is separate from owner authorization. Only a fully checked
// submission may authorize; presenting newer samples cannot renew its expiry.
struct NativeToolPoseWindow {
 std::shared_ptr<const NativeAvatarPublication> current;
 std::uint64_t authorized_sample=0,source_generation=0;
 void reset()noexcept{current.reset();authorized_sample=source_generation=0;}
 static bool fresh(const NativeAvatarPublication&p,std::uint64_t now)noexcept{
  return p.serial&&p.published_milliseconds&&now>=p.published_milliseconds&&now-p.published_milliseconds<=250;
 }
 static bool complete(const NativeAvatarPublication&p)noexcept{
  return p.asset&&p.source_instances&&!p.source_instance_offset&&p.source_instance_range&&p.source_buffer_epoch&&
   !p.parts.empty()&&p.parts.size()<=64&&!p.instance_qualifiers.empty()&&p.instance_qualifiers.size()<=64&&
   std::isfinite(p.height_scale)&&p.height_scale>0&&std::isfinite(p.basis_xz)&&std::abs(p.basis_xz)==1&&
   std::isfinite(p.diagnostic_x)&&(p.tool_hands[0].valid||p.tool_hands[1].valid);
 }
 static bool same_contract(const NativeAvatarPublication&a,const NativeAvatarPublication&b)noexcept{
  return a.asset==b.asset&&a.source_instances==b.source_instances&&a.source_instance_offset==b.source_instance_offset&&
   a.source_instance_range==b.source_instance_range&&a.source_buffer_epoch==b.source_buffer_epoch&&
   a.parts==b.parts&&a.instance_qualifiers==b.instance_qualifiers&&a.height_scale==b.height_scale&&
   a.basis_xz==b.basis_xz&&a.diagnostic_x==b.diagnostic_x;
 }
 bool valid(std::uint64_t now)const noexcept{
  return current&&source_generation&&authorized_sample&&now>=authorized_sample&&now-authorized_sample<=250&&fresh(*current,now);
 }
 bool authorize(std::shared_ptr<const NativeAvatarPublication> p,std::uint64_t generation,std::uint64_t now)noexcept{
  if(!p||!generation||!fresh(*p,now)||!complete(*p)){reset();return false;}
  // A checked new authorization can restore an expired authority without
  // replacing its still-fresh newer pose with an older queued publication.
  const bool keep=current&&fresh(*current,now)&&generation==source_generation&&same_contract(*current,*p)&&current->serial>p->serial;
  authorized_sample=p->published_milliseconds;source_generation=generation;
  if(!keep)current=std::move(p);
  return true;
 }
 bool advance(std::shared_ptr<const NativeAvatarPublication> p,std::uint64_t generation,std::uint64_t now)noexcept{
  if(!valid(now)||!p||!fresh(*p,now)||!complete(*p)||generation!=source_generation||!same_contract(*current,*p)){reset();return false;}
  if(p->serial>current->serial)current=std::move(p);
  return true;
 }
};
}
