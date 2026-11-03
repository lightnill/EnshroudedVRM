#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#include "debug_report_sources.hpp"
#include "enshrouded_bridge.hpp"
#include "avatar_model.hpp"
#include "avatar_service_types.hpp"
#include "mod_settings.hpp"
#include "mouth_data.hpp"
#include <algorithm>
#include <atomic>
#include <cstring>
#include <mutex>
namespace evrm {namespace {
struct Sources {
    std::mutex mutex;std::atomic<std::uint64_t> active{0};std::atomic<bool> collecting{false};DebugReportData data;unsigned frames=0;
    struct Interval {std::uint64_t tick_us=0;std::uint32_t delta_us=0,padding=0;};
    static_assert(sizeof(Interval)==16,"bounded frame history");
    std::array<Interval,4096> intervals{};std::size_t cursor=0,count=0;std::uint64_t previous_us=0,previous_source=0;
    bool previous_menu=false,previous_reporting=false;
};
Sources& state(){static Sources value;return value;}
template<class Function>void observe(Function&& function)noexcept{
    try{auto& source=state();const auto job=source.active.load(std::memory_order_acquire);if(!job)return;
        std::unique_lock<std::mutex> lock(source.mutex,std::try_to_lock);if(lock.owns_lock()&&job==source.active.load(std::memory_order_acquire)&&source.frames<2)function(source.data);
    }catch(...){}
}
std::string registry_text(const wchar_t* key,const wchar_t* name){
    std::array<wchar_t,256> text{};DWORD bytes=DWORD(text.size()*sizeof(wchar_t));
    if(RegGetValueW(HKEY_LOCAL_MACHINE,key,name,RRF_RT_REG_SZ,nullptr,text.data(),&bytes)!=ERROR_SUCCESS)return {};
    std::size_t length=0;while(length<text.size()&&text[length])++length;if(length==text.size())return {};
    const auto count=WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(length),nullptr,0,nullptr,nullptr);if(count<=0||count>256)return {};
    std::string result(std::size_t(count),'\0');if(WideCharToMultiByte(CP_UTF8,WC_ERR_INVALID_CHARS,text.data(),int(length),result.data(),count,nullptr,nullptr)!=count)return {};
    while(!result.empty()&&result.back()==' ')result.pop_back();
    return result;
}
}
bool debug_report_sources_pending()noexcept{return state().collecting.load(std::memory_order_relaxed);}
void debug_report_begin_sources(std::uint64_t job,std::uint64_t request_ms){
    auto& source=state();std::lock_guard<std::mutex> lock(source.mutex);source.data={};source.frames=0;source.data.request_ms=request_ms;
    source.data.frame_intervals_us.reserve(source.count);const auto first=(source.cursor+source.intervals.size()-source.count)%source.intervals.size();
    for(std::size_t i=0;i<source.count;++i){const auto& interval=source.intervals[(first+i)%source.intervals.size()];
        if(interval.tick_us<=source.previous_us&&source.previous_us-interval.tick_us<=120000000)source.data.frame_intervals_us.push_back(interval.delta_us);}
    source.active.store(job,std::memory_order_release);
    source.collecting.store(job!=0,std::memory_order_release);
}
bool debug_report_sources_ready(std::uint64_t job)noexcept{try{auto& source=state();std::lock_guard<std::mutex> lock(source.mutex);return job&&job==source.active.load()&&source.frames>=2;}catch(...){return false;}}
void debug_report_cancel_sources(std::uint64_t job)noexcept{try{auto& source=state();std::lock_guard<std::mutex> lock(source.mutex);if(source.active.load()==job){source.collecting.store(false);source.active.store(0);source.data={};source.frames=0;}}catch(...) {}}
void debug_report_observe_player(const PlayerFrame& frame,std::uint64_t sample_ms)noexcept{
    observe([&](DebugReportData& data){if(!frame||!frame.map_position_known||!sample_ms)return;
        data.scene.location={true,true,frame.map_position,sample_ms};data.scene.relative_clip_available=true;
        for(unsigned c=0;c<4;++c)for(unsigned r=0;r<4;++r){double value=0;for(unsigned k=0;k<4;++k)value+=double(frame.camera_world_to_clip[k*4+r])*frame.actor_world[c*4+k];data.scene.player_to_clip[c*4+r]=value;}
    });
}
void debug_report_observe_scene(DebugReportSceneFamily family)noexcept{observe([&](DebugReportData& data){data.scene.render_activity_available=true;
    auto& count=family==DebugReportSceneFamily::Terrain?data.scene.terrain_draws:family==DebugReportSceneFamily::Building?data.scene.building_draws:data.scene.unknown_draws;
    if(count<1000000)++count;});}
void debug_report_observe_view(std::uint32_t width,std::uint32_t height,std::uint32_t format,std::uint32_t samples)noexcept{
    observe([&](DebugReportData& data){data.scene.width=width;data.scene.height=height;data.scene.depth_format=format;data.scene.depth_samples=samples;data.scene.depth_available=format&&samples;});
}
void debug_report_observe_lighting(std::array<std::uint32_t,3> extent)noexcept{observe([&](DebugReportData& data){data.scene.lighting_extent=extent;data.scene.lighting_available=extent[0]&&extent[1]&&extent[2];});}
void debug_report_observe_gpu(const DebugReportGpuInfo& gpu)noexcept{observe([&](DebugReportData& data){data.system.gpu=gpu;});}
void debug_report_observe_settings(const ModSettings& settings,const AvatarMenuSnapshot* avatar)noexcept{
    observe([&](DebugReportData& data){auto& out=data.settings;out.available=true;out.avatar_available=avatar&&avatar->preferences_loaded;out.appearance_available=out.avatar_available&&bool(avatar->profile);out.voice_enabled=settings.voice_enabled;out.voice_source=settings.voice_source==VoiceInputSource::Microphone?1:0;
        out.strength=settings.strength;out.sensitivity=settings.sensitivity;out.smoothing_ms=settings.smoothing_ms;out.materials.clear();
        if(avatar){out.replacement_enabled=avatar->replacement_enabled;if(avatar->profile){out.whole_preset=valid_avatar_preset(avatar->profile->whole)?static_cast<unsigned>(avatar->profile->whole):0;
            for(const auto& item:avatar->profile->overrides){if(out.materials.size()==1024)break;if(valid_avatar_preset(item.preset))out.materials.push_back({item.index,static_cast<unsigned>(item.preset)});}}}
    });
}
void debug_report_observe_model(const PreparedAvatar* prepared)noexcept{
    observe([&](DebugReportData& data){if(!prepared||!prepared->asset)return;const auto& avatar=*prepared->asset;DebugReportModel model;model.available=true;
        model.vrm_version=avatar.version==vrm::VrmVersion::V0?0:1;model.meshes=avatar.meshes.size();model.materials=avatar.materials.size();model.textures=avatar.textures.size();
        model.bundled_sample=prepared->fingerprint=="da1e68ca997415de3ff55ba1a8468257c523c6d5db65d193f4110e37861bc098";
        for(const auto& mesh:avatar.meshes)for(const auto& primitive:mesh.primitives){model.vertices+=primitive.vertices.size();model.indices+=primitive.indices.size();}
        for(const auto& skin:avatar.skins)model.joints+=skin.joints.size();
        for(const auto& image:avatar.images)model.texture_bytes+=image.rgba.size();
        if(avatar.mouth)model.mouth_shapes=avatar.mouth->available&31u;
        // General morph targets are not retained by the current importer.
        // Report that count unavailable instead of pretending it is zero.
        data.model=model;
    });
}
void debug_report_end_source_frame(std::uint64_t tick_ms)noexcept{try{auto& source=state();if(!source.active.load(std::memory_order_relaxed))return;
    std::unique_lock<std::mutex> lock(source.mutex,std::try_to_lock);if(lock.owns_lock()&&source.active.load()&&source.frames<2){++source.frames;source.data.collected_ms=tick_ms;source.data.scene.sample_ms=tick_ms;if(source.frames==2)source.collecting.store(false,std::memory_order_release);}
}catch(...) {}}
DebugReportData debug_report_take_sources(std::uint64_t job,std::uint64_t now_ms){auto& source=state();std::lock_guard<std::mutex> lock(source.mutex);
    if(!job||job!=source.active.load())return {};
    auto result=source.data;if(!result.collected_ms)result.collected_ms=now_ms;
    const auto sample=result.scene.location.sample_ms;
    if(!sample||sample>result.collected_ms||result.collected_ms-sample>250)result.scene.location.available=false;
    if(source.frames<2)result.partial.push_back(DebugReportCode::Unavailable);
    return result;
}
void debug_report_presented(std::uint64_t tick_us,std::uint64_t generation,bool menu_open,bool reporting)noexcept{try{
    auto& source=state();std::unique_lock<std::mutex> lock(source.mutex,std::try_to_lock);if(!lock.owns_lock())return;
    if(generation&&generation==source.previous_source&&tick_us>source.previous_us&&!menu_open&&!reporting&&!source.previous_menu&&!source.previous_reporting){const auto delta=tick_us-source.previous_us;
        if(delta<=60000000){source.intervals[source.cursor]={tick_us,std::uint32_t(delta),0};source.cursor=(source.cursor+1)%source.intervals.size();source.count=std::min(source.count+1,source.intervals.size());}}
    source.previous_us=tick_us;source.previous_source=generation;
    source.previous_menu=menu_open;source.previous_reporting=reporting;
}catch(...) {}}
DebugReportSystem debug_report_collect_system()noexcept{
    DebugReportSystem result;try{
        result.cpu_model=registry_text(L"HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",L"ProcessorNameString");
        result.logical_processors=GetActiveProcessorCount(ALL_PROCESSOR_GROUPS);
        DWORD bytes=0;if(!GetLogicalProcessorInformationEx(RelationProcessorCore,nullptr,&bytes)&&GetLastError()==ERROR_INSUFFICIENT_BUFFER&&bytes&&bytes<=1024*1024){
            std::vector<std::uint8_t> data(bytes);if(GetLogicalProcessorInformationEx(RelationProcessorCore,reinterpret_cast<PSYSTEM_LOGICAL_PROCESSOR_INFORMATION_EX>(data.data()),&bytes)&&bytes<=data.size()){
                for(std::size_t at=0;at+8<=bytes;){DWORD relation=0,size=0;std::memcpy(&relation,data.data()+at,4);std::memcpy(&size,data.data()+at+4,4);
                    if(size<8||size>bytes-at)break;
                    if(relation==RelationProcessorCore)++result.physical_cores;
                    at+=size;}
            }
        }
        MEMORYSTATUSEX memory{};memory.dwLength=sizeof memory;if(GlobalMemoryStatusEx(&memory)){result.ram_total=memory.ullTotalPhys;result.ram_available=memory.ullAvailPhys;}
        PROCESS_MEMORY_COUNTERS_EX process{};process.cb=sizeof process;if(K32GetProcessMemoryInfo(GetCurrentProcess(),reinterpret_cast<PPROCESS_MEMORY_COUNTERS>(&process),sizeof process)){
            result.working_set=process.WorkingSetSize;result.private_bytes=process.PrivateUsage;}
        const auto build=registry_text(L"SOFTWARE\\Microsoft\\Windows NT\\CurrentVersion",L"CurrentBuildNumber");
        std::uint32_t number=0;bool valid=!build.empty()&&build.size()<=7;for(auto c:build){valid=valid&&c>='0'&&c<='9';if(valid)number=number*10+unsigned(c-'0');}if(valid)result.windows_build=number;
    }catch(...){}return result;
}
std::string debug_report_engine_events(std::string_view tail){
    if(tail.size()>2*1024*1024)tail.remove_prefix(tail.size()-2*1024*1024);
    constexpr std::string_view tokens[]={"VK_ERROR_OUT_OF_HOST_MEMORY","VK_ERROR_OUT_OF_DEVICE_MEMORY","VK_ERROR_DEVICE_LOST","VK_ERROR_INITIALIZATION_FAILED","VK_ERROR_SURFACE_LOST_KHR","VK_ERROR_OUT_OF_DATE_KHR"};
    std::array<unsigned,6> counts{};std::size_t at=0;
    while(at<tail.size()){const auto end=tail.find('\n',at);const auto line=tail.substr(at,(end==std::string_view::npos?tail.size():end)-at);
        if(line.substr(0,2)=="[E"||line.substr(0,2)=="[W")for(std::size_t i=0;i<std::size(tokens);++i)if(line.find(tokens[i])!=std::string_view::npos&&counts[i]<1000000)++counts[i];
        if(end==std::string_view::npos)break;
        at=end+1;}
    std::string output;for(std::size_t i=0;i<counts.size();++i)if(counts[i])output+="engine.graphics code="+std::to_string(i+1)+" count="+std::to_string(counts[i])+"\n";
    return output;
}
}
