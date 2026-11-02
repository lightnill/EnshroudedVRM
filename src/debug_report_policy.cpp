#include "debug_report_policy.hpp"
#include "debug_report_log.hpp"
#include "../vendor/tinygltf/json.hpp"
#include <algorithm>
#include <cmath>
#include <string_view>
namespace evrm {namespace {
using Json=nlohmann::json;
bool version(std::string_view value){if(value.empty()||value.size()>64||value.front()<'0'||value.front()>'9')return false;for(auto c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='z')||(c>='A'&&c<='Z')||c=='.'||c=='-'||c=='+'))return false;return true;}
bool digest(std::string_view value){if(value.size()!=64)return false;for(auto c:value)if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return false;return true;}
bool timestamp(std::string_view value){if(value.size()!=20)return false;for(unsigned i=0;i<20;++i){const char expected=i==4||i==7?'-':i==10?'T':i==13||i==16?':':i==19?'Z':0;if(expected?value[i]!=expected:value[i]<'0'||value[i]>'9')return false;}return true;}
std::string hardware(std::string_view value){
    if(value.empty()||value.size()>128)return {};
    constexpr std::string_view vendors[]={"Intel","GenuineIntel","AMD","AuthenticAMD","ARM","Qualcomm","NVIDIA","Apple","Microsoft","Mesa","Radeon","GeForce","11th Gen Intel","12th Gen Intel","13th Gen Intel","14th Gen Intel","15th Gen Intel"};
    bool vendor=false;for(auto name:vendors)if(value.substr(0,name.size())==name&&(value.size()==name.size()||value[name.size()]==' '||value[name.size()]=='('))vendor=true;
    if(!vendor)return {};
    for(std::size_t i=0;i<value.size();++i){const char c=value[i];if((c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c==' '||c=='('||c==')'||c=='-'||c=='.'||c=='+')continue;
        if(c=='@'&&i+2<value.size()&&value[i+1]==' '&&value[i+2]>='0'&&value[i+2]<='9')continue;
        return {};}
    return std::string(value);
}
Json graphics(std::string_view raw){
    if(raw.size()>2*1024*1024)return Json::object();
    auto source=Json::parse(raw.begin(),raw.end(),nullptr,false);if(!source.is_object())return Json::object();
    const auto selected=source.find("graphics");if(selected!=source.end()){if(!selected->is_object())return Json::object();source=*selected;}
    Json result=Json::object();constexpr const char* allowed[]={"fogQuality","shadowQuality","textureQuality","terrainQuality","objectDetailQuality","reflectionQuality","ambientOcclusionQuality",
        "antiAliasingQuality","resolutionScale","renderScale","fieldOfView","width","height","fullscreen","vsync","frameGeneration"};
    for(const auto* name:allowed){const auto found=source.find(name);if(found==source.end())continue;
        if(found->is_boolean())result[name]=found->get<bool>();
        else if(found->is_number()){const auto number=found->get<double>();if(std::isfinite(number)&&number>=0&&number<=32768)result[name]=*found;}
        else if(found->is_string()){
            auto label=found->get<std::string>();if(label.size()>32)continue;for(auto& c:label)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
            constexpr const char* labels[]={"off","on","performance","balance","balanced","quality","ultra","lowest","low","medium","high","native","taa","fxaa","fsr","dlss","enabled","disabled","custom"};
            for(const auto* allowed_label:labels)if(label==allowed_label){result[name]=label;break;}
        }
    }
    const auto window=source.find("windowSize");if(window!=source.end()&&window->is_object())for(const auto* axis:{"x","y"}){
        const auto item=window->find(axis);if(item!=window->end()&&item->is_number_unsigned()){const auto value=item->get<std::uint64_t>();if(value&&value<=32768)result[std::string(axis)=="x"?"width":"height"]=value;}}
    const auto render=source.find("render");if(render!=source.end()&&render->is_object()){
        Json selected=Json::object();constexpr const char* fields[]={"preset","antiAliasing","fsr2","fsr3","dlss","textures","shadows","contactShadows","dynamicGi","reflections","fog","volumetricShadow","ssao","distantObjects","decoObjects","detailScattering","voxelMeshQuality","tessellation","pointLightShadows"};
        for(const auto* name:fields){const auto item=render->find(name);if(item==render->end()||!item->is_string())continue;auto label=item->get<std::string>();if(label.size()>32)continue;
            for(auto& c:label)if(c>='A'&&c<='Z')c=char(c-'A'+'a');
            for(const auto* allowed:{"off","on","disabled","enabled","custom","performance","balance","balanced","quality","ultra","lowest","low","medium","high","native","taa","fxaa","fsr","fsr2","fsr3","dlss"})if(label==allowed){selected[name]=label;break;}}
        if(!selected.empty())result["render"]=std::move(selected);
    }
    return result;
}
void add(DebugReportContents& files,const char* name,const Json& object){const auto text=object.dump(2)+'\n';files.push_back({name,{text.begin(),text.end()}});}
void text(DebugReportContents& files,const char* name,const std::string& value){files.push_back({name,{value.begin(),value.end()}});}
std::uint64_t bounded(std::uint64_t value,std::uint64_t limit=std::uint64_t(1)<<54){return std::min(value,limit);}
}
const char* debug_report_code_name(DebugReportCode code)noexcept{
    switch(code){case DebugReportCode::None:return "none";case DebugReportCode::Unavailable:return "unavailable";case DebugReportCode::Busy:return "busy";case DebugReportCode::Permission:return "permission";
        case DebugReportCode::Disk:return "disk";case DebugReportCode::Stale:return "stale";case DebugReportCode::Unsupported:return "unsupported";case DebugReportCode::Invalid:return "invalid";case DebugReportCode::Limit:return "limit";case DebugReportCode::Cancelled:return "cancelled";}
    return "invalid";
}
std::string debug_report_graphics_json(std::string_view raw){return graphics(raw).dump();}
DebugReportContents make_debug_report_contents(const DebugReportData& input){
    DebugReportContents files;files.reserve(8);
    Json report={{"schema","enshrouded-vrm-debug-report-v1"},{"mod_version",version(input.mod_version)?input.mod_version:"unavailable"},
        {"game_sha256",digest(input.game_sha256)?input.game_sha256:"unavailable"},{"requested_utc",timestamp(input.utc)?input.utc:"unavailable"},
        {"collected_after_request_ms",input.collected_ms>=input.request_ms?bounded(input.collected_ms-input.request_ms,60000):0},
        {"sharing","local-file-only"},{"screenshot_included",false},{"partial",Json::array()},
        {"diagnostic_history",{{"runtime_window_seconds",120},{"runtime_row_limit",512},{"contention_drops_this_process",bounded(input.diagnostics_contention_drops)},
            {"overwritten_rows_this_process",bounded(input.diagnostics_overwritten)},{"engine_scope","fixed-error-categories-in-last-2MiB-of-current-log"}}}};
    for(auto code:input.partial)if(report["partial"].size()<32)report["partial"].push_back(debug_report_code_name(code));
    auto intervals=input.frame_intervals_us;if(intervals.size()>4096)intervals.resize(4096);
    intervals.erase(std::remove_if(intervals.begin(),intervals.end(),[](auto value){return !value||value>60000000;}),intervals.end());
    if(!intervals.empty()){std::sort(intervals.begin(),intervals.end());double sum=0;for(auto value:intervals)sum+=value;
        report["present_intervals"]={{"source","application-cpu-present-intervals-not-displayed-fps"},{"samples",intervals.size()},
            {"mean_ms",sum/intervals.size()/1000},{"p95_ms",intervals[(intervals.size()-1)*95/100]/1000.},{"maximum_ms",intervals.back()/1000.}};}
    add(files,"report.json",report);
    const auto& system=input.system;const auto cpu=hardware(system.cpu_model),gpu_name=hardware(system.gpu.model);
    Json machine={{"cpu",{{"available",!cpu.empty()},{"model",cpu.empty()?"unavailable":cpu}}},
        {"ram_bytes",{{"available",system.ram_total!=0}}},{"game_process_memory_bytes",{{"available",system.working_set!=0||system.private_bytes!=0}}},
        {"windows",{{"available",system.windows_build!=0}}},{"gpu",{{"available",system.gpu.available}}}};
    if(system.logical_processors)machine["cpu"]["logical_processors"]=std::min(system.logical_processors,65536u);
    if(system.physical_cores)machine["cpu"]["physical_cores"]=std::min(system.physical_cores,65536u);
    if(system.ram_total){machine["ram_bytes"]["total"]=bounded(system.ram_total);machine["ram_bytes"]["currently_free"]=std::min(system.ram_available,system.ram_total);}
    if(system.working_set)machine["game_process_memory_bytes"]["working_set"]=bounded(system.working_set);
    if(system.private_bytes)machine["game_process_memory_bytes"]["private"]=bounded(system.private_bytes);
    if(system.windows_build)machine["windows"]["build"]=std::min(system.windows_build,1000000u);
    if(system.gpu.available)machine["gpu"]={{"available",true},{"model",gpu_name.empty()?"unavailable":gpu_name},{"vendor_model_id",system.gpu.vendor},{"device_model_id",system.gpu.device},
        {"driver_version_raw",system.gpu.driver_version},{"api_version_raw",system.gpu.api_version},{"memory_type",system.gpu.shared_memory?"shared":"device-local-heap"},
        {"memory_capacity_available",system.gpu.local_bytes!=0},{"memory_budget_available",system.gpu.budget_bytes!=0}};
    if(system.gpu.available&&system.gpu.local_bytes)machine["gpu"]["memory_capacity_bytes"]=bounded(system.gpu.local_bytes);
    if(system.gpu.available&&system.gpu.budget_bytes)machine["gpu"]["memory_budget_bytes"]=bounded(system.gpu.budget_bytes);
    add(files,"system.json",machine);
    const auto& settings=input.settings;
    const auto selected_graphics=graphics(input.graphics_source_json);
    Json configured={{"mod_settings_available",settings.available},{"avatar_settings_available",settings.avatar_available},{"appearance_settings_available",settings.appearance_available},{"graphics_settings_available",!selected_graphics.empty()},{"graphics",selected_graphics}};
    if(settings.available){configured["voice_enabled"]=settings.voice_enabled;configured["voice_source"]=settings.voice_source==1?"microphone":"game";configured["strength"]=std::min(settings.strength,100u);configured["sensitivity"]=std::min(settings.sensitivity,100u);configured["smoothing_ms"]=std::clamp(settings.smoothing_ms,20u,140u);}
    if(settings.avatar_available)configured["replacement_enabled"]=settings.replacement_enabled;
    if(settings.appearance_available){configured["whole_model_preset"]=settings.whole_preset==1?"matte":"game-lighting";configured["material_overrides"]=Json::array();}
    for(const auto& material:settings.materials)if(settings.appearance_available&&configured["material_overrides"].size()<1024&&material.index<100000&&material.preset<=1)
        configured["material_overrides"].push_back({{"index",material.index},{"preset",material.preset==1?"matte":"game-lighting"}});
    add(files,"settings.json",configured);
    const auto& model=input.model;Json active={{"available",model.available}};
    if(model.available)active={{"available",true},{"label","active-model"},{"bundled_sample",model.bundled_sample},{"vrm_version",std::min(model.vrm_version,1u)},
        {"meshes",bounded(model.meshes)},{"vertices",bounded(model.vertices)},{"indices",bounded(model.indices)},{"materials",bounded(model.materials)},
        {"joints",bounded(model.joints)},{"morph_count_available",model.morphs_known},{"textures",bounded(model.textures)},{"texture_bytes",bounded(model.texture_bytes)},{"mouth_shape_mask",model.mouth_shapes&31u}};
    if(model.available&&model.morphs_known)active["morphs"]=bounded(model.morphs);
    add(files,"model.json",active);
    const auto& scene=input.scene;const auto& point=scene.location;
    bool position=point.available&&point.local_world_player&&point.sample_ms<=input.collected_ms&&input.collected_ms-point.sample_ms<=250;
    for(auto value:point.xyz)position=position&&std::isfinite(value)&&std::abs(value)<=1000000;
    Json context={{"terrain_context","observed-rendering-only"},{"exact_voxel_cells_available",false},{"map_location",{{"available",position}}},
        {"render_activity_available",scene.render_activity_available},{"viewport",{{"available",scene.width!=0&&scene.height!=0}}},
        {"depth_available",scene.depth_available},{"lighting_available",scene.lighting_available}};
    if(position)context["map_location"]={{"available",true},{"source","enshrouded-local-world-player"},{"axes","native-xyz-y-up; map-horizontal-xz"},
        {"units","native-world-units; fixed17 packed coordinate convention"},{"sample_age_ms",input.collected_ms-point.sample_ms},{"xyz",point.xyz}};
    if(scene.width&&scene.height){context["viewport"]["width"]=std::min(scene.width,32768u);context["viewport"]["height"]=std::min(scene.height,32768u);}
    if(scene.render_activity_available)context["observed_draw_calls"]={{"terrain",bounded(scene.terrain_draws)},{"building",bounded(scene.building_draws)},{"unknown",bounded(scene.unknown_draws)}};
    if(scene.depth_available)context["depth"]={{"format",scene.depth_format},{"samples",scene.depth_samples}};
    if(scene.lighting_available)context["lighting_volume_extent"]=scene.lighting_extent;
    bool clip=scene.relative_clip_available;for(auto value:scene.player_to_clip)clip=clip&&std::isfinite(value)&&std::abs(value)<=10000000;
    if(clip)context["player_relative_to_clip"]=scene.player_to_clip;
    add(files,"scene.json",context);
    std::string logs;std::size_t offset=0;
    while(offset<input.diagnostics.size()&&offset<1024*1024){const auto end=input.diagnostics.find('\n',offset);const auto size=std::min<std::size_t>((end==std::string::npos?input.diagnostics.size():end)-offset,4096);
        const auto safe=debug_report_safe_event(std::string_view(input.diagnostics).substr(offset,size));if(!safe.empty()&&logs.size()+safe.size()+1<=1024*1024){logs+=safe;logs+='\n';}
        if(end==std::string::npos)break;
        offset=end+1;}
    text(files,"diagnostics.log",logs);
    text(files,"README.txt","EnshroudedVRM local debug report\n\nOnly approved diagnostic fields are included; unknown text and identifiers are omitted.\n"
        "Map coordinates refer only to the character inside Enshrouded. No real-world location is queried.\n"
        "Terrain information describes observed rendering, not a dump of blocks, a save or a world map. Missing data is marked unavailable.\n"
        "Application-present intervals and CPU submission timings are not GPU times or measured displayed FPS.\n"
        "No audio, avatar assets, personal paths, account/server details or automatic uploads.\n"
        "Screenshots require explicit preview/inclusion and are not automatically redacted; inspect all included images before manually sharing.\n");
    return files;
}
}
