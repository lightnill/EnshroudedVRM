// Real publication/advance/callback code with owned destinations. The patch
// boundary is modeled; no executable pages, game process, or GPU are touched.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "../src/native_tool_slots.hpp"
#include "../src/native_tool_call_patch.hpp"
#include "../src/native_visual_slot_write.hpp"
#include "../src/native_publication.hpp"
#include "../src/native_tool_pose_window.hpp"
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
// Exercise the production target join separately from the deterministic patch
// lifecycle fixture below. Its input is an owned exact-layout memory image.
#define resolve_native_tool_slot_target resolve_fixture_native_tool_slot_target
#include "../src/native_tool_slot_target.cpp"
#undef resolve_native_tool_slot_target
namespace {
using U=std::uint64_t;
void need(bool yes,const char* reason){if(!yes)throw std::runtime_error(reason);}
const std::array<unsigned char,14> original{0x49,0x8b,0x06,0x48,0x8b,0xb4,0x24,0,1,0,0,0x49,0x89,7};
std::array<unsigned char,14> code=original;
unsigned installs=0,restores=0;
struct Actor {
    evrm::NativeVisualSlots visual;
    std::array<unsigned char,56> transform{};
    std::array<unsigned char,16> record{};
    std::array<unsigned char,2> equipment{4,0},ranged{0,0};
    std::uint32_t generation=7;
    unsigned writes=0;
    evrm::NativeOwnerCandidate owner;
    evrm::NativeToolSlotTarget target;
    Actor(unsigned id,U account){
        const std::array<float,4> q{0,0,0,1};const std::array<float,3> scale{1,1,1};
        std::memcpy(transform.data()+24,q.data(),sizeof q);std::memcpy(transform.data()+40,scale.data(),sizeof scale);
        const std::uint32_t connection=id-1;std::memcpy(record.data(),&id,4);std::memcpy(record.data()+4,&connection,4);std::memcpy(record.data()+8,&account,8);
        for(auto& m:visual.matrices)m={1,0,0,0,1,0,0,0,1,0,0,0};
        for(const auto slot:{8,9,12,14,15})visual.active|=U(1)<<slot;
        owner.entity=reinterpret_cast<U>(&generation);owner.entity_id=id;owner.entity_generation=generation;
        owner.native_buffer=4096+id*4096;owner.account=account;owner.connection=connection;
        owner.player_infos=owner.player_record=reinterpret_cast<U>(record.data());
        target.client_slots=reinterpret_cast<U>(&visual);target.render_transform=reinterpret_cast<U>(transform.data());
        target.publish_call_site=reinterpret_cast<U>(code.data());target.entity_id=id;
        target.equipment_animation_sets=reinterpret_cast<U>(equipment.data());target.client_ranged_weapon=reinterpret_cast<U>(ranged.data());
        target.guards={{reinterpret_cast<U>(record.data()),{record.begin(),record.end()}},
                       {reinterpret_cast<U>(&generation),{reinterpret_cast<unsigned char*>(&generation),reinterpret_cast<unsigned char*>(&generation)+4}}};
    }
};
Actor a(1,0x101),b(2,0x202),third(3,0x303);
std::vector<Actor*> actors{&a,&b,&third};
struct Memory final:evrm::NativeOwnerMemory {
    U now=0;
    bool read(U address,void* out,std::size_t size)const noexcept override{
        for(const auto* p:actors){
            const auto start=reinterpret_cast<U>(p),end=start+sizeof *p;
            if(address>=start&&address<=end&&size<=end-address){std::memcpy(out,reinterpret_cast<void*>(address),size);return true;}
        }return false;
    }
    U milliseconds()const noexcept override{return now?now:GetTickCount64();}
} memory;
std::shared_ptr<evrm::NativeAvatarPublication> pose(const Actor&actor,std::string subject,float hand,unsigned serial=1){
    auto p=std::make_shared<evrm::NativeAvatarPublication>();p->asset=std::make_shared<vrm::Avatar>();p->subject=std::move(subject);
    p->serial=serial;p->published_milliseconds=GetTickCount64();p->source_instances=actor.owner.native_buffer;
    p->source_instance_range=80;p->source_buffer_epoch=1;p->parts={{actor.owner.entity_id,2,3,4}};p->instance_qualifiers={{5,6,7,8}};
    p->player_identity={actor.owner.account,actor.owner.entity_id,actor.owner.connection,actor.owner.entity_id==1};p->assignment_generation=1;
    for(auto& h:p->tool_hands){h.valid=true;h.target_native_hand[12]=hand;}return p;
}
void callback(Actor& actor){evrm::test_invoke_native_tool_slot_success(&actor.visual);}
void expect(Actor&actor,unsigned writes,const char* reason){const auto before=actor.writes;callback(actor);need(actor.writes==before+writes,reason);}
evrm::NativeToolSlots* queued_slots=nullptr;
void revoke_queued(U,std::size_t){evrm::test_observe_native_tool_slot_writes(nullptr);queued_slots->disable_subject("");}
void unrelated_with_lock(void*){callback(third);}
struct TargetFixture final:evrm::NativeOwnerMemory {
    std::vector<std::vector<unsigned char>> blocks;
    evrm::NativeOwnerCandidate owner;
    U allocate(std::size_t n){blocks.emplace_back(n);return reinterpret_cast<U>(blocks.back().data());}
    template<class T>void put(U address,T value){std::memcpy(reinterpret_cast<void*>(address),&value,sizeof value);}
    bool read(U address,void*out,std::size_t size)const noexcept override{
        for(const auto&block:blocks){const auto start=reinterpret_cast<U>(block.data());if(address>=start&&address-start<=block.size()&&size<=block.size()-(address-start)){
            std::memcpy(out,reinterpret_cast<const void*>(address),size);return true;}}return false;
    }
    U milliseconds()const noexcept override{return GetTickCount64();}
    TargetFixture(){
        namespace p=evrm::native_owner_profile;namespace e=p::ecsReadLayout;namespace s=p::serviceLayout;
        auto&o=owner;o.image.base=allocate(p::image_size);o.image.size=p::image_size;o.image.timestamp=p::image_timestamp;
        std::copy(std::begin(p::image_sha256),std::end(p::image_sha256),o.image.sha256.begin());
        o.root=allocate(1024);o.client=allocate(p::ownerLayout::controllerPointerOffset+8);o.controller=allocate(1024);o.main=allocate(1024);
        o.execution=allocate(e::executionRegistryOwnerOffset+s::systemCountOffset+4096);o.registry=o.execution+e::executionRegistryOwnerOffset;
        o.entity=allocate(64);o.entity_layout=allocate(4096);o.entity_storage=allocate(4096);o.entity_id=2;o.entity_generation=9;o.native_buffer=4096;
        put(o.root,o.image.base+p::ownerLayout::rootVtableRva);put(o.root+p::ownerLayout::clientPointerOffset,o.client);
        put(o.client,o.image.base+p::ownerLayout::clientVtableRva);put(o.client+p::ownerLayout::controllerPointerOffset,o.controller);
        put(o.controller,o.image.base+p::ownerLayout::controllerVtableRva);put(o.controller+p::ownerLayout::controllerClientBackPointerOffset,o.client);
        put(o.controller+p::ownerLayout::controllerMainOwnerOffset,o.main);put(o.main+e::mainExecutionOwnerOffset,o.execution);
        put(o.entity+16,o.entity_id);put(o.entity+20,o.entity_generation);put(o.entity+24,o.entity_layout);put(o.entity+32,o.entity_storage);put(o.entity+48,o.entity_row);
        const U rows=allocate(512);put(o.registry+8,rows);put(o.registry+16,U(2));put(o.registry+24,U(2));
        const auto type=[&](unsigned index,const std::string&name,std::uint16_t size,std::uint16_t offset){
            std::uint32_t h=2166136261u;for(unsigned char c:name)h=(h^c)*16777619u;
            const U text=o.image.base+0x5000+index*256,meta=o.image.base+0x1000+index*256,row=rows+index*256;
            std::memcpy(reinterpret_cast<void*>(text),name.data(),name.size());
            put(row+16,text);put(row+24,U(name.size()));put(row+32,h);put(row+40,meta);put(row+56,std::uint32_t(size));
            put(meta+32,text);put(meta+40,U(name.size()));put(meta+64,std::uint32_t(size));put(meta+80,h);
            put(o.entity_layout+e::componentOffsets+index*2,offset);put(o.entity_layout+e::componentStrides+index*2,size);
        };
        type(0,"keen::ecs::AnimationGraphSlotComponent",1760,0);type(1,"keen::ecs::RenderTransform",176,1760);put(o.entity_layout,std::uint8_t(3));
        put(o.registry+s::systemCountOffset,std::uint16_t(1));const U row=o.registry+s::systemsOffset,descriptor=o.image.base+0x2000,name=o.image.base+0x6000;
        const std::string label="update_animation_graph_slots_2";std::memcpy(reinterpret_cast<void*>(name),label.data(),label.size());
        put(row,descriptor);put(row+40,o.image.base+0x31c3b0);put(descriptor,name);put(descriptor+8,U(label.size()));put(descriptor+16,o.image.base+0x31c3b0);
        o.account=0xabcdef;o.connection=65;o.player_infos=allocate(16*160);o.player_record=o.player_infos+160;
        put(o.player_record,o.entity_id);put(o.player_record+4,o.connection);put(o.player_record+8,o.account);
        const auto*record=reinterpret_cast<const unsigned char*>(o.player_record);o.guards=std::vector<evrm::NativeOwnerGuard>{{o.player_record,{record,record+16}}};
    }
};
void target_association(){
    TargetFixture memory;evrm::NativeToolSlotTarget target;std::string reason;
    need(evrm::resolve_fixture_native_tool_slot_target(memory,memory.owner,target,reason),"owned production target join failed");
    bool account_guard=false,generation_guard=false;
    for(const auto&g:target.guards){account_guard|=g.address==memory.owner.player_record&&g.bytes.size()==16;generation_guard|=g.address==memory.owner.entity+20&&g.bytes.size()==4;}
    need(account_guard&&generation_guard&&target.guards.size()<128,"compact callback association/generation guards absent");
    const auto saved=memory.owner.account;++memory.owner.account;
    need(!evrm::resolve_fixture_native_tool_slot_target(memory,memory.owner,target,reason),"metadata changed without consumed account guard accepted");memory.owner.account=saved;
    memory.put(memory.owner.player_record+8,U(saved+1));
    need(!evrm::resolve_fixture_native_tool_slot_target(memory,memory.owner,target,reason),"current account record change accepted");memory.put(memory.owner.player_record+8,saved);
    memory.owner.guards=std::vector<evrm::NativeOwnerGuard>{{memory.owner.entity,{1}}};
    need(!evrm::resolve_fixture_native_tool_slot_target(memory,memory.owner,target,reason),"account record absent from consumed guards accepted");
}
void contracts(){
    const auto base=pose(a,"",.25f);const U now=base->published_milliseconds;
    for(unsigned field=0;field<9;++field){
        evrm::NativeToolPoseWindow window;need(window.authorize(base,11,now),"contract baseline rejected");
        auto changed=std::make_shared<evrm::NativeAvatarPublication>(*base);++changed->serial;
        switch(field){
        case 0:++changed->player_identity.account;break;
        case 1:++changed->player_identity.entity_id;break;
        case 2:++changed->player_identity.connection;break;
        case 3:changed->player_identity.local=!changed->player_identity.local;break;
        case 4:changed->subject="synthetic-other";break;
        case 5:++changed->assignment_generation;break;
        case 6:++changed->source_buffer_epoch;break;
        case 7:++changed->source_instances;break;
        default:changed->asset=std::make_shared<vrm::Avatar>();break;
        }
        need(!window.advance(changed,11,now)&&!window.current,"identity/resource contract advanced old authority");
    }
    evrm::NativeToolPoseWindow window;need(window.authorize(base,11,now),"authority baseline rejected");
    auto newer=std::make_shared<evrm::NativeAvatarPublication>(*base);++newer->serial;newer->published_milliseconds=now+200;
    need(window.advance(newer,11,now+200),"in-authority fresh sample rejected");
    need(!window.valid(now+251),"newer pose renewed submission expiry");
}
void full_roster(evrm::NativeToolSlots& slots){
    std::vector<std::unique_ptr<Actor>> cohort;
    for(unsigned id=1;id<=17;++id){
        auto actor=std::make_unique<Actor>(id,0x1000+id);actors.push_back(actor.get());cohort.push_back(std::move(actor));
    }
    for(unsigned i=0;i<17;++i){auto p=pose(*cohort[i],"synthetic-slot-"+std::to_string(i),float(i+1)/8);
        slots.publish(memory,cohort[i]->owner,p,i+1);}
    for(unsigned i=0;i<16;++i){expect(*cohort[i],3,"sixteen-subject authority lost");need(cohort[i]->visual.matrices[8][9]==float(i+1)/8,"sixteen subjects shared pose state");}
    expect(*cohort[16],0,"seventeenth subject exceeded proven bound");
    slots.disable_subject("synthetic-slot-7");expect(*cohort[7],0,"target removal failed in full roster");
    for(unsigned i=0;i<16;++i)if(i!=7)expect(*cohort[i],3,"target removal disturbed roster peer");
    slots.retain_subjects({});
    for(unsigned i=0;i<16;++i)expect(*cohort[i],0,"roster disconnect retained authority");
    actors.resize(3);
}
}
namespace evrm {
bool NativeToolCallPatch::install(std::uintptr_t site,const std::array<unsigned char,14>& before,std::uintptr_t wrapper)noexcept{
    if(installed_)return site==site_&&before==before_&&wrapper==wrapper_&&code==after_;
    if(site!=reinterpret_cast<U>(code.data())||!wrapper||code!=before)return false;
    site_=site;wrapper_=wrapper;before_=before;after_.fill(0xe9);code=after_;installed_=true;++installs;return true;
}
bool NativeToolCallPatch::restore()noexcept{if(!installed_)return true;if(code!=after_)return false;code=before_;installed_=false;++restores;return true;}
bool resolve_native_tool_slot_target(const NativeOwnerMemory&,const NativeOwnerCandidate&o,NativeToolSlotTarget&out,std::string&)noexcept{
    for(const auto* p:actors)if(o.entity==p->owner.entity){out=p->target;return true;}
    return false;
}
bool write_native_visual_slot_data(U destination,const float*data,std::size_t bytes)noexcept{
    for(auto*p:actors){const auto base=reinterpret_cast<U>(&p->visual)+32;
        if((destination==base+8*48&&bytes==96)||(destination==base+14*48&&bytes==96)||(destination==base+12*48&&bytes==48)){
            std::memcpy(reinterpret_cast<void*>(destination),data,bytes);++p->writes;return true;}}
    return false;
}
}
int main(){try{
    SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",nullptr);
    evrm::NativeToolSlots slots;auto pa=pose(a,"",.25f),pb=pose(b,"synthetic-remote",.75f);
    slots.publish(memory,a.owner,pa,11);slots.publish(memory,b.owner,pb,22);
    expect(a,3,"publishing second subject revoked first destination");expect(b,3,"second destination unauthorized");expect(third,0,"unassigned destination changed");
    need(a.visual.matrices[8][9]==.25f&&b.visual.matrices[8][9]==.75f,"subjects shared hand state");
    need(installs==1&&!restores,"per-subject hook duplicated");
    const auto b_source=b.owner.native_buffer;b.owner.native_buffer=a.owner.native_buffer;
    auto shared=pose(b,"synthetic-remote",.875f);shared->asset=pa->asset;
    slots.publish(memory,b.owner,shared,22);expect(a,3,"shared model/source disturbed local subject");expect(b,3,"shared model/source remote failed");
    need(a.visual.matrices[8][9]==.25f&&b.visual.matrices[8][9]==.875f,"same model/source shared mutable hands");
    b.owner.native_buffer=b_source;slots.publish(memory,b.owner,pb,22);
    evrm::test_with_native_tool_snapshot_lock(&unrelated_with_lock,nullptr);
    U owned=0,observed=0;
    need(evrm::test_read_native_hand_contact_mode(memory,*pa,memory.milliseconds(),owned,&observed)==evrm::SecondaryHandContactMode::TwoHanded&&owned==a.target.client_slots&&observed,"local contact crossed subject destination");
    b.equipment={5,0};b.ranged={2,0};
    need(evrm::test_read_native_hand_contact_mode(memory,*pb,memory.milliseconds(),owned,&observed)==evrm::SecondaryHandContactMode::BowDrawn&&owned==b.target.client_slots,"remote contact crossed subject destination");
    auto next=std::make_shared<evrm::NativeAvatarPublication>(*pa);++next->serial;next->tool_hands[0].target_native_hand[12]=.5f;next->tool_hands[1].target_native_hand[12]=.5f;
    slots.advance(memory,next,11);expect(a,3,"first subject advance failed");expect(b,3,"first advance disturbed second");
    auto changed=std::make_shared<evrm::NativeAvatarPublication>(*next);++changed->serial;++changed->assignment_generation;
    slots.advance(memory,changed,11);expect(a,0,"assignment generation advanced old authority");expect(b,3,"target revocation disturbed second");
    slots.publish(memory,a.owner,pa,11);a.record[8]^=1;expect(a,0,"account record guard accepted reuse");expect(b,3,"account reuse disturbed unrelated target");a.record[8]^=1;
    slots.publish(memory,a.owner,pa,11);++a.generation;expect(a,0,"actor generation accepted reuse");--a.generation;
    slots.publish(memory,a.owner,pa,11);auto foreign=std::make_shared<evrm::NativeAvatarPublication>(*pa);++foreign->player_identity.account;
    slots.advance(memory,foreign,11);expect(a,0,"foreign account advanced subject authority");expect(b,3,"foreign account disturbed unrelated subject");
    slots.publish(memory,a.owner,pa,11);foreign=std::make_shared<evrm::NativeAvatarPublication>(*pa);foreign->source_instances=b.owner.native_buffer;
    slots.publish(memory,a.owner,foreign,11);expect(a,0,"wrong source published subject authority");expect(b,3,"wrong source disturbed unrelated subject");
    auto unbound=a.owner;unbound.account=unbound.player_infos=unbound.player_record=0;unbound.connection=0;
    slots.publish(memory,unbound,pa,11);expect(a,0,"identified publication accepted legacy unbound owner");expect(b,3,"unbound owner disturbed unrelated subject");
    memory.now=pb->published_milliseconds+251;slots.advance(memory,pb,22);memory.now=0;expect(b,0,"advance renewed expired authority");
    pa=pose(a,"",.25f);pb=pose(b,"synthetic-remote",.75f);slots.publish(memory,a.owner,pa,11);slots.publish(memory,b.owner,pb,22);
    slots.disable_subject("");expect(a,0,"target disable retained authority");expect(b,3,"target disable cleared unrelated subject");
    slots.advance(memory,pa,11);expect(a,0,"advance recreated disabled subject");
    slots.publish(memory,a.owner,pa,11);slots.retain_subjects({"synthetic-remote"});expect(a,0,"disconnected subject retained authority");expect(b,3,"retain removed live subject");
    slots.publish(memory,a.owner,pa,11);queued_slots=&slots;evrm::test_observe_native_tool_slot_writes(&revoke_queued);
    expect(a,0,"queued old snapshot wrote after subject disable");expect(b,3,"queued revocation removed unrelated subject");
    slots.retain_subjects({});expect(b,0,"empty live set retained subject");
    target_association();contracts();full_roster(slots);
    slots.clear();need(restores==1,"teardown did not restore shared hook once");
    need(evrm::test_install_native_tool_slots(a.target,pose(a,"",.5f)),"legacy test installation seam failed");
    expect(a,3,"legacy test installation callback failed");evrm::test_clear_native_tool_slots();expect(a,0,"legacy test clear retained authority");
    std::cout<<"PASS 16 independent player equipment destinations; per-target hands/contact/disable/retention/expiry; account/generation guards; queued revocation; shared hook and legacy seams\n";return 0;
}catch(const std::exception&e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
