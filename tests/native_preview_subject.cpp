#include "../src/native_preview_subject.hpp"
#include <iostream>
#include <stdexcept>
#include <functional>
using namespace evrm;
void require(bool value,const char* reason){if(!value)throw std::runtime_error(reason);}
NativePreviewContext context(){return {{1,2,3,4,5,6,7,8,9,10},11,12,13,14,15,0};}
template<class Core>NativePreviewStamp publish(Core& s,const NativePreviewContext& c,NativePreviewSubject subject){
    const auto ticket=s.begin_request(c.epoch);s.finish_request(ticket,subject);return s.copied(s.copy_proposal(c.epoch));
}
struct GateMutex {inline static bool busy=false;inline static std::function<void()> interleave;
    bool try_lock(){if(interleave){auto call=std::move(interleave);interleave={};call();}return !busy;}void unlock()noexcept{}};
int main(){try{
    {
        BasicNativePreviewSubjects<GateMutex> core;auto c=core.configure(context());auto local=publish(core,c,NativePreviewSubject::local);
        core.allocated(100,local);core.allocated(101,local);GateMutex::busy=true;
        core.allocated(100,{});core.consume(999);
        require(core.current(local),"unrelated worker/unknown allocation contention revoked the entire preview cohort");
        require(core.source().epoch==c.epoch&&core.configure(context()).epoch==c.epoch,"unchanged source publication waited for or lost private task lock");
        GateMutex::busy=false;require(!core.consume(100).request&&core.consume(101).local(),"exact reused-key retirement lost unrelated queued authority");
        core.allocated(102,local);GateMutex::busy=true;require(!core.consume(102).request,"contended task leaked an unowned stamp");
        GateMutex::busy=false;require(!core.consume(102).request&&core.current(local),"contended worker retained replay authority or revoked unrelated work");
        const auto other=publish(core,c,NativePreviewSubject::other);core.allocated(201,local);
        GateMutex::interleave=[&]{core.allocated(202,other);};
        require(!core.consume(201).request&&core.consume(202).subject==NativePreviewSubject::other,"free-slot reuse changed the stamp after the worker reserved its old key");
        auto changed=context();++changed.actor.generation;
        GateMutex::interleave=[&]{core.begin_lifetime();core.end_lifetime();};
        require(!core.configure(changed).epoch&&!core.source().epoch,"source publication crossed a native lifecycle boundary");
    }
    NativePreviewSubjects s;auto c=s.configure(context());
    auto local=publish(s,c,NativePreviewSubject::local);
    require(local.local(),"accepted local publication was not carried through its actual copy");
    {
        const auto saved=local;s.allocated(800,saved);s.begin_lifetime();
        require(!s.current(saved)&&!s.source().epoch&&!s.configure(context()).epoch,"native world boundary retained old authority or admitted during construction/destruction");
        s.end_lifetime();c=s.configure(context());
        require(c.epoch!=saved.source.epoch&&c.world_lifetime!=saved.source.world_lifetime&&!s.consume(800).request,
            "identical addresses/IDs/generation and live Character allocation inherited an earlier world lifetime");
        local=publish(s,c,NativePreviewSubject::local);require(local.local(),"new world lifetime could not establish new local work");
        s.begin_lifetime();s.begin_lifetime();s.end_lifetime();require(!s.configure(context()).epoch,"nested lifecycle scope ended an ongoing native transition");
        s.end_lifetime();c=s.configure(context());local=publish(s,c,NativePreviewSubject::local);
    }
    {
        const auto older=s.begin_request(c.epoch),newer=s.begin_request(c.epoch);
        s.finish_request(newer,NativePreviewSubject::local);s.finish_request(older,NativePreviewSubject::other);
        require(!s.copied(s.copy_proposal(c.epoch)).request,"overlapping writers retained the last metadata finisher despite later native payload writes");
        local=publish(s,c,NativePreviewSubject::local);
    }
    s.allocated(100,local);auto pending=s.copy_proposal(c.epoch);
    auto writing=s.begin_request(c.epoch);
    require(!s.copied(pending).request,"copy overlapping native publication inherited the earlier local tag");
    s.finish_request(writing,NativePreviewSubject::other);auto other=s.copied(s.copy_proposal(c.epoch));
    require(other.subject==NativePreviewSubject::other&&!other.local(),"same source survivor became local");
    require(s.consume(100).local(),"later request relabeled or revoked an already captured local task");
    require(!s.consume(100).request,"task was consumed twice");
    s.allocated(101,other);local=publish(s,c,NativePreviewSubject::local);
    require(!s.consume(101).local(),"later local request relabeled an earlier survivor task");
    s.allocated(102,local);s.allocated(102,{});
    require(!s.consume(102).request,"unknown allocation reused a cancelled local task address");
    s.allocated(103,local);s.allocated(103,other);
    require(s.consume(103).subject==NativePreviewSubject::other,"same address retained its previous allocation label");
    auto before=s.copy_proposal(c.epoch);auto failed=s.begin_request(c.epoch);(void)failed;
    require(!s.copied(before).request&&!s.copy_proposal(c.epoch).request,"failed publication retained previous subject");
    s.finish_request(failed,NativePreviewSubject::unresolved,false);
    local=publish(s,c,NativePreviewSubject::local);s.allocated(104,local);
    auto changed=context();++changed.actor.generation;c=s.configure(changed);
    require(!s.current(local)&&!s.consume(104).request,"actor generation reused old queued authority");
    auto fresh=publish(s,c,NativePreviewSubject::local);require(fresh.local()&&!same_native_preview_local(fresh,local),"new actor shared old cohort");
    s.retire_buffer(999,c.buffer);require(s.current(fresh),"other device's equal buffer handle revoked this source");
    s.retire_owner(999);require(s.current(fresh),"unrelated backend retired this source");
    s.retire_buffer(c.device,c.buffer);require(!s.current(fresh),"destroyed source retained authority");
    c=s.configure(changed);fresh=publish(s,c,NativePreviewSubject::local);s.retire_owner(c.owner);
    require(!s.current(fresh),"retired backend retained authority");
    c=s.configure(context());local=publish(s,c,NativePreviewSubject::local);
    for(unsigned i=0;i<128;++i)s.allocated(1000+i,local);
    s.allocated(2000,local);require(!s.consume(2000).request,"full fixed task map invented authority");
    require(s.consume(1000).local(),"full task map lost a registered task");s.allocated(2001,local);require(s.consume(2001).local(),"consumed fixed slot was not reusable");
    for(unsigned i=1;i<128;++i)s.consume(1000+i);
    for(unsigned i=0;i<512;++i){s.allocated(3000+i,local);require(s.consume(3000+i).local(),"sequential native task lifetime exhausted fixed storage");}
    std::cout<<"PASS publication/copy overlap, deferred local/other work, allocation reuse, actor/source retirement\n";
    return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
