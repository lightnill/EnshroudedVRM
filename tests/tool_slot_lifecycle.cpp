// Production publication/expiry/callback logic, with owned memory only. The
// patch boundary below models byte validation and counts transitions; it never
// writes executable pages, enumerates threads, or suspends another process.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/native_tool_slots.hpp"
#include "../src/native_tool_call_patch.hpp"
#include "../src/native_visual_slot_write.hpp"
#include "../src/native_publication.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
namespace {
using U=std::uint64_t;
const std::array<unsigned char,14> original{0x49,0x8b,0x06,0x48,0x8b,0xb4,0x24,0,1,0,0,0x49,0x89,7};
std::array<unsigned char,14> code=original;
std::array<unsigned char,4> guard{1,2,3,4};
std::array<unsigned char,56> transform{};
evrm::NativeVisualSlots visual;
evrm::NativeToolSlotTarget target;
unsigned relay_allocations=0,patch_writes=0,restorations=0,visual_writes=0,resolutions=0;
void need(bool yes,const char* reason){if(!yes)throw std::runtime_error(reason);}
struct Memory final:evrm::NativeOwnerMemory {
    U now=0;
    bool read(U address,void* out,std::size_t bytes)const noexcept override {
        const auto base=reinterpret_cast<U>(guard.data());
        if(!out||address<base||address-base>guard.size()||bytes>guard.size()-(address-base))return false;
        std::memcpy(out,reinterpret_cast<const void*>(address),bytes);return true;
    }
    U milliseconds()const noexcept override{return now?now:GetTickCount64();}
} memory;
std::shared_ptr<evrm::NativeAvatarPublication> publication(unsigned serial=1){
    auto p=std::make_shared<evrm::NativeAvatarPublication>();
    p->asset=std::make_shared<vrm::Avatar>();p->serial=serial;p->published_milliseconds=GetTickCount64();
    p->source_instances=4096;p->source_instance_range=80;p->source_buffer_epoch=1;
    p->parts={{1,2,3,4}};p->instance_qualifiers={{5,6,7,8}};
    for(auto& hand:p->tool_hands){hand.valid=true;hand.target_native_hand[12]=.125f;}
    return p;
}
void callback(){evrm::test_invoke_native_tool_slot_success(&visual);}
void expect_no_write(unsigned before,const char* why){callback();need(visual_writes==before,why);}
}
namespace evrm {
// Deterministic stand-in for the unchanged native code-patching implementation.
bool NativeToolCallPatch::install(std::uintptr_t site,const std::array<unsigned char,14>& before,std::uintptr_t wrapper)noexcept {
    if(installed_)return site==site_&&before==before_&&wrapper==wrapper_&&code==after_;
    if(site!=reinterpret_cast<std::uintptr_t>(code.data())||!wrapper||code!=before)return false;
    if(site!=site_||before!=before_||wrapper!=wrapper_){++relay_allocations;site_=site;wrapper_=wrapper;before_=before;after_.fill(0xe9);}
    code=after_;++patch_writes;installed_=true;return true;
}
bool NativeToolCallPatch::restore()noexcept {
    if(!installed_)return true;
    if(code!=after_)return false;
    code=before_;++patch_writes;++restorations;installed_=false;return true;
}
bool resolve_native_tool_slot_target(const NativeOwnerMemory& m,const NativeOwnerCandidate&,NativeToolSlotTarget& out,std::string&)noexcept {
    ++resolutions;std::array<unsigned char,4> observed{};
    if(!m.read(target.guards[0].address,observed.data(),observed.size())||observed!=std::array<unsigned char,4>{1,2,3,4})return false;
    out=target;return true;
}
bool write_native_visual_slot_data(U destination,const float* data,std::size_t bytes)noexcept {
    const auto base=reinterpret_cast<U>(&visual)+32;
    if(!data||!((destination==base+8*48&&bytes==96)||(destination==base+14*48&&bytes==96)||(destination==base+12*48&&bytes==48)))return false;
    std::memcpy(reinterpret_cast<void*>(destination),data,bytes);++visual_writes;return true;
}
}
int main(){try{
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_TOOL_SLOTS",L"1");SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",nullptr);
    target.client_slots=reinterpret_cast<U>(&visual);target.render_transform=reinterpret_cast<U>(transform.data());
    target.publish_call_site=reinterpret_cast<U>(code.data());target.guards={{reinterpret_cast<U>(guard.data()),{1,2,3,4}}};
    const std::array<float,4> q{0,0,0,1};const std::array<float,3> scale{1,1,1};
    std::memcpy(transform.data()+24,q.data(),sizeof q);std::memcpy(transform.data()+40,scale.data(),sizeof scale);
    for(auto& m:visual.matrices)m={1,0,0,0,1,0,0,0,1,0,0,0};
    for(const auto slot:{8,9,12,14,15})visual.active|=U(1)<<slot;
    evrm::NativeOwnerCandidate owner;owner.entity=1;owner.native_buffer=4096;
    std::weak_ptr<const evrm::NativeAvatarPublication> previous;
    std::weak_ptr<const vrm::Avatar> previous_asset;
    {
        evrm::NativeToolSlots slots;
        for(unsigned i=1;i<=1000;++i){
            slots.disable();need(previous.expired()&&previous_asset.expired(),"disabled model/pose retained across changeover");
            const auto before=visual_writes;expect_no_write(before,"disabled callback retained old authority");
            auto p=publication(i);slots.advance(memory,p,i);expect_no_write(before,"advance authorized a new model after disable");
            slots.publish(memory,owner,p,i);callback();need(visual_writes==before+3,"new model publication did not authorize approved hand groups");
            previous=p;previous_asset=p->asset;p.reset();need(!previous.expired(),"active pose unexpectedly released");
            need(relay_allocations==1&&patch_writes==1&&!restorations,"handoff reallocated relay or restored/repatched native bytes");
        }
        slots.disable();need(previous.expired()&&previous_asset.expired(),"last model/pose retained after disable");
        auto p=publication(1001);auto before=visual_writes;
        p->published_milliseconds-=251;slots.publish(memory,owner,p,1);expect_no_write(before,"stale pose accepted after disable");
        p=publication(1002);slots.publish(memory,owner,p,1);callback();need(visual_writes==before+3,"fresh authority failed");before=visual_writes;
        {evrm::NativeToolSlots other;other.publish(memory,owner,publication(1003),1);other.disable();}
        callback();need(visual_writes==before+3,"another owner revoked existing authority");before=visual_writes;
        guard[0]^=1;expect_no_write(before,"changed target guard accepted by callback");slots.publish(memory,owner,p,1);expect_no_write(before,"changed target guard accepted by resolver");guard[0]^=1;
        slots.publish(memory,owner,p,1);callback();need(visual_writes==before+3,"guard recovery failed");before=visual_writes;
        const auto patched=code;code[0]^=1;slots.publish(memory,owner,p,1);expect_no_write(before,"changed hook bytes accepted");need(code[0]!=(patched[0]),"foreign hook bytes overwritten");code=patched;
        slots.publish(memory,owner,p,1);callback();need(visual_writes==before+3,"hook guard recovery failed");before=visual_writes;
        slots.advance(memory,publication(1004),2);expect_no_write(before,"advance crossed source generation");
        slots.publish(memory,owner,p,0);expect_no_write(before,"zero source generation authorized");
        auto wrong=owner;wrong.native_buffer=8192;slots.publish(memory,wrong,p,1);expect_no_write(before,"wrong source buffer authorized");
        slots.publish(memory,owner,p,1);memory.now=p->published_milliseconds+251;slots.advance(memory,publication(1005),1);memory.now=0;expect_no_write(before,"expired authorization renewed through advance");
        p.reset();slots.clear();need(code==original&&restorations==1&&patch_writes==2,"real teardown did not restore exactly once");
    }
    need(restorations==1&&patch_writes==2&&relay_allocations==1,"destructor repeated restoration");
    std::cout<<"PASS 1000 changeovers: modeled relay allocations="<<relay_allocations<<" patch-boundary writes="<<patch_writes<<" restorations="<<restorations
        <<"; all 1000 old pose/model weak references expired; source/owner/expiry/target-byte guards intact; resolutions="<<resolutions<<"\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
