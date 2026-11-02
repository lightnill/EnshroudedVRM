#include "debug_report_service.hpp"
#include "debug_report_sources.hpp"
#include "debug_report_policy.hpp"
#include "debug_report_archive.hpp"
#include "debug_report_log.hpp"
#include "native_preview_trace.hpp"
#include "standalone_files.hpp"
#include "sha256.hpp"
#ifndef EVRM_DEBUG_REPORT_TESTING
#include "standalone_startup.hpp"
#endif
#include "../vendor/tinygltf/json.hpp"
#include <shellapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
namespace evrm {namespace {
bool active(DebugReportPhase phase){return phase==DebugReportPhase::Collecting||phase==DebugReportPhase::AwaitingPixels||phase==DebugReportPhase::Encoding||phase==DebugReportPhase::Preview||phase==DebugReportPhase::Writing||phase==DebugReportPhase::Finalizing;}
bool terminal(DebugReportPhase phase){return phase==DebugReportPhase::Complete||phase==DebugReportPhase::Partial||phase==DebugReportPhase::Cancelled||phase==DebugReportPhase::Failed;}
std::string utc_now(){SYSTEMTIME time{};GetSystemTime(&time);char text[32]{};std::snprintf(text,sizeof text,"%04u-%02u-%02uT%02u:%02u:%02uZ",time.wYear,time.wMonth,time.wDay,time.wHour,time.wMinute,time.wSecond);return text;}
std::string fingerprint(const std::vector<std::uint8_t>& bytes){const auto digest=detail::sha256_portable(bytes.data(),bytes.size());const char hex[]="0123456789abcdef";std::string result;result.reserve(64);for(auto value:digest){result+=hex[value>>4];result+=hex[value&15];}return result;}
std::string nonce(){std::array<std::uint8_t,8> bytes{};if(BCryptGenRandom(nullptr,bytes.data(),ULONG(bytes.size()),BCRYPT_USE_SYSTEM_PREFERRED_RNG)<0)throw DebugReportCode::Unavailable;
    const char hex[]="0123456789abcdef";std::string result;for(auto byte:bytes){result+=hex[byte>>4];result+=hex[byte&15];}return result;}
bool hex_tag(const std::string& value){if(value.size()<8||value.size()>32)return false;for(auto c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
std::string date_tag(const std::string& text){if(text.size()!=20)return "undated";std::string result;for(std::size_t i=0;i<text.size();++i){const auto c=text[i];if(c>='0'&&c<='9')result+=c;else if(!((i==4||i==7)&&c=='-')&&!(i==10&&c=='T')&&!((i==13||i==16)&&c==':')&&!(i==19&&c=='Z'))return "undated";}return result;}
bool handle_path(HANDLE file,const std::filesystem::path& expected,bool directory){BY_HANDLE_FILE_INFORMATION metadata{};std::array<wchar_t,32768> actual{},full{};
    const auto size=GetFinalPathNameByHandleW(file,actual.data(),DWORD(actual.size()),FILE_NAME_NORMALIZED|VOLUME_NAME_DOS);
    const auto wanted=GetFullPathNameW(expected.c_str(),DWORD(full.size()),full.data(),nullptr);const auto prefix=size>=4&&std::wstring_view(actual.data(),4)==L"\\\\?\\"?4u:0u;
    return size>prefix&&size<actual.size()&&wanted&&wanted<full.size()&&GetFileInformationByHandle(file,&metadata)&&GetFileType(file)==FILE_TYPE_DISK&&
        !(metadata.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&bool(metadata.dwFileAttributes&FILE_ATTRIBUTE_DIRECTORY)==directory&&(directory||metadata.nNumberOfLinks==1)&&
        size-prefix==wanted&&CompareStringOrdinal(actual.data()+prefix,int(size-prefix),full.data(),int(wanted),TRUE)==CSTR_EQUAL;
}
std::string read_known_file(const std::filesystem::path& path,std::size_t limit,bool tail){
    if(!detail::local_standalone_path(path))throw DebugReportCode::Permission;
    const auto file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw DebugReportCode::Unavailable;
    struct Close {HANDLE file;~Close(){CloseHandle(file);}} close{file};
    if(!handle_path(file,path,false))throw DebugReportCode::Permission;
    LARGE_INTEGER size{};if(!GetFileSizeEx(file,&size)||size.QuadPart<0)throw DebugReportCode::Unavailable;
    if(!tail&&std::uint64_t(size.QuadPart)>limit)throw DebugReportCode::Limit;
    const auto count=std::min<std::uint64_t>(std::uint64_t(size.QuadPart),limit);LARGE_INTEGER start{};start.QuadPart=tail?size.QuadPart-static_cast<LONGLONG>(count):0;
    if(!SetFilePointerEx(file,start,nullptr,FILE_BEGIN))throw DebugReportCode::Unavailable;
    std::string result(std::size_t(count),'\0');std::size_t offset=0;
    while(offset<result.size()){DWORD received=0;const auto wanted=DWORD(std::min<std::size_t>(65536,result.size()-offset));if(!ReadFile(file,result.data()+offset,wanted,&received,nullptr)||!received)throw DebugReportCode::Unavailable;offset+=received;}
    if(!handle_path(file,path,false))throw DebugReportCode::Stale;
    return result;
}
struct Output {
    std::filesystem::path temporary,final;std::unique_ptr<detail::StandaloneFile> parent_lock,directory_lock;HANDLE file=INVALID_HANDLE_VALUE;bool renamed=false;
    explicit Output(const std::filesystem::path& directory,const std::string& basename){
        if(!detail::local_standalone_path(directory.parent_path())||!detail::local_standalone_path(directory,true))throw DebugReportCode::Permission;
        parent_lock=std::make_unique<detail::StandaloneFile>(directory.parent_path(),true);
        if(!CreateDirectoryW(directory.c_str(),nullptr)&&GetLastError()!=ERROR_ALREADY_EXISTS)throw DebugReportCode::Permission;
        directory_lock=std::make_unique<detail::StandaloneFile>(directory,true);final=directory/std::filesystem::u8path(basename);temporary=final;temporary+=L".pending";
        if(GetFileAttributesW(final.c_str())!=INVALID_FILE_ATTRIBUTES)throw DebugReportCode::Disk;
        file=CreateFileW(temporary.c_str(),GENERIC_WRITE|DELETE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE)throw DebugReportCode::Disk;
        if(!handle_path(file,temporary,false)){discard();throw DebugReportCode::Permission;}
    }
    void discard()noexcept{if(file!=INVALID_HANDLE_VALUE){if(!renamed){FILE_DISPOSITION_INFO disposition{TRUE};SetFileInformationByHandle(file,FileDispositionInfo,&disposition,sizeof disposition);}CloseHandle(file);file=INVALID_HANDLE_VALUE;}}
    ~Output(){discard();}
    bool write(const std::uint8_t* data,std::size_t size){std::size_t at=0;while(at<size){DWORD wrote=0;const auto wanted=DWORD(std::min<std::size_t>(65536,size-at));if(!WriteFile(file,data+at,wanted,&wrote,nullptr)||!wrote)return false;at+=wrote;}return true;}
    bool commit(){
        if(!FlushFileBuffers(file)||!handle_path(file,temporary,false))return false;
        const auto target=final.wstring();const auto bytes=offsetof(FILE_RENAME_INFO,FileName)+(target.size()+1)*sizeof(wchar_t);std::vector<std::uint8_t> storage(bytes,0);
        auto* rename=reinterpret_cast<FILE_RENAME_INFO*>(storage.data());rename->ReplaceIfExists=FALSE;rename->FileNameLength=DWORD(target.size()*sizeof(wchar_t));std::memcpy(rename->FileName,target.data(),rename->FileNameLength);
        if(!SetFileInformationByHandle(file,FileRenameInfo,rename,DWORD(storage.size())))return false;
        renamed=true;return true;
    }
};
}
struct DebugReportService::Impl {
    DebugReportOptions options;std::mutex changes,pumping,wake_mutex;std::condition_variable wake;std::thread worker;std::atomic<bool> stopping{false};
    std::atomic<std::uint64_t> job{0};std::atomic<DebugReportPhase> phase{DebugReportPhase::Idle};std::shared_ptr<const DebugReportStatus> published;
    std::atomic<std::uint64_t> capture_source{0},retired_source{0},queued_image_failure{0};std::atomic<bool> wants_image{false};bool image_partial_added=false;
    std::uint64_t requested=0;bool initialized=false,sources_done=false,capture_requested=false,image_failed=false,preview_shown=false,selected=false,open_pending=false;
    DebugReportCode code=DebugReportCode::None;DebugReportData data;std::shared_ptr<const std::vector<std::uint32_t>> program;std::shared_ptr<const DebugReportImage> pixels,preview;std::vector<std::uint8_t> png;std::string image_id,filename;
    explicit Impl(DebugReportOptions value):options(std::move(value)){published=std::make_shared<const DebugReportStatus>();}
    std::uint64_t now()const{return options.clock?options.clock():GetTickCount64();}
    bool capture_valid()const noexcept{const auto source=capture_source.load();return (!source||retired_source.load()!=source)&&(queued_image_failure.load()>>8)!=job.load();}
    DebugReportCode invalid_image_code()const noexcept{const auto failure=queued_image_failure.load();return (failure>>8)==job.load()?static_cast<DebugReportCode>(failure&255):DebugReportCode::Stale;}
    void publish()noexcept{try{auto value=std::make_shared<DebugReportStatus>();value->job=job.load();value->phase=phase.load();value->code=value->phase==DebugReportPhase::Cancelled?DebugReportCode::Cancelled:code;
        value->filename=filename;value->capture_requested=capture_requested&&!image_failed&&(value->phase==DebugReportPhase::Collecting||value->phase==DebugReportPhase::AwaitingPixels);
        value->capture_program=program;
        value->capture_source=capture_source.load();
        if(value->phase==DebugReportPhase::Preview){value->image_id=image_id;value->preview=preview;value->preview_presented=preview_shown;}
        std::atomic_store_explicit(&published,std::shared_ptr<const DebugReportStatus>(std::move(value)),std::memory_order_release);
    }catch(...) {}}
    void release_image()noexcept{pixels.reset();preview.reset();program.reset();std::vector<std::uint8_t>{}.swap(png);image_id.clear();capture_requested=false;preview_shown=false;}
};
DebugReportService::DebugReportService(DebugReportOptions options):impl_(std::make_unique<Impl>(std::move(options))){
    if(impl_->options.background)impl_->worker=std::thread([this]{SetThreadPriority(GetCurrentThread(),THREAD_PRIORITY_BELOW_NORMAL);while(!impl_->stopping.load()){pump();std::unique_lock<std::mutex> lock(impl_->wake_mutex);impl_->wake.wait_for(lock,std::chrono::milliseconds(50));}});
}
DebugReportService::~DebugReportService(){impl_->stopping.store(true);impl_->wake.notify_all();if(impl_->worker.joinable())impl_->worker.join();debug_report_cancel_sources(impl_->job.load());}
std::shared_ptr<const DebugReportStatus> DebugReportService::status()const noexcept{return std::atomic_load_explicit(&impl_->published,std::memory_order_acquire);}
std::uint64_t DebugReportService::request(bool image)noexcept{try{
    std::unique_lock<std::mutex> runner(impl_->pumping,std::try_to_lock);if(!runner.owns_lock())return active(impl_->phase.load())?impl_->job.load():0;
    std::unique_lock<std::mutex> lock(impl_->changes,std::try_to_lock);if(!lock.owns_lock())return 0;
    if(active(impl_->phase.load()))return impl_->job.load();
    if(impl_->job.load()>=(UINT64_MAX>>8))return 0;
    impl_->requested=impl_->now();impl_->job.fetch_add(1);impl_->initialized=impl_->sources_done=false;impl_->wants_image=impl_->capture_requested=image;impl_->image_failed=impl_->selected=false;
    impl_->capture_source.store(0);impl_->retired_source.store(0);impl_->queued_image_failure.store(0);impl_->image_partial_added=false;
    impl_->code=DebugReportCode::None;impl_->data={};impl_->release_image();impl_->capture_requested=image;impl_->open_pending=false;impl_->filename.clear();impl_->phase.store(DebugReportPhase::Collecting);impl_->publish();impl_->wake.notify_one();return impl_->job.load();
}catch(...){return 0;}}
bool DebugReportService::capture_started(std::uint64_t job,std::uint64_t source)noexcept{try{std::unique_lock<std::mutex> lock(impl_->changes,std::try_to_lock);
    if(!lock.owns_lock()||impl_->job.load()!=job||!impl_->capture_requested||!impl_->wants_image||(impl_->phase.load()!=DebugReportPhase::Collecting&&impl_->phase.load()!=DebugReportPhase::AwaitingPixels))return false;
    impl_->capture_source.store(source);impl_->capture_requested=false;impl_->publish();return true;
}catch(...){return false;}}
void DebugReportService::capture_retired(std::uint64_t source)noexcept{if(source&&impl_->capture_source.load()==source&&impl_->phase.load()!=DebugReportPhase::Finalizing){impl_->retired_source.store(source);impl_->wake.notify_one();}}
bool DebugReportService::capture_failed(std::uint64_t job,DebugReportCode code)noexcept{
    const auto phase=impl_->phase.load();if(!job||impl_->job.load()!=job||!impl_->wants_image.load()||(phase!=DebugReportPhase::Collecting&&phase!=DebugReportPhase::AwaitingPixels&&phase!=DebugReportPhase::Encoding&&phase!=DebugReportPhase::Preview))return false;
    const auto value=(job<<8)|static_cast<unsigned>(code);auto queued=impl_->queued_image_failure.load();
    while((queued>>8)<=job&&!impl_->queued_image_failure.compare_exchange_weak(queued,value)){}
    impl_->wake.notify_one();return true;
}
bool DebugReportService::submit_pixels(std::uint64_t job,std::shared_ptr<const DebugReportImage> pixels,std::uint64_t source)noexcept{try{
    if(!pixels||!pixels->width||!pixels->height||pixels->width>1920||pixels->height>1080||pixels->rgba.size()!=std::uint64_t(pixels->width)*pixels->height*4){capture_failed(job,DebugReportCode::Invalid);return false;}
    std::unique_lock<std::mutex> lock(impl_->changes,std::try_to_lock);const auto phase=impl_->phase.load();
    if(!lock.owns_lock()||impl_->job.load()!=job||!impl_->wants_image||impl_->pixels||impl_->capture_source.load()!=source||!impl_->capture_valid()||(phase!=DebugReportPhase::Collecting&&phase!=DebugReportPhase::AwaitingPixels))return false;
    impl_->pixels=std::move(pixels);impl_->capture_requested=false;impl_->publish();impl_->wake.notify_one();return true;
}catch(...){return false;}}
bool DebugReportService::preview_presented(std::uint64_t job,std::string_view image)noexcept{try{std::unique_lock<std::mutex> lock(impl_->changes,std::try_to_lock);
    if(!lock.owns_lock()||impl_->job.load()!=job||impl_->phase.load()!=DebugReportPhase::Preview||!impl_->capture_valid()||image!=impl_->image_id)return false;
    impl_->preview_shown=true;impl_->publish();return true;
}catch(...){return false;}}
bool DebugReportService::apply(const DebugReportAction& action)noexcept{try{
    if(action.kind==DebugReportActionKind::Begin)return request(action.include_image)!=0;
    std::unique_lock<std::mutex> lock(impl_->changes,std::try_to_lock);if(!lock.owns_lock())return false;
    if(action.job!=impl_->job.load())return false;
    if(action.kind==DebugReportActionKind::Cancel){auto phase=impl_->phase.load();while(active(phase)&&phase!=DebugReportPhase::Finalizing){if(impl_->phase.compare_exchange_weak(phase,DebugReportPhase::Cancelled)){impl_->publish();impl_->wake.notify_one();return true;}}return false;}
    auto phase=impl_->phase.load();
    if(action.kind==DebugReportActionKind::OpenFolder&&terminal(phase)){impl_->open_pending=true;impl_->wake.notify_one();return true;}
    if(phase!=DebugReportPhase::Preview)return false;
    if(action.kind!=DebugReportActionKind::SaveTextOnly&&action.kind!=DebugReportActionKind::IncludePreview)return false;
    if(action.kind==DebugReportActionKind::IncludePreview&&(!impl_->preview_shown||!impl_->capture_valid()||action.image_id!=impl_->image_id))return false;
    if(!impl_->phase.compare_exchange_strong(phase,DebugReportPhase::Writing))return false;
    impl_->selected=action.kind==DebugReportActionKind::IncludePreview;impl_->publish();impl_->wake.notify_one();return true;
}catch(...){return false;}}
void DebugReportService::pump()noexcept{
    std::unique_lock<std::mutex> runner(impl_->pumping,std::try_to_lock);if(!runner.owns_lock())return;auto& state=*impl_;const auto job=state.job.load();
    try{
        bool open=false;{std::lock_guard<std::mutex> lock(state.changes);open=state.open_pending;state.open_pending=false;}
        if(open&&detail::local_standalone_path(state.options.directory)){if(state.options.open_folder)state.options.open_folder(state.options.directory);else ShellExecuteW(nullptr,L"open",state.options.directory.c_str(),nullptr,nullptr,SW_SHOWNORMAL);}
        const auto image_failure=state.queued_image_failure.exchange(0);if((image_failure>>8)==job&&job){std::lock_guard<std::mutex> lock(state.changes);state.image_failed=true;state.capture_requested=false;state.selected=false;state.code=static_cast<DebugReportCode>(image_failure&255);
            if(state.phase.load()==DebugReportPhase::Preview)state.phase.store(DebugReportPhase::Writing);}
        auto phase=state.phase.load();if(!job||phase==DebugReportPhase::Idle||terminal(phase)){if(phase==DebugReportPhase::Cancelled){debug_report_cancel_sources(job);std::lock_guard<std::mutex> lock(state.changes);state.release_image();state.data={};state.publish();}
            else if(terminal(phase)&&status()->phase!=phase){std::lock_guard<std::mutex> lock(state.changes);state.publish();}
            return;}
        if(!state.capture_valid()){std::lock_guard<std::mutex> lock(state.changes);state.image_failed=true;state.capture_requested=false;state.selected=false;state.code=state.invalid_image_code();
            if(state.phase.load()==DebugReportPhase::Preview)state.phase.store(DebugReportPhase::Writing);
            state.publish();}
        if(!state.initialized){
            state.initialized=true;if(!state.options.sources)debug_report_begin_sources(job,state.requested);
            if(state.wants_image&&!state.options.capture_shader.empty())try{
                const auto bytes=detail::StandaloneFile(state.options.capture_shader).read(65536);
                if(bytes.size()<20||bytes.size()%4)throw DebugReportCode::Invalid;
                auto words=std::make_shared<std::vector<std::uint32_t>>(bytes.size()/4);std::memcpy(words->data(),bytes.data(),bytes.size());
                if(words->front()!=0x07230203u)throw DebugReportCode::Invalid;
                std::lock_guard<std::mutex> lock(state.changes);state.program=std::move(words);state.publish();
            }catch(...){std::lock_guard<std::mutex> lock(state.changes);state.image_failed=true;state.capture_requested=false;state.code=DebugReportCode::Unavailable;state.publish();}
            state.data.request_ms=state.requested;state.data.mod_version=state.options.mod_version;state.data.game_sha256=state.options.game_sha256;
            try{state.data.utc=state.options.utc?state.options.utc():utc_now();}catch(...){state.data.partial.push_back(DebugReportCode::Unavailable);}
            try{state.data.system=state.options.system?state.options.system():debug_report_collect_system();}catch(...){state.data.partial.push_back(DebugReportCode::Unavailable);}
            std::string engine;
            try{if(state.options.files)state.options.files(state.data.graphics_source_json,engine);else if(!state.options.game_directory.empty()){
                    try{state.data.graphics_source_json=read_known_file(state.options.game_directory/L"enshrouded_local.json",2*1024*1024,false);}catch(...){state.data.partial.push_back(DebugReportCode::Unavailable);}
                    try{engine=read_known_file(state.options.game_directory/L"enshrouded.log",2*1024*1024,true);}catch(...){state.data.partial.push_back(DebugReportCode::Unavailable);}
                }}catch(...){state.data.partial.push_back(DebugReportCode::Unavailable);}
            state.data.graphics_source_json=debug_report_graphics_json(state.data.graphics_source_json);
            state.data.diagnostics=debug_report_recent_log(state.requested)+debug_report_engine_events(engine)+native_preview_trace_report(state.requested)+native_preview_handoff_history().report(state.requested);
            const auto omissions=debug_report_log_omissions();state.data.diagnostics_contention_drops=omissions[0];state.data.diagnostics_overwritten=omissions[1];
        }
        if(state.phase.load()==DebugReportPhase::Cancelled)return;
        const auto now=state.now();
        if(!state.sources_done){
            if(!state.options.sources&&!debug_report_sources_ready(job)&&now>=state.requested&&now-state.requested<3000)return;
            auto observed=state.options.sources?state.options.sources(job,state.requested,now):debug_report_take_sources(job,now);
            state.data.scene=observed.scene;state.data.model=observed.model;state.data.settings=observed.settings;state.data.frame_intervals_us=std::move(observed.frame_intervals_us);
            state.data.collected_ms=observed.collected_ms?observed.collected_ms:now;if(observed.system.gpu.available)state.data.system.gpu=std::move(observed.system.gpu);
            state.data.partial.insert(state.data.partial.end(),observed.partial.begin(),observed.partial.end());state.sources_done=true;debug_report_cancel_sources(job);
        }
        phase=state.phase.load();
        if(phase==DebugReportPhase::Collecting||phase==DebugReportPhase::AwaitingPixels){
            std::shared_ptr<const DebugReportImage> pixels;bool failed=false;DebugReportCode failure=DebugReportCode::None;
            {std::lock_guard<std::mutex> lock(state.changes);pixels=state.pixels;failed=state.image_failed;failure=state.code;}
            if(state.wants_image&&!failed&&!pixels){
                if(now>=state.requested&&now-state.requested<5000){auto expected=phase;if(state.phase.compare_exchange_strong(expected,DebugReportPhase::AwaitingPixels)){std::lock_guard<std::mutex> lock(state.changes);state.publish();}return;}
                failed=true;failure=DebugReportCode::Unavailable;
            }
            if(state.wants_image&&!failed&&pixels){auto expected=phase;if(!state.phase.compare_exchange_strong(expected,DebugReportPhase::Encoding))return;
                auto png=encode_debug_report_png(*pixels);auto preview=std::make_shared<DebugReportImage>(decode_debug_report_png(png));
                if(png.empty()||preview->rgba.empty()){failed=true;failure=DebugReportCode::Invalid;}
                else {const auto id=fingerprint(png);std::lock_guard<std::mutex> lock(state.changes);expected=DebugReportPhase::Encoding;
                    if(state.image_failed||!state.capture_valid()){failed=true;failure=state.capture_valid()?state.code:state.invalid_image_code();}
                    else {if(state.phase.compare_exchange_strong(expected,DebugReportPhase::Preview)){state.png=std::move(png);state.preview=std::move(preview);state.image_id=id;state.pixels.reset();state.publish();}return;}}
                phase=DebugReportPhase::Encoding;
            }
            if(failed){state.data.partial.push_back(failure==DebugReportCode::None?DebugReportCode::Unavailable:failure);state.image_partial_added=true;std::lock_guard<std::mutex> lock(state.changes);state.code=failure;}
            auto expected=phase;if(!state.phase.compare_exchange_strong(expected,DebugReportPhase::Writing))return;
        }
        if(state.phase.load()!=DebugReportPhase::Writing)return;
        for(unsigned attempt=0;attempt<2;++attempt){
        bool selected=false;{std::lock_guard<std::mutex> lock(state.changes);if(!state.capture_valid()){state.image_failed=true;state.selected=false;state.code=DebugReportCode::Stale;}
            if(state.image_failed&&!state.image_partial_added){state.data.partial.push_back(state.code);state.image_partial_added=true;}selected=state.selected;}
        auto files=make_debug_report_contents(state.data);
        {std::lock_guard<std::mutex> lock(state.changes);if(selected){files.push_back({"screenshot.png",state.png});
            for(auto& file:files)if(file.name=="report.json"){auto value=nlohmann::json::parse(file.bytes);value["screenshot_included"]=true;const auto text=value.dump(2)+'\n';file.bytes.assign(text.begin(),text.end());}}}
        auto tag=state.options.nonce.empty()?nonce():state.options.nonce;if(!hex_tag(tag))throw DebugReportCode::Invalid;
        const auto filename="EnshroudedVRM-report-"+date_tag(state.data.utc)+'-'+tag+'-'+std::to_string(job)+".zip";
        {std::lock_guard<std::mutex> lock(state.changes);state.filename=filename;}
        Output output(state.options.directory,filename);std::size_t written=0;
        const bool saved=write_debug_report_zip(files,[&](const std::uint8_t* bytes,std::size_t size){if(state.phase.load()!=DebugReportPhase::Writing||size>state.options.write_limit-written)return false;
            if(!output.write(bytes,size))return false;
            written+=size;return true;});
        if(!saved){if(state.phase.load()==DebugReportPhase::Cancelled)return;throw DebugReportCode::Disk;}
        if(selected&&!state.capture_valid())continue;
        auto expected=DebugReportPhase::Writing;if(!state.phase.compare_exchange_strong(expected,DebugReportPhase::Finalizing))return;
        if(selected&&!state.capture_valid()){state.phase.store(DebugReportPhase::Writing);continue;}
        if(!output.commit())throw DebugReportCode::Disk;
        {std::lock_guard<std::mutex> lock(state.changes);state.phase.store(state.data.partial.empty()?DebugReportPhase::Complete:DebugReportPhase::Partial);state.release_image();state.data={};state.publish();}
        return;}
        throw DebugReportCode::Stale;
    }catch(DebugReportCode code){std::lock_guard<std::mutex> lock(state.changes);if(state.phase.load()!=DebugReportPhase::Cancelled){state.code=code;state.phase.store(DebugReportPhase::Failed);}state.filename.clear();state.release_image();state.data={};state.publish();debug_report_cancel_sources(job);}
    catch(...){std::lock_guard<std::mutex> lock(state.changes);if(state.phase.load()!=DebugReportPhase::Cancelled){state.code=DebugReportCode::Unavailable;state.phase.store(DebugReportPhase::Failed);}state.filename.clear();state.release_image();state.data={};state.publish();debug_report_cancel_sources(job);}
}
namespace {
std::atomic<DebugReportService*> global{nullptr};std::mutex global_mutex;
DebugReportService* global_service(bool create)noexcept{
    if(auto* service=global.load(std::memory_order_acquire))return service;
#ifndef EVRM_DEBUG_REPORT_TESTING
    if(create)try{std::lock_guard<std::mutex> lock(global_mutex);if(auto* service=global.load())return service;
        const auto& context=runtime_startup_context();if(!context.ready()||!context.state_writable)return nullptr;
        DebugReportOptions options;options.directory=context.state_directory/L"Reports";options.game_directory=context.game_directory;options.mod_version=context.version;
        options.capture_shader=context.runtime_directory/L"shaders"/L"debug_report_capture.comp.spv";
        const char hex[]="0123456789abcdef";for(auto byte:context.owner.sha256){options.game_sha256+=hex[byte>>4];options.game_sha256+=hex[byte&15];}
        auto* service=new DebugReportService(std::move(options));global.store(service,std::memory_order_release);return service;
    }catch(...){}
#else
    (void)create;
#endif
    return nullptr;
}
}
std::shared_ptr<const DebugReportStatus> debug_report_status()noexcept{if(auto* service=global_service(false))return service->status();static const auto empty=std::make_shared<const DebugReportStatus>();return empty;}
void debug_report_apply(const DebugReportAction& action)noexcept{if(auto* service=global_service(action.kind==DebugReportActionKind::Begin))service->apply(action);}
bool debug_report_capture_started(std::uint64_t job,std::uint64_t source)noexcept{if(auto* service=global_service(false))return service->capture_started(job,source);return false;}
void debug_report_capture_retired(std::uint64_t source)noexcept{if(auto* service=global_service(false))service->capture_retired(source);}
void debug_report_capture_failed(std::uint64_t job,DebugReportCode code)noexcept{if(auto* service=global_service(false))service->capture_failed(job,code);}
void debug_report_submit_pixels(std::uint64_t job,std::shared_ptr<const DebugReportImage> image,std::uint64_t source)noexcept{if(auto* service=global_service(false))service->submit_pixels(job,std::move(image),source);}
void debug_report_preview_presented(std::uint64_t job,std::string_view id)noexcept{if(auto* service=global_service(false))service->preview_presented(job,id);}
bool debug_report_busy()noexcept{return active(debug_report_status()->phase);}
}
