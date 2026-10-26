#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "native_tool_slots.hpp"
#include "native_publication.hpp"
#include "native_tool_pose_window.hpp"
#include "native_tool_call_patch.hpp"
#include "native_visual_slot_write.hpp"
#include "runtime_log.hpp"
#include <atomic>
#include <cstring>
#include <mutex>
#include <chrono>
namespace evrm { namespace {
using U=std::uint64_t;

struct SelfMemory final:NativeOwnerMemory {
 bool read(U address,void*dst,size_t size)const noexcept override{if(!address||!dst||!size||size>65536||address>=0x800000000000ULL||size>0x800000000000ULL-address)return false;SIZE_T got=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(static_cast<std::uintptr_t>(address)),dst,size,&got)&&got==size;}
 U milliseconds()const noexcept override{return GetTickCount64();}
};
SelfMemory self;
struct Snapshot {NativeToolSlotTarget target;NativeToolPoseWindow window;};
std::shared_ptr<const Snapshot> snapshot;
// Advisory rejection key only. Never dereferenced or accepted as authority;
// matching callbacks still acquire and validate the full owned snapshot.
std::atomic<U> callback_destination{0};
void store_snapshot(std::shared_ptr<const Snapshot> next)noexcept {
 const auto destination=next?next->target.client_slots:0;
 if(!destination)callback_destination.store(0,std::memory_order_release);
 std::atomic_store_explicit(&snapshot,std::move(next),std::memory_order_release);
 if(destination)callback_destination.store(destination,std::memory_order_release);
}

std::atomic<U> applied{0};
std::atomic<U> last_applied_serial{0};
// Bounded diagnostic: stop callback clocks after four 300-presentation windows.
std::atomic<bool> profile_callbacks{false};
std::atomic<U> callback_calls{0},callback_nanoseconds{0},callback_targets{0},callback_target_nanoseconds{0};
std::array<std::atomic<U>,5> callback_stage_ns{};
unsigned callback_profile_frames=0;
struct CallbackTiming {
 bool enabled=profile_callbacks.load(std::memory_order_relaxed);
 bool target=false;
 std::chrono::steady_clock::time_point started=enabled?std::chrono::steady_clock::now():std::chrono::steady_clock::time_point{};
 std::chrono::steady_clock::time_point last=started;
 void stage(unsigned index)noexcept {if(enabled){const auto now=std::chrono::steady_clock::now();callback_stage_ns[index].fetch_add(std::chrono::duration_cast<std::chrono::nanoseconds>(now-last).count(),std::memory_order_relaxed);last=now;}}
 ~CallbackTiming(){if(enabled){const auto ns=std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now()-started).count();callback_calls.fetch_add(1,std::memory_order_relaxed);callback_nanoseconds.fetch_add(ns,std::memory_order_relaxed);if(target){callback_target_nanoseconds.fetch_add(ns,std::memory_order_relaxed);stage(4);}}}
};
std::mutex control_mutex;
const void* control_owner=nullptr;
NativeToolCallPatch call_patch;
#ifdef EVRM_NATIVE_TOOL_SLOTS_TESTING
void(*write_observer)(U,std::size_t)=nullptr;
#endif
bool write_visual_slots(U address,const float* data,std::size_t bytes) {
#ifdef EVRM_NATIVE_TOOL_SLOTS_TESTING
 if(write_observer)write_observer(address,bytes);
#endif
 return write_native_visual_slot_data(address,data,bytes);
}
bool guards_current(const NativeOwnerMemory&memory,const NativeToolSlotTarget&t)noexcept {
 if(t.guards.empty()||t.guards.size()>128)return false;
 std::array<unsigned char,256> buffer{};
 for(const auto&g:t.guards){if(g.bytes.empty()||g.bytes.size()>buffer.size()||!memory.read(g.address,buffer.data(),g.bytes.size())||std::memcmp(g.bytes.data(),buffer.data(),g.bytes.size()))return false;}
 return true;
}
SecondaryHandContactMode read_contact_mode(const NativeOwnerMemory&memory,const NativeAvatarPublication&candidate,U now,U&owned,U*observed)noexcept {
 owned=0;
 if(observed)*observed=0;
 try{
  const auto current=std::atomic_load_explicit(&snapshot,std::memory_order_acquire);
  if(!current||!current->window.valid(now)||!NativeToolPoseWindow::fresh(candidate,now)||
     !NativeToolPoseWindow::complete(candidate)||!NativeToolPoseWindow::same_contract(*current->window.current,candidate))return SecondaryHandContactMode::None;
  const auto&t=current->target;
  if(!t.client_slots||!t.equipment_animation_sets||!t.client_ranged_weapon||!guards_current(memory,t))return SecondaryHandContactMode::None;
  std::array<std::uint8_t,2> equipment{},ranged{},equipment_after{},ranged_after{};
  if(!memory.read(t.equipment_animation_sets,equipment.data(),2)||!memory.read(t.client_ranged_weapon,ranged.data(),2)||
     !guards_current(memory,t)||!memory.read(t.equipment_animation_sets,equipment_after.data(),2)||
     !memory.read(t.client_ranged_weapon,ranged_after.data(),2)||equipment!=equipment_after||ranged!=ranged_after)return SecondaryHandContactMode::None;
  const auto finished=memory.milliseconds();
  if(!current->window.valid(finished)||!NativeToolPoseWindow::fresh(candidate,finished)||
     current!=std::atomic_load_explicit(&snapshot,std::memory_order_acquire))return SecondaryHandContactMode::None;
  const auto mode=classify_secondary_hand_contact(equipment,ranged);
  if(mode!=SecondaryHandContactMode::None){owned=t.client_slots;if(observed)*observed=finished;}
  return mode;
 }catch(...){return SecondaryHandContactMode::None;}
}
void success_callback(void*destination)noexcept {
 CallbackTiming timing;
 const auto address=reinterpret_cast<U>(destination);
 if(!address||address!=callback_destination.load(std::memory_order_acquire))return;
 try{const auto current=std::atomic_load_explicit(&snapshot,std::memory_order_acquire);if(!current||!current->window.current)return;
  const auto&t=current->target;
  if(reinterpret_cast<U>(destination)!=t.client_slots)return;
  timing.target=true;
  if(timing.enabled)callback_targets.fetch_add(1,std::memory_order_relaxed);
  const auto now=GetTickCount64(),sample=current->window.current->published_milliseconds;if(!current->window.valid(now)||!guards_current(self,t))return;
  timing.stage(0);
  NativeVisualSlots source;std::array<unsigned char,56> transform{};
  if(!self.read(t.client_slots,&source,sizeof source)||!self.read(t.render_transform,transform.data(),transform.size()))return;
  timing.stage(1);
  std::array<float,4> q;std::array<float,3> scale;std::memcpy(q.data(),transform.data()+24,sizeof q);std::memcpy(scale.data(),transform.data()+40,sizeof scale);
  NativeToolSlotEdits edits;if(!make_native_tool_slot_edits(source,q,scale,current->window.current->tool_hands,sample,now,edits))return;
  timing.stage(2);
  // This callback runs only after this entity's native success path stored its mask.
  // No server/collider/pose data is written; origin and all other slots stay native.
  for(size_t i=0;i<edits.enabled.size();++i)if(edits.enabled[i]){const auto address=t.client_slots+32+NativeToolSlotEdits::slots[i]*48;
   // Only adjacent enabled corrections may share one write. Shield12 is a
   // separate48byte write; unrelated10/11/13 are never included.
   if(i+1<edits.enabled.size()&&edits.enabled[i+1]&&NativeToolSlotEdits::slots[i+1]==NativeToolSlotEdits::slots[i]+1){
    std::array<float,24> pair;
    std::copy(edits.matrices[i].begin(),edits.matrices[i].end(),pair.begin());
    std::copy(edits.matrices[i+1].begin(),edits.matrices[i+1].end(),pair.begin()+12);
    if(!write_visual_slots(address,pair.data(),sizeof pair))return;
    ++i;
   }else if(!write_visual_slots(address,edits.matrices[i].data(),48))return;}
  timing.stage(3);
  const auto count=applied.fetch_add(1,std::memory_order_relaxed)+1;
  last_applied_serial.store(current->window.current->serial,std::memory_order_release);
  if(count==1||count%300==0)log_event("tool-slots.applied count="+std::to_string(count)+" pose-serial="+std::to_string(current->window.current->serial)+" pose-age-ms="+std::to_string(now-sample)+" right-y="+std::to_string(source.matrices[8][10])+"->"+std::to_string(edits.matrices[0][10]));
 }catch(...){}
}
void clear_locked(const void*owner)noexcept {
 if(control_owner!=owner)return;
 store_snapshot({});
 if(call_patch.restore())control_owner=nullptr;
 else log_event("tool-slots.restore-declined original-forwarding-retained");
}
}
NativeToolSlots::NativeToolSlots()noexcept {wchar_t value[4]{};enabled_=GetEnvironmentVariableW(L"ENSHROUDED_VRM_TOOL_SLOTS",value,4)==1&&value[0]==L'1';profile_callbacks.store(enabled_,std::memory_order_relaxed);}
SecondaryHandContactMode read_native_hand_contact_mode(const NativeAvatarPublication&p,U now,U&owned,U*observed)noexcept {return read_contact_mode(self,p,now,owned,observed);}
NativeToolSlots::~NativeToolSlots(){clear();}
void NativeToolSlots::disable()noexcept {if(!enabled_)return;try{std::lock_guard<std::mutex> lock(control_mutex);if(control_owner==this)store_snapshot({});}catch(...){} }
void NativeToolSlots::clear()noexcept {try{std::lock_guard<std::mutex> lock(control_mutex);clear_locked(this);}catch(...){}target_={};identity_={};}
void NativeToolSlots::publish(const NativeOwnerMemory&memory,const NativeOwnerCandidate&owner,std::shared_ptr<const NativeAvatarPublication>pose,U source_generation)noexcept {
 if(!enabled_)return;
 try{if(!pose||pose->source_instances!=owner.native_buffer||(!pose->tool_hands[0].valid&&!pose->tool_hands[1].valid)){disable();return;}
  const std::array<U,8> identity{owner.entity,owner.entity_layout,owner.entity_storage,(U(owner.entity_generation)<<32)|owner.entity_id,owner.entity_row,owner.execution,owner.root,owner.image.base};
  if(identity!=identity_||!guards_current(memory,target_)){disable();target_={};identity_={};std::string why;if(!resolve_native_tool_slot_target(memory,owner,target_,why)){log_event("tool-slots.target-declined "+why);return;}identity_=identity;}
  std::lock_guard<std::mutex> lock(control_mutex);if(control_owner&&control_owner!=this)return;
  auto next=std::make_shared<Snapshot>();next->target=target_;
  const auto previous=std::atomic_load_explicit(&snapshot,std::memory_order_acquire);
  if(control_owner==this&&previous)next->window=previous->window;
  const std::array<unsigned char,14> before{0x49,0x8b,0x06,0x48,0x8b,0xb4,0x24,0x00,0x01,0x00,0x00,0x49,0x89,0x07};
  if(!call_patch.install(target_.publish_call_site,before,reinterpret_cast<std::uintptr_t>(&success_callback))){store_snapshot({});log_event("tool-slots.success-hook-install-declined");return;}
  if(!control_owner)log_event("tool-slots.installed per-entity-client-publish-success");
  control_owner=this;
  if(!guards_current(memory,target_)||!next->window.authorize(std::move(pose),source_generation,memory.milliseconds())){
   store_snapshot({});return;
  }
  store_snapshot(std::move(next));
 }catch(...){clear();}
}
void NativeToolSlots::advance(const NativeOwnerMemory&memory,std::shared_ptr<const NativeAvatarPublication>pose,U source_generation)noexcept {
 if(!enabled_)return;
 try{std::lock_guard<std::mutex> lock(control_mutex);if(control_owner!=this)return;
  const auto previous=std::atomic_load_explicit(&snapshot,std::memory_order_acquire);if(!previous)return;
  auto next=std::make_shared<Snapshot>(*previous);
  if(!guards_current(memory,next->target)||!next->window.advance(std::move(pose),source_generation,memory.milliseconds())){
   store_snapshot({});return;
  }
  const auto serial=next->window.current->serial;
  store_snapshot(std::move(next));
  // Bounded telemetry is flushed on presentation, outside the native callback.
  if(advance_notices_<120){++advance_notices_;log_event("tool-slots.pose-advance serial="+std::to_string(serial)+" last-applied-serial="+std::to_string(last_applied_serial.load(std::memory_order_acquire)));}
  if(profile_callbacks.load(std::memory_order_relaxed)&&++callback_profile_frames%300==0){
   const auto calls=callback_calls.exchange(0),targets=callback_targets.exchange(0),nanoseconds=callback_nanoseconds.exchange(0),target_ns=callback_target_nanoseconds.exchange(0);
   log_event("tool-slots.callback-profile frames=300 calls="+std::to_string(calls)+" targets="+std::to_string(targets)+" ms-per-frame="+std::to_string(double(nanoseconds)/300000000.0)+" target-ms-per-frame="+std::to_string(double(target_ns)/300000000.0));
   std::string stages="tool-slots.callback-stages-ms guard,read,math,write,exit=";for(auto& ns:callback_stage_ns)stages+=std::to_string(double(ns.exchange(0))/300000000.0)+",";log_event(stages);
   if(callback_profile_frames>=1200)profile_callbacks.store(false,std::memory_order_relaxed);
  }
 }catch(...){disable();}
}
#ifdef EVRM_NATIVE_TOOL_SLOTS_TESTING
namespace {int test_owner;}
SecondaryHandContactMode test_read_native_hand_contact_mode(const NativeOwnerMemory&m,const NativeAvatarPublication&p,U now,U&owned,U*observed)noexcept{return read_contact_mode(m,p,now,owned,observed);}
bool test_install_native_tool_slots(const NativeToolSlotTarget&t,std::shared_ptr<const NativeAvatarPublication>p)noexcept {
 try{auto next=std::make_shared<Snapshot>();next->target=t;if(!next->window.authorize(std::move(p),1,GetTickCount64()))return false;control_owner=&test_owner;store_snapshot(std::move(next));return true;}catch(...){return false;}
}
void test_clear_native_tool_slots()noexcept {clear_locked(&test_owner);}
void test_invoke_native_tool_slot_success(void*a)noexcept {success_callback(a);}
void test_with_native_tool_snapshot_lock(void(*task)(void*),void* context){std::_Sp_locker lock(&snapshot);task(context);}
void test_observe_native_tool_slot_writes(void(*observer)(U,std::size_t))noexcept {write_observer=observer;}
#endif
}
