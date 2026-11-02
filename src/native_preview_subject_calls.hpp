#pragma once
#include "native_preview_subject.hpp"
#include "native_preview_handoff_calls.hpp"
#include "native_owner_profile.hpp"
namespace evrm {
class NativePreviewSubjectCalls {
    using U=std::uint64_t;
    NativePreviewSubjects& subjects_;NativePreviewHandoffRead read_;NativePreviewSubjectThread& thread_;U base_;
    static U add(U p,U n)noexcept{return p>=0x10000&&p<0x800000000000ULL&&n<0x800000000000ULL-p?p+n:0;}
    template<class T>bool scalar(U p,T& value)const noexcept {
        return p>=0x10000&&p<0x800000000000ULL&&sizeof(T)<=0x800000000000ULL-p&&read_.copy&&read_.copy(read_.context,p,&value,sizeof value);
    }
    bool fresh(const NativePreviewActor& actor)const noexcept {
        namespace o=native_owner_profile::ownerLayout;namespace e=native_owner_profile::ecsReadLayout;
        U pointer=0;std::uint32_t id=0;std::array<std::uint32_t,2> header{};
        return actor.valid()&&actor.registry==add(actor.execution,e::executionRegistryOwnerOffset)&&
            scalar(add(add(actor.image,o::rootObjectRva),o::clientPointerOffset),pointer)&&pointer==actor.client&&
            scalar(add(actor.client,o::controllerPointerOffset),pointer)&&pointer==actor.controller&&
            scalar(add(actor.controller,o::controllerMainOwnerOffset),pointer)&&pointer==actor.main&&
            scalar(add(actor.main,e::mainExecutionOwnerOffset),pointer)&&pointer==actor.execution&&
            scalar(add(actor.selector,4),id)&&id==actor.id&&scalar(add(actor.entity,e::entityId),header)&&header[0]==actor.id&&header[1]==actor.generation;
    }
    NativePreviewSubject subject(U caller)const noexcept {
        if(caller<base_)return NativePreviewSubject::unresolved;
        switch(caller-base_){case 0xbadb95:return NativePreviewSubject::local;
            case 0xbf150f:case 0xc0e001:case 0xc47438:case 0xc6cda9:case 0xc70e65:return NativePreviewSubject::other;
            default:return NativePreviewSubject::unresolved;}
    }
    struct Scope {
        NativePreviewStamp& destination;NativePreviewStamp previous;
        Scope(NativePreviewStamp& slot,const NativePreviewStamp& value)noexcept:destination(slot),previous(slot){slot=value;}
        ~Scope(){destination=previous;}
    };
    struct PayloadScope {U& slot;U previous;PayloadScope(U& target,U value)noexcept:slot(target),previous(target){slot=value;}~PayloadScope(){slot=previous;}};
    struct LifetimeScope {NativePreviewSubjects& subjects;explicit LifetimeScope(NativePreviewSubjects& value)noexcept:subjects(value){subjects.begin_lifetime();}~LifetimeScope(){subjects.end_lifetime();}};
public:
    NativePreviewSubjectCalls(NativePreviewSubjects& s,NativePreviewHandoffRead r,NativePreviewSubjectThread& t,U base)noexcept:
        subjects_(s),read_(r),thread_(t),base_(base){}
    template<class F>void request(F&& original,U app,U payload,U color,U caller){
        const auto source=subjects_.source();U ui=0;NativePreviewStamp proposed;
        if(source.epoch&&scalar(add(app,0xd0),ui)&&ui==add(source.actor.client,0x730))proposed={subject(caller),source,0};
        Scope scope(thread_.proposed,proposed);PayloadScope source_scope(thread_.request_payload,payload);
        original(app,payload,color);
    }
    template<class F>U copy(F&& original,U destination,U source,U count){
        thread_.packet=0;thread_.copied={};const auto context=subjects_.source();unsigned char before=0;
        const bool ours=context.epoch&&source==add(context.actor.client,0x34428)&&count==0x8d4&&scalar(add(source,0x8d0),before)&&before==1;
        auto proposal=ours?subjects_.copy_proposal(context.epoch):NativePreviewStamp{};
        if(proposal.local()&&!fresh(context.actor))proposal={};
        const auto result=original(destination,source,count);
        unsigned char after=0,packet_valid=0;
        if(proposal.request&&scalar(add(source,0x8d0),after)&&after==1&&scalar(add(destination,0x8d0),packet_valid)&&packet_valid==1&&
            (!proposal.local()||fresh(context.actor))){
            thread_.copied=subjects_.copied(proposal);if(thread_.copied.request)thread_.packet=destination;
        }
        return result;
    }
    template<class F>U accepted_copy(F&& original,U destination,U source,U count){
        const auto context=subjects_.source();
        if(!context.epoch||destination!=add(context.actor.client,0x34428))return original(destination,source,count);
        const auto ticket=subjects_.begin_request(context.epoch);
        const auto proposal=thread_.proposed;
        auto kind=NativePreviewSubject::unresolved;
        if(count==0x8d0&&thread_.request_payload==source&&proposal.source.epoch==context.epoch&&proposal.source.same(context))kind=proposal.subject;
        U result=0;
        try{result=original(destination,source,count);}catch(...){subjects_.finish_request(ticket,NativePreviewSubject::unresolved,false);throw;}
        if(kind==NativePreviewSubject::local&&!fresh(context.actor))kind=NativePreviewSubject::unresolved;
        // The following original instruction publishes valid=1. The consumer
        // also requires that native flag, so it cannot use unfinished bytes.
        subjects_.finish_request(ticket,kind);return result;
    }
    template<class F>U main_create(F&& original,U main){LifetimeScope scope(subjects_);return original(main);}
    template<class F>U main_destroy(F&& original,U main,U flags){LifetimeScope scope(subjects_);return original(main,flags);}
    template<class F>void render(F&& original,U renderer,U view,U frame,U target,U payload,U caller){
        const auto copied=thread_.copied;const auto packet=thread_.packet;thread_.copied={};thread_.packet=0;
        const bool ours=packet&&packet==payload&&renderer==copied.source.renderer&&caller>=base_&&caller-base_==0x861d49&&subjects_.current(copied);
        Scope scope(thread_.rendering,ours?copied:NativePreviewStamp{});original(renderer,view,frame,target,payload);
    }
    template<class F>U allocate(F&& original,U context,U flags,U label){
        const auto stamp=thread_.rendering;const auto result=original(context,flags,label);subjects_.allocated(result,stamp);return result;
    }
    template<class F>void execute(F&& original,U backend,U command,U task,U hints){
        Scope scope(thread_.working,subjects_.consume(task));original(backend,command,task,hints);
    }
};
}
