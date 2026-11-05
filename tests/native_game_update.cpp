// Actual ownership and preview forwarding against independent updated layouts.
#include "../src/native_preview_subject_calls.hpp"
#define main original_native_owner_census_main
#include "native_owner_census.cpp"
#undef main
#include "../src/local_voice_observation.hpp"
#include "../src/debug_report_log.hpp"
#include "game_26320373_fixture.hpp"
#include "game_26404966_fixture.hpp"

int main(int argc,char** argv){try{
    SetErrorMode(GetErrorMode()|0x8003);need(argc==2,"scenario required");const std::string mode=argv[1];
    namespace game=game_26430625_fixture;
    if(mode=="report"){
        const auto valid=evrm::debug_report_safe_event("preview-ownership.sample stage=1 path-mask=0 group-0=72885");
        need(valid.find("group-0=72885")!=std::string::npos,"new last code ordinal was dropped from private report");
        need(evrm::debug_report_safe_event("preview-ownership.sample stage=1 path-mask=0 group-0=72886").empty(),"out-of-table ordinal entered report");
        need(evrm::debug_report_safe_event("preview-ownership.sample stage=1 path-mask=0 group-0=0x140000000").empty(),"raw code address entered report");
    }else if(mode=="voice"){
        evrm::VoiceSourceIdentity source;source.file_sha256=game::image_sha256;
        source.timestamp=game::image_timestamp;source.image_bytes=static_cast<std::uint32_t>(game::image_size);
        source.encoder_prologue={{0x48,0x83,0xec,0x38,0x44,0x8b,0x49,0x08,0x4c,0x8b,0xd2,0x49,0x8b,0x40,0x08,0x33}};
        source.encoder_call={{0xe8,0xbf,0xab,0xc6,0xff}};
        need(evrm::local_voice_identity_supported(source),"new game voice identity declined");
        source.encoder_call={{0xe8,0x9f,0xab,0xc6,0xff}};
        need(!evrm::local_voice_identity_supported(source),"previous voice call admitted");
        source.encoder_call={{0xe8,0xbf,0xab,0xc6,0xff}};
        source.file_sha256=game_26404966_fixture::image_sha256;source.timestamp=game_26404966_fixture::image_timestamp;
        source.image_bytes=static_cast<std::uint32_t>(game_26404966_fixture::image_size);
        need(!evrm::local_voice_identity_supported(source),"previous voice image admitted");
    }else{
        Fixture f;evrm::NativeOwnerCandidate owner;std::string why;evrm::NativeOwnerLimits limits;limits.max_milliseconds=40;
        // A stale controller read must not succeed by finding another valid
        // pointer in the old field. The real new field is populated separately.
        put<U>(f.client,0x35280,0);
        if(mode=="world"||mode=="mixed-bindings"||mode=="old-image"||mode=="changed-image"||mode=="old-global"||mode=="old-vtable"||mode=="old-descriptor"){
            if(mode=="mixed-bindings"){
                f.reg(game::systems+game::normal_count,std::uint8_t(0));
                f.reg(game::systems+0x1b10,std::uint8_t(6));
            }
            if(mode=="old-image"){
                f.identity.size=game_26404966_fixture::image_size;f.identity.timestamp=game_26404966_fixture::image_timestamp;
                f.identity.sha256=game_26404966_fixture::image_sha256;
                put(f.image,136,f.identity.timestamp);put<std::uint32_t>(f.image,208,static_cast<std::uint32_t>(f.identity.size));
                put<std::uint32_t>(f.image,224,static_cast<std::uint32_t>(f.identity.size));
            }
            if(mode=="old-global"){
                // A complete valid-looking owner at the previous global must
                // not rescue an invalid owner at this game's exact root.
                std::memcpy(f.image.data()+game_26320373_fixture::root,f.image.data()+game::root,0x290);
                put<U>(f.image,game::root,0);
            }
            if(mode=="old-vtable")put(f.client,0,f.identity.base+game_26320373_fixture::client_vtable);
            if(mode=="old-descriptor"){
                std::memcpy(f.image.data()+game_26320373_fixture::equipment_descriptor,f.image.data()+game::equipment_descriptor,0x200);
                f.reg(game::systems,f.identity.base+game_26320373_fixture::equipment_descriptor);
            }
            if(mode=="changed-image")f.identity.sha256[0]^=1;
            const bool accepted=evrm::capture_native_owner(f.memory,f.identity,owner,why,limits);
            if(mode=="world"){
                need(accepted,("new world layout declined: "+why).c_str());
                need(owner.controller==address(f.controller)&&owner.entity_manager==address(f.execution)+0xe179c0&&owner.entity_id==1&&owner.models.size()==1,
                     "new layout resolved wrong player or allocation");
                need(evrm::revalidate_native_owner(f.memory,owner,why,limits),"new full owner guards did not revalidate");
            }else{
                need(!accepted&&owner.guards.empty()&&owner.models.empty(),"mixed/changed game data retained ownership");
                if(mode=="old-image"||mode=="changed-image")need(why=="unsupported image SHA/timestamp/size","other image reached native layout traversal");
            }
        }else if(mode=="preview"){
            using namespace evrm;
            NativePreviewSubjects subjects;NativePreviewSubjectThread thread;
            const U app=address(f.ordinary),packet=address(f.player),task=0xb00000;
            // The existing owned builder's main token is renamed along with
            // its entry point by the include above.
            NativePreviewContext proposed{{f.identity.base,address(f.client),address(f.controller),address(f.original_native_owner_census_main),address(f.execution),f.registry,address(f.local_player),address(f.entities),1,1},
                                           1,2,3,4,0x800000,0};
            const auto context=subjects.configure(proposed);const U slot=address(f.client)+game::client_slot;
            put(f.ordinary,0xd0,address(f.client)+0x730);
            // Old validity address overlaps only unused payload bytes. It is
            // deliberately nonzero; only the new native flag may be consumed.
            f.client[0x34408+0x8d0]=0x7f;
            auto read=[](void* memory,U at,void* dst,std::size_t n)noexcept{return static_cast<Memory*>(memory)->read(at,dst,n);};
            NativePreviewSubjectCalls calls(subjects,{&f.memory,read},thread,f.identity.base);
            auto memcpy_owned=[](U dst,U src,U n)->U{std::memcpy(reinterpret_cast<void*>(dst),reinterpret_cast<const void*>(src),std::size_t(n));return dst;};
            Bytes payload(0x8d0);const U source=address(payload);
            auto request=[&](U a,U p,U color){need(a==app&&p==source&&color==88,"native request forwarding changed");
                if(!f.client[game::client_slot+0x8d0]){calls.accepted_copy(memcpy_owned,slot,p,0x8d0);f.client[game::client_slot+0x8d0]=1;}};
            NativePreviewStamp seen;unsigned forwarded=0;
            auto render=[&](U a,U,U,U,U p){need(a==context.renderer&&p==packet,"native render forwarding changed");
                calls.allocate([&](U,U,U)->U{++forwarded;return task;},10,20,30);};
            auto execute=[&](U,U,U,U){seen=thread.working;};
            auto consume=[&]{calls.copy(memcpy_owned,packet,slot,0x8d4);calls.render(render,context.renderer,12,13,14,packet,f.identity.base+game::render_return);calls.execute(execute,11,22,task,44);};
            calls.request(request,app,source,88,f.identity.base+game::inventory_return);consume();
            need(seen.local()&&forwarded==1,"new accepted-copy/render/worker path lost local inventory identity");
            f.client[game::client_slot+0x8d0]=0;calls.request(request,app,source,88,f.identity.base+game::survivor_return);consume();
            need(seen.subject==NativePreviewSubject::other&&!seen.local(),"new survivor preview acquired local identity");
            f.client[game::client_slot+0x8d0]=0;calls.request(request,app,source,88,f.identity.base+game_26320373_fixture::inventory_return);consume();
            need(!seen.local(),"old game caller acquired new game authority");
            for(const U other:{0xbf15cfULL,0xc0e0c1ULL,0xc474f8ULL,0xc6ce69ULL,0xc70f25ULL}){
                f.client[game::client_slot+0x8d0]=0;calls.request(request,app,source,88,f.identity.base+other);consume();
                need(seen.subject==NativePreviewSubject::other&&!seen.local(),"noninventory producer acquired local identity");
            }
        }else need(false,"unknown scenario");
    }
    std::printf("PASS game26430625 %s\n",mode.c_str());return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL game26430625: %s\n",e.what());return 1;}}
