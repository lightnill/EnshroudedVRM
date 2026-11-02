#pragma once
#include "native_preview_handoff.hpp"
#include <cstddef>
#include <cstring>
namespace evrm {
struct NativePreviewHandoffRead {
    void* context=nullptr;
    bool(*copy)(void*,std::uint64_t,void*,std::size_t)noexcept=nullptr;
};
class NativePreviewHandoffCalls {
    using U=std::uint64_t;
    NativePreviewHandoff& history_;NativePreviewHandoffRead read_;
    NativePreviewHandoffThread& thread_;U base_;
    template<class T>bool scalar(U pointer,T& value)const noexcept{return bytes(pointer,&value,sizeof value);}
    bool bytes(U pointer,void* output,std::size_t count)const noexcept {
        return read_.copy&&pointer>=0x10000&&pointer<0x800000000000ULL&&count<=0x800000000000ULL-pointer&&
            read_.copy(read_.context,pointer,output,count);
    }
    static U add(U pointer,U offset)noexcept{return pointer<0x800000000000ULL&&offset<0x800000000000ULL-pointer?pointer+offset:0;}
    unsigned origin(U caller)const noexcept {
        if(caller<base_)return 0;
        switch(caller-base_){case 0xbadb95:return 1;case 0xbf150f:return 2;case 0xc0e001:return 3;
            case 0xc47438:return 4;case 0xc6cda9:return 5;case 0xc70e65:return 6;default:return 0;}
    }
    struct Scope {
        NativePreviewHandoffLabel& slot;NativePreviewHandoffLabel previous;
        Scope(NativePreviewHandoffLabel& destination,NativePreviewHandoffLabel value)noexcept:slot(destination),previous(destination){slot=value;}
        ~Scope(){slot=previous;}
        Scope(const Scope&)=delete;Scope& operator=(const Scope&)=delete;
    };
    struct WorkerScope {
        NativePreviewHandoff& history;NativePreviewHandoffLabel label;Scope scope;
        WorkerScope(NativePreviewHandoff& owner,NativePreviewHandoffLabel& destination,NativePreviewHandoffLabel value)noexcept:
            history(owner),label(value),scope(destination,value){}
        ~WorkerScope(){history.finish(label);}
    };
public:
    NativePreviewHandoffCalls(NativePreviewHandoff& history,NativePreviewHandoffRead read,NativePreviewHandoffThread& thread,U base)noexcept:
        history_(history),read_(read),thread_(thread),base_(base){}
    template<class F>void request(F&& original,U app,U payload,U color,U caller,U tick){
        const auto source=history_.source();U ui=0;unsigned char before=0;
        if(!source.epoch&&history_.active())history_.unavailable(source);
        const bool app_read=source.epoch&&scalar(add(app,0xd0),ui);
        const bool ours=app_read&&ui==add(source.client,0x730);
        const auto slot=add(ui,0x33cf8);
        const bool valid=ours&&scalar(add(slot,0x8d0),before)&&before<=1;
        if((source.epoch&&!app_read)||(ours&&(!valid||!before)))history_.unavailable(source);
        original(app,payload,color); // Exactly once, including unreadable diagnostics.
        if(!ours)return;
        unsigned char after=0;
        if(!valid||!scalar(add(slot,0x8d0),after)||after>1){history_.unavailable(source);return;}
        history_.request(source,origin(caller),!before&&after==1,tick);
    }
    template<class F>void render(F&& original,U renderer,U view,U frame,U target,U payload,U caller,U tick){
        const auto source=history_.source();const bool supported=source.epoch&&renderer==source.renderer&&caller>=base_&&caller-base_==0x861d49;
        bool copied=false;
        if(supported){std::array<unsigned char,0x8d4> expected{},packet{};
            copied=bytes(add(source.client,0x34428),expected.data(),expected.size())&&bytes(payload,packet.data(),packet.size())&&
                expected[0x8d0]==1&&packet[0x8d0]==1&&expected==packet;}
        const auto label=history_.render(source,supported,copied,tick);
        Scope scope(thread_.rendering,label);original(renderer,view,frame,target,payload);
    }
    template<class F>void construct(F&& original,U builder,U context,U label,U flags,U dep,U tail,U tick){
        const auto rendering=thread_.rendering;
        original(builder,context,label,flags,dep,tail);
        U task=0;if(scalar(add(builder,0x10),task))history_.enqueue(task,rendering,tick);
    }
    template<class F>void execute(F&& original,U backend,U command,U task,U hints,U tick){
        const auto label=history_.execute(task,tick);
        WorkerScope scope(history_,thread_.working,label);original(backend,command,task,hints);
    }
};
}
