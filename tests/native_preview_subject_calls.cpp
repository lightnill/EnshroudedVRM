#include "../src/native_preview_subject_calls.hpp"
#include "../src/native_owner_profile.hpp"
#include <map>
#include <cstring>
#include <iostream>
#include <stdexcept>
using namespace evrm;using U=std::uint64_t;
void require(bool v,const char* why){if(!v)throw std::runtime_error(why);}
struct Memory {
    std::map<U,unsigned char> bytes;
    void write(U address,const void* p,std::size_t n){const auto* b=static_cast<const unsigned char*>(p);for(std::size_t i=0;i<n;++i)bytes[address+i]=b[i];}
    template<class T>void put(U p,T value){write(p,&value,sizeof value);}
    bool read(U p,void* out,std::size_t n){auto* b=static_cast<unsigned char*>(out);for(std::size_t i=0;i<n;++i){auto f=bytes.find(p+i);if(f==bytes.end())return false;b[i]=f->second;}return true;}
    static bool copy(void* raw,U p,void* out,std::size_t n)noexcept{return static_cast<Memory*>(raw)->read(p,out,n);}
};
int main(){try{
    Memory memory;NativePreviewSubjects subjects;NativePreviewSubjectThread thread;
    NativePreviewContext proposal{{0x140000000,0x200000,0x300000,0x400000,0x500000,0x500928,0x600000,0x700000,19,4},1,2,3,4,0x800000,0};
    const auto context=subjects.configure(proposal);const auto& actor=context.actor;const U app=0x900000,slot=actor.client+0x34408,packet=0xa00000;
    namespace o=evrm::native_owner_profile::ownerLayout;namespace e=evrm::native_owner_profile::ecsReadLayout;
    memory.put(actor.image+o::rootObjectRva+o::clientPointerOffset,actor.client);memory.put(actor.client+o::controllerPointerOffset,actor.controller);
    memory.put(actor.controller+o::controllerMainOwnerOffset,actor.main);memory.put(actor.main+e::mainExecutionOwnerOffset,actor.execution);
    memory.put(actor.selector+4,actor.id);memory.put(actor.entity,static_cast<U>(0x55aa55aa55aa55aaULL));
    memory.put(actor.entity+e::entityId,actor.id);memory.put(actor.entity+e::entityGeneration,actor.generation);
    memory.put(app+0xd0,actor.client+0x730);std::array<unsigned char,0x8d4> data{};memory.write(slot,data.data(),data.size());
    NativePreviewSubjectCalls calls(subjects,{&memory,&Memory::copy},thread,actor.image);
    unsigned requests=0,copies=0,renders=0,allocations=0,executions=0,publications=0;
    auto publish_copy=[&](U destination,U source,U count)->U{require(destination==slot&&source==99&&count==0x8d0,"publication copy arguments changed");++publications;return destination;};
    auto request=[&](U a,U p,U color){require(a==app&&p==99&&color==88,"request arguments changed");++requests;unsigned char flag=0;require(memory.read(slot+0x8d0,&flag,1),"flag unreadable");if(!flag){require(calls.accepted_copy(publish_copy,slot,p,0x8d0)==slot,"publication copy return changed");memory.put(slot+0x8d0,static_cast<unsigned char>(1));}};
    auto copy=[&](U d,U s,U n)->U{require(d==packet&&s==slot&&n==0x8d4,"copy arguments changed");++copies;std::array<unsigned char,0x8d4> bytes{};require(memory.read(s,bytes.data(),n),"source unreadable");memory.write(d,bytes.data(),n);return d;};
    auto allocate=[&](U a,U b,U c)->U{require(a==10&&b==20&&c==30,"allocator arguments changed");++allocations;return 0xb00000;};
    NativePreviewStamp observed;
    auto execute=[&](U a,U b,U c,U d){require(a==11&&b==22&&c==0xb00000&&d==44,"worker arguments changed");++executions;observed=thread.working;};
    auto render=[&](U a,U b,U c,U d,U p){require(a==context.renderer&&b==12&&c==13&&d==14&&p==packet,"renderer arguments changed");++renders;require(calls.allocate(allocate,10,20,30)==0xb00000,"allocator return changed");};
    calls.request(request,app,99,88,actor.image+0xb9eaf5);
    require(calls.copy(copy,packet,slot,0x8d4)==packet,"copy return changed");
    calls.render(render,context.renderer,12,13,14,packet,actor.image+0x857d19);
    calls.execute(execute,11,22,0xb00000,44);
    require(observed.local(),"actual native forwarding path did not carry local subject");
    require(!thread.rendering.request&&!thread.working.request,"scoped subject leaked after original returned");
    require(requests==1&&copies==1&&renders==1&&allocations==1&&executions==1&&publications==1,"original forwarding duplicated");
    // Native first-writer-wins: rejected survivor request must preserve local.
    calls.request(request,app,99,88,actor.image+0xc37998);calls.copy(copy,packet,slot,0x8d4);
    calls.render(render,context.renderer,12,13,14,packet,actor.image+0x857d19);calls.execute(execute,11,22,0xb00000,44);
    require(observed.local(),"rejected competing request relabeled the accepted slot");
    memory.put(slot+0x8d0,static_cast<unsigned char>(0));calls.request(request,app,99,88,actor.image+0xc37998);
    calls.copy(copy,packet,slot,0x8d4);calls.render(render,context.renderer,12,13,14,packet,actor.image+0x857d19);calls.execute(execute,11,22,0xb00000,44);
    require(observed.subject==NativePreviewSubject::other&&!observed.local(),"accepted survivor inherited local authority");
    // Reset, publish unknown, same bytes: no appearance/image inference.
    memory.put(slot+0x8d0,static_cast<unsigned char>(0));calls.request(request,app,99,88,actor.image+0x12345);
    calls.copy(copy,packet,slot,0x8d4);calls.render(render,context.renderer,12,13,14,packet,actor.image+0x857d19);calls.execute(execute,11,22,0xb00000,44);
    require(!observed.local(),"unknown caller with identical payload became local");
    // A native copy interleaved with a republish must not inherit an old tag.
    memory.put(slot+0x8d0,static_cast<unsigned char>(0));calls.request(request,app,99,88,actor.image+0xb9eaf5);
    auto interleaved=[&](U d,U s,U n)->U{auto result=copy(d,s,n);memory.put(slot+0x8d0,static_cast<unsigned char>(0));calls.request(request,app,99,88,actor.image+0xc37998);return result;};
    calls.copy(interleaved,packet,slot,0x8d4);calls.render(render,context.renderer,12,13,14,packet,actor.image+0x857d19);calls.execute(execute,11,22,0xb00000,44);
    require(!observed.request,"copy crossing reset/republish retained prior local label");
    memory.put(slot+0x8d0,static_cast<unsigned char>(0));memory.put(actor.selector+4,static_cast<std::uint32_t>(20));calls.request(request,app,99,88,actor.image+0xb9eaf5);
    calls.copy(copy,packet,slot,0x8d4);calls.render(render,context.renderer,12,13,14,packet,actor.image+0x857d19);calls.execute(execute,11,22,0xb00000,44);
    require(!observed.local(),"changed native local selector retained old actor proposal");
    memory.put(actor.selector+4,actor.id);memory.put(slot+0x8d0,static_cast<unsigned char>(0));calls.request(request,app,99,88,actor.image+0xb9eaf5);
    calls.copy(copy,packet,slot,0x8d4);
    bool threw=false;try{calls.render([&](U,U,U,U,U){require(thread.rendering.local(),"local renderer scope unavailable before throw");throw std::runtime_error("owned original throw");},context.renderer,12,13,14,packet,actor.image+0x857d19);}catch(const std::runtime_error&){threw=true;}
    require(threw&&!thread.rendering.request&&!thread.packet&&!thread.copied.request,"throwing renderer leaked a scope or reusable packet lease");
    calls.copy(copy,packet,slot,0x8d4);calls.render(render,context.renderer,12,13,14,packet,actor.image+0x857d19);
    threw=false;try{calls.execute([&](U,U,U,U){require(thread.working.local(),"local worker scope unavailable before throw");throw std::runtime_error("owned worker throw");},11,22,0xb00000,44);}catch(const std::runtime_error&){threw=true;}
    require(threw&&!thread.working.request,"throwing original worker leaked local authority");
    calls.execute(execute,11,22,0xb00000,44);require(!observed.request,"throwing original worker left replay authority");
    // The native reset may occur after an entry observer read pending=1 but
    // before ced430 checks it. Only its actual accepted copy proves writing.
    calls.request([&](U a,U p,U color){memory.put(slot+0x8d0,static_cast<unsigned char>(0));request(a,p,color);},app,99,88,actor.image+0xc37998);
    calls.copy(copy,packet,slot,0x8d4);calls.render(render,context.renderer,12,13,14,packet,actor.image+0x857d19);calls.execute(execute,11,22,0xb00000,44);
    require(!observed.local(),"native reset between entry observation and accepted copy relabeled a survivor as local");
    memory.put(slot+0x8d0,static_cast<unsigned char>(0));threw=false;
    try{calls.request([&](U,U p,U){calls.accepted_copy([](U,U,U)->U{throw std::runtime_error("owned publication throw");},slot,p,0x8d0);},app,99,88,actor.image+0xb9eaf5);}catch(const std::runtime_error&){threw=true;}
    require(threw&&!thread.proposed.source.epoch&&!thread.request_payload&&!subjects.copy_proposal(context.epoch).request,"throwing publication retained a proposal or writer permit");
    calls.request(request,app,99,88,actor.image+0xb9eaf5);calls.copy(copy,packet,slot,0x8d4);calls.render(render,context.renderer,12,13,14,packet,actor.image+0x857d19);calls.execute(execute,11,22,0xb00000,44);
    require(observed.local(),"throwing publication left the writer count or request scope stuck");
    const auto old_world=observed;subjects.allocated(0xc00000,old_world);unsigned destroyed=0,created=0;
    require(calls.main_destroy([&](U main,U flags)->U{require(main==actor.main&&flags==7,"main destructor arguments changed");++destroyed;require(!subjects.current(old_world)&&!subjects.source().epoch,"old preview authority survived inside native main destruction");return main;},actor.main,7)==actor.main,"main destructor return changed");
    require(!subjects.consume(0xc00000).request&&destroyed==1,"native destruction retained queued old-world authority or forwarded twice");
    require(calls.main_create([&](U main)->U{require(main==actor.main,"main constructor argument changed");++created;require(!subjects.configure(proposal).epoch,"new actor proposal admitted inside native construction");return main;},actor.main)==actor.main&&created==1,"main constructor return/count changed");
    const auto next=subjects.configure(proposal);require(next.epoch!=old_world.source.epoch,"same-address native re-construction retained old cohort");
    threw=false;try{calls.main_create([](U)->U{throw std::runtime_error("owned constructor throw");},actor.main);}catch(const std::runtime_error&){threw=true;}
    require(threw&&subjects.configure(proposal).epoch,"throwing native constructor left lifecycle blocked");
    std::cout<<"PASS actual request/copy/render/allocation/worker forwarding, rejected/unknown/competing writers and actor change\n";return 0;
}catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}}
