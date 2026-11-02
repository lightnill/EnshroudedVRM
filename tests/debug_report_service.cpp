#include "../src/debug_report_service.hpp"
#include "../src/native_preview_trace.hpp"
#include "../src/native_preview_handoff.hpp"
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <thread>
void need(bool value,const char* text){if(!value)throw std::runtime_error(text);}
bool done(evrm::DebugReportPhase phase){return phase==evrm::DebugReportPhase::Complete||phase==evrm::DebugReportPhase::Partial;}
template<class T>auto bind_source(T& service,std::uint64_t job,std::uint64_t source,int)->decltype(service.capture_started(job,source)){return service.capture_started(job,source);}
template<class T>bool bind_source(T& service,std::uint64_t job,std::uint64_t,long){return service.capture_started(job);}
template<class T>auto retire(T& service,std::uint64_t source,int)->decltype(service.capture_retired(source),void()){service.capture_retired(source);}
template<class T>void retire(T& service,std::uint64_t,long){service.capture_failed(service.status()->job,evrm::DebugReportCode::Stale);}
template<class T>auto sourced_pixels(T& service,std::uint64_t job,std::shared_ptr<const evrm::DebugReportImage> image,std::uint64_t source,int)->decltype(service.submit_pixels(job,image,source)){return service.submit_pixels(job,std::move(image),source);}
template<class T>bool sourced_pixels(T& service,std::uint64_t job,std::shared_ptr<const evrm::DebugReportImage> image,std::uint64_t,long){return service.submit_pixels(job,std::move(image));}
std::size_t count(const std::filesystem::path& directory){std::size_t result=0;if(std::filesystem::exists(directory))for(const auto& entry:std::filesystem::directory_iterator(directory))result+=entry.is_regular_file();return result;}
int main(int argc,char** argv){try{
    need(argc==2,"fixture folder required");const auto folder=std::filesystem::path(argv[1]);std::uint64_t now=10000;unsigned opened=0;
    auto& trace=evrm::native_preview_trace_history();trace.configure(0x140000000ULL,44302336,0xc0ffee,0x12345678);
    evrm::NativePreviewTraceSample trace_sample;trace_sample.path_mask=1;trace_sample.stack_depth=16;trace_sample.game_depth=1;trace_sample.groups[0]=27453;
    need(trace.record(trace.ticket(0xc0ffee,evrm::NativePreviewTraceStage::Bindings,now),trace_sample),"report ancestry fixture did not record");
    auto& handoff=evrm::native_preview_handoff_history();const auto handoff_source=handoff.configure(1,2,3,0xc0ffee);
    handoff.request(handoff_source,1,true,now);handoff.enqueue(123,handoff.render(handoff_source,true,true,now),now);
    need(handoff.gpu(0xc0ffee,handoff.execute(123,now),5,now),"report handoff fixture did not record");
    evrm::DebugReportOptions options;options.directory=folder/"Reports";options.background=false;options.mod_version="0.3.0-report-test";options.game_sha256=std::string(64,'2');options.nonce="01020304";
    options.clock=[&]{return now;};options.utc=[]{return "2026-11-01T23:30:00Z";};options.system=[](){evrm::DebugReportSystem result;result.cpu_model="AMD Ryzen 9";result.logical_processors=16;result.ram_total=32ull<<30;return result;};
    options.sources=[](std::uint64_t,std::uint64_t request,std::uint64_t collected){evrm::DebugReportData result;result.request_ms=request;result.collected_ms=collected;result.scene.location={true,true,{3507.75,1047,1981.75},collected};return result;};
    options.files=[](std::string& graphics,std::string& engine){graphics=R"({"graphics":{"fogQuality":3,"secret":"SECRET_CANARY"}})";engine="[E 00:00] VK_ERROR_DEVICE_LOST SECRET_CANARY\n";};
    options.open_folder=[&](const std::filesystem::path& path){need(path==options.directory,"wrong folder opened");++opened;};
    evrm::DebugReportService service(options);
    const auto first=service.request(false);need(first!=0,"text report request failed");service.pump();
    need(done(service.status()->phase)&&count(options.directory)==1&&!service.status()->filename.empty(),"text-only report did not finish");
    need(!service.status()->preview&&!service.status()->capture_requested,"default text report captured an image");
    need(opened==0,"report automatically opened another application");
    service.apply({evrm::DebugReportActionKind::OpenFolder,first,false,{}});service.pump();need(opened==1,"explicit folder action missing");
    const auto photo=service.request(true);need(service.request(false)==photo,"concurrent click started a second job");service.pump();
    need(service.status()->capture_requested&&service.status()->phase==evrm::DebugReportPhase::AwaitingPixels,"image request was not explicit");
    auto image=std::make_shared<evrm::DebugReportImage>();image->width=2;image->height=2;image->rgba={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    need(service.capture_started(photo)&&!service.capture_started(photo),"screenshot request was consumed twice");
    need(!service.submit_pixels(first,image)&&service.submit_pixels(photo,image),"stale or current image job acceptance wrong");service.pump();
    auto status=service.status();need(status->phase==evrm::DebugReportPhase::Preview&&status->preview&&count(options.directory)==1,"image reached disk before preview approval");
    const auto token=status->image_id;
    need(!service.apply({evrm::DebugReportActionKind::IncludePreview,photo,false,token}),"image included before successful preview presentation");
    need(!service.preview_presented(photo,"wrong")&&service.preview_presented(photo,token),"preview acknowledgement did not bind image");
    need(!service.apply({evrm::DebugReportActionKind::IncludePreview,photo,false,"wrong"}),"wrong preview image approved");
    need(service.apply({evrm::DebugReportActionKind::IncludePreview,photo,false,token}),"matching preview approval rejected");service.pump();
    need(done(service.status()->phase)&&count(options.directory)==2&&!service.status()->preview,"approved image report failed or retained idle image data");
    const auto omitted=service.request(true);service.pump();need(service.submit_pixels(omitted,image),"new preview pixels failed");service.pump();
    need(service.apply({evrm::DebugReportActionKind::SaveTextOnly,omitted,false,{}}),"text-only preview choice failed");service.pump();
    need(done(service.status()->phase)&&count(options.directory)==3,"text-only choice did not produce report");
    const auto cancelled=service.request(true);need(service.apply({evrm::DebugReportActionKind::Cancel,cancelled,false,{}}),"cancel rejected");service.pump();
    need(service.status()->phase==evrm::DebugReportPhase::Cancelled&&count(options.directory)==3&&!service.submit_pixels(cancelled,image),"cancel exported/accepted stale image");
    auto limited=options;limited.directory=folder/"Limited";limited.write_limit=16;evrm::DebugReportService failing(limited);failing.request(false);failing.pump();
    need(failing.status()->phase==evrm::DebugReportPhase::Failed&&count(limited.directory)==0,"failed write left completed/private output");
    auto relative=options;relative.directory="relative-report-directory";evrm::DebugReportService unsafe(relative);unsafe.request(false);unsafe.pump();
    need(unsafe.status()->phase==evrm::DebugReportPhase::Failed,"relative output directory accepted");
    evrm::DebugReportService collision(options);collision.request(false);collision.pump();
    need(collision.status()->phase==evrm::DebugReportPhase::Failed&&count(options.directory)==3,"existing report overwritten");
    auto blocked=options;blocked.directory=folder/"Blocked";std::filesystem::create_directory(blocked.directory);
    const auto existing=blocked.directory/"EnshroudedVRM-report-20261101233000-01020304-1.zip.pending";
    {std::ofstream file(existing);file<<"preserve-existing";}
    evrm::DebugReportService preexisting(blocked);preexisting.request(false);preexisting.pump();
    need(preexisting.status()->phase==evrm::DebugReportPhase::Failed&&std::filesystem::file_size(existing)==17,"foreign pending file was changed");std::filesystem::remove(existing);
    auto racing=options;racing.directory=folder/"Race";std::mutex mutex;std::condition_variable wake;bool entered=false,released=false;
    racing.system=[&]{std::unique_lock<std::mutex> lock(mutex);entered=true;wake.notify_one();wake.wait(lock,[&]{return released;});return evrm::DebugReportSystem{};};
    evrm::DebugReportService concurrent(racing);const auto interrupted=concurrent.request(false);std::thread worker([&]{concurrent.pump();});
    {std::unique_lock<std::mutex> lock(mutex);wake.wait(lock,[&]{return entered;});}
    const bool cancelled_running=concurrent.apply({evrm::DebugReportActionKind::Cancel,interrupted,false,{}});
    const auto premature=concurrent.request(false);
    {std::lock_guard<std::mutex> lock(mutex);released=true;}wake.notify_one();worker.join();concurrent.pump();
    need(cancelled_running&&!premature&&count(racing.directory)==0,"cancel raced a new job or published output");
    need(concurrent.request(true)>interrupted,"new job blocked after cancelled worker finished");
    concurrent.apply({evrm::DebugReportActionKind::Cancel,concurrent.status()->job,false,{}});concurrent.pump();
    auto retired_options=options;retired_options.directory=folder/"Retired";evrm::DebugReportService retiring(retired_options);
    const auto retired_job=retiring.request(true);retiring.pump();need(bind_source(retiring,retired_job,77,0),"capture source binding failed");
    need(sourced_pixels(retiring,retired_job,image,77,0),"source pixels rejected");retiring.pump();need(retiring.status()->phase==evrm::DebugReportPhase::Preview,"retirement preview missing");
    const auto old_token=retiring.status()->image_id;retire(retiring,77,0);retiring.pump();
    need(retiring.status()->phase==evrm::DebugReportPhase::Partial&&!retiring.status()->preview&&count(retired_options.directory)==1,"retired graphics source kept an approvable screenshot instead of partial text");
    need(!retiring.preview_presented(retired_job,old_token),"retired preview acknowledgement accepted");
    const auto next_job=retiring.request(true);retiring.pump();need(bind_source(retiring,next_job,78,0),"new capture source binding failed");
    need(!sourced_pixels(retiring,next_job,image,77,0),"pixels from another graphics source accepted");retire(retiring,78,0);retiring.pump();
    need(retiring.status()->phase==evrm::DebugReportPhase::Partial&&count(retired_options.directory)==2,"pending retired capture did not retain text report");
    auto failed_preview_options=options;failed_preview_options.directory=folder/"PreviewUnavailable";evrm::DebugReportService unavailable(failed_preview_options);
    const auto unavailable_job=unavailable.request(true);unavailable.pump();need(unavailable.submit_pixels(unavailable_job,image),"failed-preview fixture pixels rejected");unavailable.pump();
    need(unavailable.capture_failed(unavailable_job,evrm::DebugReportCode::Unavailable),"preview failure notification rejected");unavailable.pump();
    need(unavailable.status()->phase==evrm::DebugReportPhase::Partial&&!unavailable.status()->preview,"failed preview did not finish partial text");
    std::cout<<"PASS report service: text default, one job, exact presented-image approval, omit/cancel, stale results, safe atomic files and explicit folder action\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL report service: "<<error.what()<<'\n';return 1;}}
