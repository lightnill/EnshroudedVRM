#include "../src/fallback_diagnostics.hpp"
#include <map>
#include <string>
#include <cstdio>
#include <cstdlib>
void need(bool value,const char* reason){if(!value){std::fprintf(stderr,"FAIL %s\n",reason);std::exit(1);}}
// Keep the old diagnostic path runnable to demonstrate a behavioral RED.
template<class T>auto record_detail(T& value,const evrm::FallbackCaptureMetadata& facts,const char* detail,int)
    ->decltype(value.record(facts,"view-pose","No catalog-compatible player parts in capture",detail),void()){
    value.record(facts,"view-pose","No catalog-compatible player parts in capture",detail);
}
template<class T>void record_detail(T& value,const evrm::FallbackCaptureMetadata& facts,const char*,long){value.record(facts,"view-pose","No catalog-compatible player parts in capture");}
template<class T>auto detail_of(const T& value,int)->decltype(value.detail.data()){return value.detail.data();}
template<class T>const char* detail_of(const T&,long){return "";}
template<class T>auto capture_failure(T& value,std::uint64_t now,const char* detail,unsigned view,int)
    ->decltype(value.failure(now,1,"No catalog-compatible player parts in capture",detail,view)){
    return value.failure(now,1,"No catalog-compatible player parts in capture",detail,view);
}
template<class T>evrm::FallbackEvent capture_failure(T& value,std::uint64_t now,const char*,unsigned,long){return value.failure(now,1,"No catalog-compatible player parts in capture");}
int main(){
    evrm::FallbackEpisodes rare;
    for(unsigned n=0;n<7;++n){const auto start=1000+n*1500;const auto failed=rare.failure(start);
        need(failed.emit&&failed.episode==n+1&&failed.failures==1,"rare seventh episode was hidden");
        const auto recovered=rare.recovery(start+157);need(recovered.emit&&recovered.elapsed_ms==157,"recovery duration missing");
        need(!rare.recovery(start+158).emit,"duplicate recovery");}
    evrm::FallbackEpisodes sustained;unsigned emitted=0;
    for(unsigned now=1000;now<5000;now+=10)emitted+=sustained.failure(now).emit;
    const auto end=sustained.recovery(5000);need(emitted==4&&end.emit&&end.failures==400&&end.elapsed_ms==4000,"sustained gap flooded logs or lost counts");
    evrm::FallbackEpisodes flapping;unsigned records=0;
    for(unsigned now=1000;now<2000;now+=10){records+=flapping.failure(now).emit;records+=flapping.recovery(now+1).emit;}
    need(records==2&&flapping.episodes()==100,"flapping emitted per-frame logs or lost episode count");
    auto restarted=flapping.failure(5);need(restarted.emit&&restarted.elapsed_ms==0,"backward diagnostic clock failed");
    struct Buffer{struct {void* buffer=nullptr;}binding;};
    struct Snapshot{std::map<std::string,Buffer> buffers;bool asynchronous=false;std::uint64_t source_epoch=0,captured_milliseconds=0;};Snapshot view;
    bool named=false;try{(void)evrm::native_view_instance_source(view);}catch(const std::exception& error){named=std::string(error.what())=="Native view missing instances capture";}
    need(named,"missing instance capture retained generic map::at");view.buffers["instances"].binding.buffer=reinterpret_cast<void*>(123);
    need(evrm::native_view_instance_source(view)==123,"diagnostic admission changed valid source identity");
    evrm::FallbackCaptureMetadata facts;view.buffers["camera"]={};view.asynchronous=true;view.source_epoch=17;view.captured_milliseconds=901;
    facts.observe(view,0);auto moved=std::move(view);view.buffers.clear();
    need(facts.roles==3&&facts.asynchronous&&facts.source_epoch==17&&facts.captured_milliseconds==901,
        "moved capture erased diagnostic facts");
    Snapshot child;child.buffers["camera"]={};child.buffers["skin"]={};child.source_epoch=18;child.captured_milliseconds=950;
    facts.observe(child,2);child.buffers.clear();
    need(facts.roles==6&&facts.view_index==2&&!facts.asynchronous&&facts.source_epoch==18&&facts.captured_milliseconds==950,
        "failed child view diagnostic used the root capture");
    evrm::FallbackPoseDiagnostics partial;partial.record(facts,"view-admission","missing instances");partial.retained_views=1;
    facts={};need(partial.failed()&&partial.failures[0].capture.roles==6&&partial.failures[0].capture.view_index==2&&
        std::string(partial.failures[0].phase)=="view-admission"&&std::string(partial.failures[0].reason.data())=="missing instances"&&partial.retained_views==1,
        "surviving/retained publication erased child failure context");
    evrm::FallbackEpisodes changed;need(changed.failure(1000,1,"cause A").emit,"changed-reason first record missing");
    need(!changed.failure(1010,2,"cause B").emit,"changed reason bypassed rate limit");const auto changed_end=changed.recovery(1020);
    need(changed_end.emit&&changed_end.reason_mask==3&&changed_end.details.count==2&&changed_end.suppressed_failures==1&&changed_end.total_failures==2,
        "recovery discarded a changed reason or suppressed failure count");
    changed.failure(1030,4,"cause C");changed.failure(1031,4,"cause C");need(!changed.recovery(1032).emit,"suppressed episode flooded recovery");
    const auto later=changed.failure(2100,8,"cause D");need(later.emit&&later.reason_mask==12&&later.details.count==2&&later.suppressed_failures==3&&later.total_failures==5,
        "later summary discarded entirely suppressed episode failures/reasons");
    need(evrm::fallback_event_summary(changed_end).find("cause B")!=std::string::npos,"changed reason was retained but omitted from formatted recovery");
    evrm::FallbackEpisodes bounded;for(unsigned n=0;n<6;++n)bounded.failure(4000+n,1ull<<n,std::to_string(n).c_str());
    const auto bounded_end=bounded.recovery(4010);need(bounded_end.details.count==4&&bounded_end.details.overflow&&bounded_end.reason_mask==63,"reason history was unbounded or silently truncated");
    evrm::FallbackDecision short_circuit;unsigned extra_checks=0;
    const bool declined=short_circuit.check(false,evrm::FallbackGate::Eligible)&&short_circuit.check(++extra_checks!=0,evrm::FallbackGate::Owner);
    need(!declined&&!extra_checks&&short_circuit.failed==std::uint64_t(evrm::FallbackGate::Eligible),"diagnostic checks changed short-circuit admission");
    evrm::FallbackPoseDiagnostics capture_details;
    std::string detail="owner-filter=1\nowner-models=2";
    record_detail(capture_details,facts,detail.c_str(),0);detail.assign(4000,'x');
    need(std::string(detail_of(capture_details.failures[0],0))=="owner-filter=1 owner-models=2","failure detail was lost, aliased, or not sanitized");
    record_detail(capture_details,facts,detail.c_str(),0);
    need(std::string(detail_of(capture_details.failures[1],0))==std::string(383,'x'),"failure detail is not bounded/terminated");
    capture_details.record(facts,"view-admission","missing instances");
    need(std::string(detail_of(capture_details.failures[2],0)).empty(),"an unrelated failed view inherited stale diagnostic detail");
    need(std::string(capture_details.failures[0].reason.data())=="No catalog-compatible player parts in capture","detail replaced the stable failure reason");
    record_detail(capture_details,facts,nullptr,0);record_detail(capture_details,facts,"overflow",0);
    need(capture_details.count==4&&std::string(detail_of(capture_details.failures[3],0)).empty(),"detail overflow changed the view budget or null detail");
    evrm::FallbackEpisodes capture_episodes;
    need(capture_failure(capture_episodes,5000,"selected-records=0",0,0).emit,"capture detail bypassed first failure");
    need(!capture_failure(capture_episodes,5010,"catalog-misses=3",1,0).emit,"second view bypassed the shared rate limit");
    capture_failure(capture_episodes,5011,"catalog-misses=4",1,0);
    auto capture_recovered=capture_episodes.recovery(5020);
    auto capture_summary=evrm::fallback_event_summary(capture_recovered);
    need(capture_recovered.emit&&capture_summary.find("view=0 selected-records=0")!=std::string::npos&&
        capture_summary.find("view=1 catalog-misses=4")!=std::string::npos&&capture_summary.find("catalog-misses=3")==std::string::npos,
        "recovery lost rate-suppressed/multiple-view counts or failed to keep the latest per view");
    need(!capture_failure(capture_episodes,5030,"submitted-records=7",2,0).emit,"brief follow-up gap bypassed pacing");
    need(!capture_episodes.recovery(5040).emit,"brief follow-up recovery bypassed pacing");
    const auto next_capture=capture_failure(capture_episodes,6100,"selected-records=1",0,0);
    capture_summary=evrm::fallback_event_summary(next_capture);
    need(next_capture.emit&&capture_summary.find("view=2 submitted-records=7")!=std::string::npos&&capture_summary.find("catalog-misses=4")==std::string::npos,
        "suppressed episode detail was lost or a logged recovery failed to clear old detail");
    evrm::FallbackEpisodes submissions;
    for(unsigned n=0;n<7;++n){evrm::FallbackDecision decision;
        bool ready=decision.check(true,evrm::FallbackGate::Shape);if(ready)ready=decision.check(false,evrm::FallbackGate::CompetingOwnership);
        need(!ready&&decision.failed==std::uint64_t(evrm::FallbackGate::CompetingOwnership),"actual veto did not retain competing ownership cause");
        const auto veto=submissions.failure(10000+n*1500,decision.failed);
        need(veto.emit&&veto.reason_mask==decision.failed&&evrm::fallback_gate_names(veto.reason_mask)=="competing-ownership","later isolated decline omitted actual check");
        submissions.recovery(10100+n*1500);}
    bool captured=false;{auto outcome=evrm::track_fallback_outcome([&](bool,const char*){captured=partial.failed()&&std::string(partial.failures[1].reason.data())=="current exception";});
        try{throw std::runtime_error("current exception");}catch(const std::exception& error){partial.record(facts,"sync-capture",error.what());}}
    need(captured,"unwind diagnostic ran before current exception was retained");
    std::puts("PASS fallback rate/count/reason retention, actual veto masks, moved/root/child/retained capture facts and exception context");
}
