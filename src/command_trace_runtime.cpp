#include "command_trace_runtime.hpp"
#include "sha256.hpp"
#include "runtime_log.hpp"
#include "../vendor/tinygltf/json.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <map>
#include <mutex>
#include <set>
#include <string>
#include <type_traits>

namespace evrm {
namespace {
using Json=nlohmann::json;
enum Registry : unsigned { buffers,shaders,pipelines,passes,framebuffers,images,image_views,registry_count };
enum MetadataIssue : std::uint64_t {
    missing=1,overflow=2,unsupported=4,reused=8,observation_failure=16,invalid=32,
    pnext_cycle=64,pnext_limit=128
};
constexpr std::size_t metadata_byte_limit=8*1024*1024,record_byte_limit=256*1024;
constexpr std::size_t file_byte_limit=32*1024*1024,record_unit_limit=2048;
constexpr VkImageUsageFlags attachment_image_usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
constexpr unsigned retry_limit=3;
const char* registry_names[]{"buffers","shaders","pipelines","renderPasses","framebuffers","images","imageViews"};
const char* registry_singular_names[]{"buffer","shader","pipeline","renderPass","framebuffer","image","imageView"};
std::atomic<std::uint64_t> next_directory{0};
std::uint64_t default_clock() noexcept { return GetTickCount64(); }
void diagnostic_log(const char* text) noexcept {try{log_event(text);}catch(...){}}
template<class Handle> std::uint64_t id(Handle handle) noexcept {
    if constexpr(std::is_pointer_v<Handle>)return reinterpret_cast<std::uintptr_t>(handle);
    else return static_cast<std::uint64_t>(handle);
}
struct NativeHandle {
    HANDLE value=INVALID_HANDLE_VALUE;
    NativeHandle()=default;
    explicit NativeHandle(HANDLE handle):value(handle){}
    NativeHandle(const NativeHandle&)=delete;
    NativeHandle& operator=(const NativeHandle&)=delete;
    ~NativeHandle(){if(value!=INVALID_HANDLE_VALUE)CloseHandle(value);}
    bool close() noexcept {if(value==INVALID_HANDLE_VALUE)return true;const auto old=value;value=INVALID_HANDLE_VALUE;return CloseHandle(old)!=0;}
};
Json issue_names(std::uint64_t flags) {
    Json result=Json::array();
    if(flags&missing)result.push_back("missing_metadata");
    if(flags&overflow)result.push_back("metadata_budget_exceeded");
    if(flags&unsupported)result.push_back("unsupported_metadata_fields");
    if(flags&reused)result.push_back("raw_handle_reused_event_association_unproven");
    if(flags&observation_failure)result.push_back("metadata_observation_failed");
    if(flags&invalid)result.push_back("invalid_observed_shape");
    if(flags&pnext_cycle)result.push_back("metadata_pnext_cycle");
    if(flags&pnext_limit)result.push_back("metadata_pnext_depth_exceeded");
    return result;
}
bool array_ok(std::uint32_t count,const void* pointer,std::uint64_t& issues,std::size_t& budget) {
    if(count>CommandTraceRuntime::max_array_entries||count>budget){issues|=overflow;return false;}
    if(count&&!pointer){issues|=missing;return false;}
    budget-=count;return true;
}
void next_info(Json& output,const void* pointer,std::uint64_t& issues) {
    output["pNextPresent"]=pointer!=nullptr;
    if(pointer){output["pNextDecoded"]=false;issues|=unsupported;}
}
struct MetadataChain {
    std::array<const VkBaseInStructure*,CommandTraceRuntime::max_metadata_pnext_depth> nodes{};
    std::size_t count=0;
};
MetadataChain next_chain(Json& output,const void* pointer,std::uint64_t& issues,
                         VkStructureType decoded=VK_STRUCTURE_TYPE_MAX_ENUM) {
    MetadataChain result;output["pNextPresent"]=pointer!=nullptr;output["pNextTypes"]=Json::array();
    output["pNextTraversalComplete"]=true;bool known=true;
    auto node=static_cast<const VkBaseInStructure*>(pointer);
    while(node) {
        if(std::find(result.nodes.begin(),result.nodes.begin()+result.count,node)!=result.nodes.begin()+result.count) {
            issues|=pnext_cycle;output["pNextTraversalComplete"]=false;known=false;break;
        }
        if(result.count==result.nodes.size()){issues|=pnext_limit;output["pNextTraversalComplete"]=false;known=false;break;}
        result.nodes[result.count++]=node;output["pNextTypes"].push_back(static_cast<std::uint32_t>(node->sType));
        if(node->sType!=decoded){issues|=unsupported;known=false;}
        node=node->pNext;
    }
    output["pNextDecoded"]=known;return result;
}
bool fragment_output_observed(Json& output,Json& fields,const VkGraphicsPipelineCreateInfo& info,
                              const MetadataChain& chain,std::uint64_t& issues) {
    if(output.at("pNextTraversalComplete")!=true) {
        fields["reason"]="incomplete_pipeline_chain";issues|=unsupported;return false;
    }
    for(std::size_t i=0;i<chain.count;++i) {
        const auto type=chain.nodes[i]->sType;
        if(type==VK_STRUCTURE_TYPE_PIPELINE_CREATE_FLAGS_2_CREATE_INFO||
           type==VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_LIBRARY_CREATE_INFO_EXT||type==VK_STRUCTURE_TYPE_PIPELINE_LIBRARY_CREATE_INFO_KHR) {
            fields["reason"]="effective_flags_or_library_state_not_interpreted";issues|=unsupported;return false;
        }
    }
    if(info.flags&VK_PIPELINE_CREATE_LIBRARY_BIT_KHR) {
        fields["reason"]="pipeline_library_rendering_not_interpreted";issues|=unsupported;return false;
    }
    if(output.at("dynamicStatesKnown")!=true) {
        fields["reason"]="dynamic_state_not_observed";issues|=missing;return false;
    }
    for(const auto& state:output.at("dynamicStates"))if(state==static_cast<std::uint32_t>(VK_DYNAMIC_STATE_RASTERIZER_DISCARD_ENABLE))return true;
    if(!info.pRasterizationState) {
        fields["reason"]="rasterization_state_not_observed";issues|=missing;return false;
    }
    if(info.pRasterizationState->rasterizerDiscardEnable==VK_TRUE) {
        fields["ignored"]=true;fields["reason"]="static_discard_omits_fragment_output_state";return false;
    }
    if(info.pRasterizationState->rasterizerDiscardEnable!=VK_FALSE) {
        fields["reason"]="invalid_observed_discard_value";issues|=invalid;return false;
    }
    return true;
}
void pipeline_rendering(Json& output,const VkGraphicsPipelineCreateInfo& info,std::uint64_t& issues,std::size_t& budget) {
    const auto chain=next_chain(output,info.pNext,issues,VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO);
    const VkPipelineRenderingCreateInfo* rendering=nullptr;bool duplicate=false;
    for(std::size_t i=0;i<chain.count;++i)if(chain.nodes[i]->sType==VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO) {
        if(rendering)duplicate=true;else rendering=reinterpret_cast<const VkPipelineRenderingCreateInfo*>(chain.nodes[i]);
    }
    Json fields={{"available",false},{"ignored",false},{"colorFormatsKnown",false},{"colorFormats",Json::array()}};
    if(!rendering)fields["reason"]="structure_not_observed";
    else if(info.renderPass){fields["ignored"]=true;fields["reason"]="native_render_pass_parameters_take_precedence";}
    else if(duplicate){fields["reason"]="multiple_rendering_structures";issues|=invalid;output["pNextDecoded"]=false;}
    else if(fragment_output_observed(output,fields,info,chain,issues)) {
        fields["available"]=true;fields["viewMask"]=rendering->viewMask;fields["declaredColorCount"]=rendering->colorAttachmentCount;
        fields["depthFormat"]=static_cast<std::uint32_t>(rendering->depthAttachmentFormat);
        fields["stencilFormat"]=static_cast<std::uint32_t>(rendering->stencilAttachmentFormat);
        const bool colors=array_ok(rendering->colorAttachmentCount,rendering->pColorAttachmentFormats,issues,budget);
        fields["colorFormatsKnown"]=colors;
        if(colors)for(std::uint32_t i=0;i<rendering->colorAttachmentCount;++i)fields["colorFormats"].push_back(static_cast<std::uint32_t>(rendering->pColorAttachmentFormats[i]));
    }else if(fields.at("ignored")!=true)output["pNextDecoded"]=false;
    output["dynamicRendering"]=std::move(fields);
}
std::string fingerprint(const void* data,std::size_t size) {
    static constexpr char digits[]="0123456789abcdef";
    const auto digest=detail::sha256(data,size);std::string result;result.reserve(64);
    for(const auto byte:digest){result.push_back(digits[byte>>4]);result.push_back(digits[byte&15]);}return result;
}
Json entry_name(const char* name,std::uint64_t& issues) {
    if(!name){issues|=missing;return nullptr;}
    std::size_t length=0;while(length<CommandTraceRuntime::max_name_bytes&&name[length])++length;
    if(length==CommandTraceRuntime::max_name_bytes){issues|=overflow;return nullptr;}
    return std::string(name,length);
}
template<class Reference> Json attachment_reference(const Reference& reference) {
    return {{"attachment",reference.attachment},{"layout",static_cast<std::uint32_t>(reference.layout)}};
}
template<class Attachment> Json attachment_description(const Attachment& attachment) {
    return {{"flags",attachment.flags},{"format",static_cast<std::uint32_t>(attachment.format)},
        {"samples",static_cast<std::uint32_t>(attachment.samples)},
        {"loadOp",static_cast<std::uint32_t>(attachment.loadOp)},{"storeOp",static_cast<std::uint32_t>(attachment.storeOp)},
        {"stencilLoadOp",static_cast<std::uint32_t>(attachment.stencilLoadOp)},
        {"stencilStoreOp",static_cast<std::uint32_t>(attachment.stencilStoreOp)},
        {"initialLayout",static_cast<std::uint32_t>(attachment.initialLayout)},
        {"finalLayout",static_cast<std::uint32_t>(attachment.finalLayout)}};
}
template<class Subpass> Json subpass_description(const Subpass& subpass,std::uint64_t& issues,std::size_t& budget) {
    Json result={{"flags",subpass.flags},{"pipelineBindPoint",static_cast<std::uint32_t>(subpass.pipelineBindPoint)},
        {"declaredColorCount",subpass.colorAttachmentCount},{"declaredInputCount",subpass.inputAttachmentCount},
        {"declaredPreserveCount",subpass.preserveAttachmentCount},
        {"colors",Json::array()},{"inputs",Json::array()},{"resolves",Json::array()},{"preserves",Json::array()},{"depth",nullptr}};
    result["colorsKnown"]=array_ok(subpass.colorAttachmentCount,subpass.pColorAttachments,issues,budget);
    if(result["colorsKnown"].get<bool>())for(std::uint32_t i=0;i<subpass.colorAttachmentCount;++i)result["colors"].push_back(attachment_reference(subpass.pColorAttachments[i]));
    result["inputsKnown"]=array_ok(subpass.inputAttachmentCount,subpass.pInputAttachments,issues,budget);
    if(result["inputsKnown"].get<bool>())for(std::uint32_t i=0;i<subpass.inputAttachmentCount;++i)result["inputs"].push_back(attachment_reference(subpass.pInputAttachments[i]));
    result["resolvesPresent"]=subpass.pResolveAttachments!=nullptr;
    if(subpass.pResolveAttachments&&array_ok(subpass.colorAttachmentCount,subpass.pResolveAttachments,issues,budget))
        for(std::uint32_t i=0;i<subpass.colorAttachmentCount;++i)result["resolves"].push_back(attachment_reference(subpass.pResolveAttachments[i]));
    result["preservesKnown"]=array_ok(subpass.preserveAttachmentCount,subpass.pPreserveAttachments,issues,budget);
    if(result["preservesKnown"].get<bool>())for(std::uint32_t i=0;i<subpass.preserveAttachmentCount;++i)result["preserves"].push_back(subpass.pPreserveAttachments[i]);
    if(subpass.pDepthStencilAttachment)result["depth"]=attachment_reference(*subpass.pDepthStencilAttachment);
    return result;
}
Json trace_snapshot(const CommandTraceSnapshot& snapshot) {
    Json output={{"sequence",snapshot.sequence},{"collectorComplete",snapshot.complete},{"issues",snapshot.issues},{"submissions",Json::array()}};
    for(const auto& submission:snapshot.submissions) {
        Json submitted={{"serial",submission.serial},{"queue",submission.queue},{"queueFamily",submission.queue_family},
            {"submit2",submission.submit2},{"batches",Json::array()}};
        for(const auto& batch:submission.batches) {
            Json recorded={{"flags",batch.flags},{"pNextTypes",batch.pnext_types},{"waits",Json::array()},
                {"signals",Json::array()},{"commands",Json::array()},{"events",Json::array()}};
            const auto semaphores=[](const auto& values){Json result=Json::array();for(const auto& value:values)
                result.push_back({{"semaphore",value.semaphore},{"stageMask",value.stage_mask},{"value",value.value},
                    {"deviceIndex",value.device_index},{"hasStageMask",value.has_stage_mask},{"hasValue",value.has_value},
                    {"pNextTypes",value.pnext_types}});return result;};
            recorded["waits"]=semaphores(batch.waits);recorded["signals"]=semaphores(batch.signals);
            for(const auto& command:batch.commands)recorded["commands"].push_back({{"command",command.command},
                {"generation",command.generation},{"deviceMask",command.device_mask},{"pNextTypes",command.pnext_types}});
            for(const auto& event:batch.events) {
                Json observed={{"kind",static_cast<std::uint32_t>(event.kind)},{"command",event.command},
                    {"generation",event.generation},{"queueFamily",event.queue_family},
                    {"level",static_cast<std::uint32_t>(event.level)},{"args",event.args},
                    {"pNextTypes",event.pnext_types},{"regions",Json::array()},
                    {"descriptorSets",event.descriptor_sets},{"dynamicOffsets",event.dynamic_offsets},{"rendering",nullptr}};
                for(const auto& region:event.regions)observed["regions"].push_back({{"sourceOffset",region.source_offset},
                    {"destinationOffset",region.destination_offset},{"size",region.size}});
                if(event.rendering) {
                    const auto attachment=[](const CommandTraceRenderingAttachment& value) {
                        return Json{{"imageView",value.image_view},{"imageLayout",value.image_layout},{"resolveMode",value.resolve_mode},
                            {"resolveImageView",value.resolve_image_view},{"resolveImageLayout",value.resolve_image_layout},
                            {"loadOp",value.load_op},{"storeOp",value.store_op},{"clearValueBits",value.clear_value_bits},
                            {"pNextTypes",value.pnext_types}};
                    };
                    const auto& value=*event.rendering;
                    Json rendering={{"interpretation","declared immediate fields; effective use/layout and resource generations are not proven"},
                        {"renderArea",{{"offset",Json::array({value.render_area.offset.x,value.render_area.offset.y})},
                            {"extent",Json::array({value.render_area.extent.width,value.render_area.extent.height})}}},
                        {"flags",value.flags},{"layerCount",value.layer_count},{"viewMask",value.view_mask},
                        {"declaredColorCount",value.color_attachment_count},{"colors",Json::array()},
                        {"hasDepth",value.has_depth},{"depth",nullptr},{"hasStencil",value.has_stencil},{"stencil",nullptr}};
                    for(const auto& color:value.colors)rendering["colors"].push_back(attachment(color));
                    if(value.has_depth)rendering["depth"]=attachment(value.depth);
                    if(value.has_stencil)rendering["stencil"]=attachment(value.stencil);
                    observed["rendering"]=std::move(rendering);
                }
                recorded["events"].push_back(std::move(observed));
            }
            submitted["batches"].push_back(std::move(recorded));
        }
        output["submissions"].push_back(std::move(submitted));
    }
    return output;
}
}

struct CommandTraceRuntime::Impl {
    struct Record {Json data;std::size_t bytes=0;};
    std::array<std::map<std::uint64_t,Record>,registry_count> registries;
    std::mutex mutex;
    std::atomic<bool> failed{false};
    std::uint64_t issues=0,generation=0,last_poll=0;
    std::size_t retained_bytes=0;
    bool polled=false;
    std::filesystem::path directory,marker,session;
    Clock clock=default_clock;
    NativeHandle request;
    unsigned published=0,write_failures=0,remove_failures=0,logs=0,attempt=0;
    std::array<std::uint64_t,3> saved_frames{},saved_sequences{};

    Impl(const std::filesystem::path& path,Clock time):directory(std::filesystem::absolute(path).lexically_normal()),clock(time?time:default_clock) {
        if(directory.native().size()>30000)throw std::length_error("Trace directory exceeds bound");
        marker=directory/L"trace-native-pass.request";
    }
    template<class Function> void observe(Function&& function) noexcept {
        try{std::lock_guard<std::mutex> guard(mutex);function();}catch(...){failed.store(true);}
    }
    void log(const char* text) noexcept {if(logs<8){++logs;diagnostic_log(text);}}
    template<class Builder> void record(Registry kind,std::uint64_t handle,Builder&& build) {
        if(!handle){issues|=missing;return;}
        auto& registry=registries[kind];auto old=registry.find(handle);const bool was_reused=old!=registry.end();
        if(was_reused){retained_bytes-=old->second.bytes;registry.erase(old);issues|=reused;}
        if(generation==UINT64_MAX){issues|=overflow;return;}
        const auto next=++generation;
        if(registry.size()>=max_registry_entries) {
            auto retired=std::find_if(registry.begin(),registry.end(),[](const auto& value){return !value.second.data.at("alive").template get<bool>();});
            if(retired==registry.end()){issues|=overflow;return;}
            retained_bytes-=retired->second.bytes;registry.erase(retired);issues|=overflow;
        }
        Json data={{"handle",handle},{"generation",next},{"alive",true},{"reused",was_reused}};
        build(data);
        auto bytes=data.dump().size()+64;
        if(bytes>record_byte_limit||bytes>metadata_byte_limit-retained_bytes) {
            issues|=overflow;data={{"handle",handle},{"generation",next},{"alive",true},{"reused",was_reused},
                {"available",false},{"reason","metadata_budget_exceeded"}};bytes=data.dump().size()+64;
        }
        if(bytes>metadata_byte_limit-retained_bytes){issues|=overflow;return;}
        registry.emplace(handle,Record{std::move(data),bytes});retained_bytes+=bytes;
    }
    void forget(Registry kind,std::uint64_t handle) {
        auto& registry=registries[kind];const auto found=registry.find(handle);
        if(found==registry.end())return;
        retained_bytes-=found->second.bytes;registry.erase(found);issues|=reused;
    }
    void destroyed(Registry kind,std::uint64_t handle,bool missing_is_issue=true) {
        auto& registry=registries[kind];const auto found=registry.find(handle);
        if(found==registry.end()){if(handle&&missing_is_issue)issues|=missing;return;}found->second.data["alive"]=false;
    }
    Json stage(const VkPipelineShaderStageCreateInfo& info) {
        Json result={{"stage",static_cast<std::uint32_t>(info.stage)},{"flags",info.flags},{"module",id(info.module)},
            {"entry",entry_name(info.pName,issues)},{"sha256",nullptr},{"codeSize",nullptr},{"moduleGeneration",nullptr}};
        const auto found=registries[shaders].find(id(info.module));
        if(found!=registries[shaders].end()&&found->second.data.at("alive")==true&&found->second.data.contains("sha256")&&
           !found->second.data.at("sha256").is_null()) {
            result["sha256"]=found->second.data.at("sha256");result["codeSize"]=found->second.data.at("codeSize");
            result["moduleGeneration"]=found->second.data.at("generation");
        }else issues|=missing;
        next_info(result,info.pNext,issues);
        result["specializationPresent"]=info.pSpecializationInfo!=nullptr;
        if(info.pSpecializationInfo){issues|=unsupported;result["specializationDecoded"]=false;}
        return result;
    }
    Json metadata(const CommandTraceSnapshot& snapshot) {
        auto flags=issues;if(failed.load())flags|=observation_failure;
        Json result={{"complete",false},{"association","latest registry records at finish; event resource generations are not established"},
            {"imageCoverage",{{"policy","attachment_focused"},{"legacyAttachmentUsageMask",attachment_image_usage},
                {"nonemptyPNextImagesRetained",true},{"viewsRequireLiveRetainedImage",true},{"ignoredHandlesTracked",false}}},
            {"unresolved",Json::array()}};
        for(unsigned kind=0;kind<registry_count;++kind){result[registry_names[kind]]=Json::array();
            for(const auto& value:registries[kind]) {
                auto& records=result[registry_names[kind]];records.push_back(value.second.data);
                if(kind==image_views&&records.back().contains("imageGeneration")) {
                    auto& view=records.back();const auto image=registries[images].find(view.at("image").get<std::uint64_t>());
                    view["imageAssociationKnown"]=view.at("alive")==true&&image!=registries[images].end()&&
                        image->second.data.at("alive")==true&&image->second.data.at("generation")==view.at("imageGeneration");
                }
            }}
        std::set<std::pair<unsigned,std::uint64_t>> missing_handles;
        const auto reference=[&](Registry kind,std::uint64_t handle) {
            if(!handle||registries[kind].count(handle))return;
            flags|=missing;
            if(missing_handles.size()>=max_array_entries){flags|=overflow;return;}
            if(missing_handles.emplace(kind,handle).second)result["unresolved"].push_back({{"kind",registry_singular_names[kind]},{"handle",handle}});
        };
        for(const auto& submit:snapshot.submissions)for(const auto& batch:submit.batches)for(const auto& event:batch.events) {
            switch(event.kind) {
            case CommandTraceKind::pipeline_bind:reference(pipelines,event.args[1]);break;
            case CommandTraceKind::pass_begin:
                reference(passes,event.args[1]);reference(framebuffers,event.args[2]);break;
            case CommandTraceKind::dispatch_indirect:case CommandTraceKind::fill_buffer:case CommandTraceKind::update_buffer:
                reference(buffers,event.args[0]);break;
            case CommandTraceKind::copy_buffer:reference(buffers,event.args[0]);reference(buffers,event.args[1]);break;
            case CommandTraceKind::draw_indexed_indirect:case CommandTraceKind::draw_indexed_indirect_count:
                reference(buffers,event.args[0]);reference(buffers,event.args[4]);break;
            case CommandTraceKind::rendering_begin:
                if(event.rendering) {
                    const auto attachment=[&](const CommandTraceRenderingAttachment& value){reference(image_views,value.image_view);reference(image_views,value.resolve_image_view);};
                    for(const auto& value:event.rendering->colors)attachment(value);
                    if(event.rendering->has_depth)attachment(event.rendering->depth);
                    if(event.rendering->has_stencil)attachment(event.rendering->stencil);
                }
                break;
            case CommandTraceKind::conditional_begin:reference(buffers,event.args[0]);break;
            default:break;
            }
        }
        result["issues"]=issue_names(flags);return result;
    }
    bool open_request() {
        const auto attributes=GetFileAttributesW(directory.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES||!(attributes&FILE_ATTRIBUTE_DIRECTORY)||(attributes&FILE_ATTRIBUTE_REPARSE_POINT))return false;
        const auto handle=CreateFileW(marker.c_str(),FILE_READ_ATTRIBUTES|DELETE,FILE_SHARE_READ|FILE_SHARE_WRITE,
            nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(handle==INVALID_HANDLE_VALUE)return false;
        BY_HANDLE_FILE_INFORMATION info{};
        if(!GetFileInformationByHandle(handle,&info)||(info.dwFileAttributes&(FILE_ATTRIBUTE_DIRECTORY|FILE_ATTRIBUTE_REPARSE_POINT))){CloseHandle(handle);return false;}
        request.value=handle;session.clear();published=write_failures=remove_failures=attempt=0;
        saved_frames={};saved_sequences={};return true;
    }
    bool create_session(std::uint64_t tick) {
        if(!session.empty())return true;
        for(unsigned tries=0;tries<8;++tries) {
            const auto serial=next_directory.fetch_add(1)+1;
            const auto candidate=directory/(L"command-trace-"+std::to_wstring(tick)+L"-"+
                std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(serial));
            if(CreateDirectoryW(candidate.c_str(),nullptr)){session=candidate;return true;}
            if(GetLastError()!=ERROR_ALREADY_EXISTS)return false;
        }
        return false;
    }
    bool publish(const std::string& bytes,std::uint64_t sequence,std::uint64_t tick) {
        if(!create_session(tick))return false;
        const auto slot=std::to_wstring(published+1);
        const auto pending=session/(L"trace-"+slot+L"-"+std::to_wstring(sequence)+L"-"+std::to_wstring(++attempt)+L".pending");
        const auto final=session/(L"trace-"+slot+L".json");
        NativeHandle file(CreateFileW(pending.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr));
        if(file.value==INVALID_HANDLE_VALUE)return false;
        bool okay=true;std::size_t at=0;
        while(at<bytes.size()) {
            const auto count=static_cast<DWORD>(std::min<std::size_t>(bytes.size()-at,1024*1024));DWORD written=0;
            if(!WriteFile(file.value,bytes.data()+at,count,&written,nullptr)||written!=count){okay=false;break;}at+=written;
        }
        if(okay&&!FlushFileBuffers(file.value))okay=false;
        if(!file.close())okay=false;
        return okay&&MoveFileExW(pending.c_str(),final.c_str(),MOVEFILE_WRITE_THROUGH)!=0;
    }
    void complete_request() {
        if(remove_failures>=retry_limit)return;
        FILE_DISPOSITION_INFO disposition{};disposition.DeleteFile=TRUE;
        if(!SetFileInformationByHandle(request.value,FileDispositionInfo,&disposition,sizeof(disposition))){++remove_failures;log("command-trace.request-removal.failed");return;}
        if(!request.close())log("command-trace.request-close.failed");
        session.clear();log("command-trace.saved-three diagnostic-only");
    }
    void finish(const CommandTraceSnapshot& snapshot,std::uint64_t frame) {
        const auto tick=clock();
        if(request.value==INVALID_HANDLE_VALUE) {
            if(polled&&tick>=last_poll&&tick-last_poll<1000)return;
            polled=true;last_poll=tick;if(!open_request())return;
        }
        if(published==3){complete_request();return;}
        if(write_failures>=retry_limit||!snapshot.sequence)return;
        for(unsigned i=0;i<published;++i)if(saved_frames[i]==frame||saved_sequences[i]==snapshot.sequence)return;
        Json output={{"schema",1},{"diagnosticOnly",true},{"complete",false},{"frame",frame},
            {"window","successful CPU submit observations since prior collector finish; not GPU frame attribution"},
            {"provesOwnership",false},{"provesSourceReadiness",false},
            {"incompleteIssues",Json::array({"partial observer scope; descriptor contents, binding variants and barriers are not fully captured",
                "indexed queries, transform feedback and event scopes are not captured; observed scopes do not prove all scopes"})},
            {"snapshot",trace_snapshot(snapshot)},{"metadata",metadata(snapshot)}};
        const auto bytes=output.dump(2)+"\n";
        if(bytes.size()>file_byte_limit||!publish(bytes,snapshot.sequence,tick)) {
            ++write_failures;log("command-trace.publication.failed; request retained");return;
        }
        saved_frames[published]=frame;saved_sequences[published]=snapshot.sequence;++published;
        if(published==3)complete_request();
    }
};

CommandTraceRuntime::CommandTraceRuntime(const std::filesystem::path& path,Clock clock) noexcept {
    try{if(!path.empty())impl_=std::make_unique<Impl>(path,clock);}catch(...){diagnostic_log("command-trace.initialization.failed");}
}
CommandTraceRuntime::~CommandTraceRuntime()=default;
void CommandTraceRuntime::buffer_created(VkBuffer handle,const VkBufferCreateInfo& info) noexcept {
    if(!impl_)return;
    impl_->observe([&]{impl_->record(buffers,id(handle),[&](Json& output){
        output["size"]=info.size;output["usage"]=info.usage;output["usageKnown"]=true;output["flags"]=info.flags;
        output["sharingMode"]=static_cast<std::uint32_t>(info.sharingMode);
        output["declaredQueueFamilyCount"]=info.queueFamilyIndexCount;output["queueFamilies"]=Json::array();
        output["queueFamiliesUsed"]=info.sharingMode==VK_SHARING_MODE_CONCURRENT;
        std::size_t budget=record_unit_limit;
        if(info.sharingMode==VK_SHARING_MODE_CONCURRENT&&array_ok(info.queueFamilyIndexCount,info.pQueueFamilyIndices,impl_->issues,budget))
            for(std::uint32_t i=0;i<info.queueFamilyIndexCount;++i)output["queueFamilies"].push_back(info.pQueueFamilyIndices[i]);
        next_info(output,info.pNext,impl_->issues);
        if(info.pNext){output["usageKnown"]=false;output["usageInterpretation"]="legacy field; pNext effective usage not decoded";}
    });});
}
void CommandTraceRuntime::buffer_destroyed(VkBuffer handle) noexcept {if(impl_)impl_->observe([&]{impl_->destroyed(buffers,id(handle));});}
void CommandTraceRuntime::image_created(VkImage handle,const VkImageCreateInfo& info) noexcept {
    if(!impl_)return;
    impl_->observe([&]{
        if(!(info.usage&attachment_image_usage)&&!info.pNext){impl_->forget(images,id(handle));return;}
        impl_->record(images,id(handle),[&](Json& output){
        output["format"]=static_cast<std::uint32_t>(info.format);output["imageType"]=static_cast<std::uint32_t>(info.imageType);
        output["extent"]=Json::array({info.extent.width,info.extent.height,info.extent.depth});
        output["mipLevels"]=info.mipLevels;output["arrayLayers"]=info.arrayLayers;output["samples"]=static_cast<std::uint32_t>(info.samples);
        output["flags"]=info.flags;output["tiling"]=static_cast<std::uint32_t>(info.tiling);output["usage"]=info.usage;
        output["initialLayout"]=static_cast<std::uint32_t>(info.initialLayout);output["sharingMode"]=static_cast<std::uint32_t>(info.sharingMode);
        output["declaredQueueFamilyCount"]=info.queueFamilyIndexCount;output["queueFamilies"]=Json::array();
        output["queueFamiliesUsed"]=info.sharingMode==VK_SHARING_MODE_CONCURRENT;
        std::size_t budget=record_unit_limit;
        if(info.sharingMode==VK_SHARING_MODE_CONCURRENT&&array_ok(info.queueFamilyIndexCount,info.pQueueFamilyIndices,impl_->issues,budget))
            for(std::uint32_t i=0;i<info.queueFamilyIndexCount;++i)output["queueFamilies"].push_back(info.pQueueFamilyIndices[i]);
        next_chain(output,info.pNext,impl_->issues);output["usageKnown"]=info.pNext==nullptr;output["flagsKnown"]=info.pNext==nullptr;
        output["interpretation"]="declared VkImageCreateInfo fields; undecoded pNext changes may apply";
    });});
}
void CommandTraceRuntime::image_destroyed(VkImage handle) noexcept {if(impl_)impl_->observe([&]{impl_->destroyed(images,id(handle),false);});}
void CommandTraceRuntime::image_view_created(VkImageView handle,const VkImageViewCreateInfo& info) noexcept {
    if(!impl_)return;
    impl_->observe([&]{
        const auto found=impl_->registries[images].find(id(info.image));
        if(found==impl_->registries[images].end()||found->second.data.at("alive")!=true){impl_->forget(image_views,id(handle));return;}
        impl_->record(image_views,id(handle),[&](Json& output){
        output["image"]=id(info.image);output["imageGeneration"]=found->second.data.at("generation");output["imageAssociationKnown"]=true;
        output["imageAssociation"]="creation-time generation; known only while latest image/view records are live and generation matches; no event identity proof";
        output["flags"]=info.flags;output["format"]=static_cast<std::uint32_t>(info.format);output["viewType"]=static_cast<std::uint32_t>(info.viewType);
        output["components"]=Json::array({static_cast<std::uint32_t>(info.components.r),static_cast<std::uint32_t>(info.components.g),
            static_cast<std::uint32_t>(info.components.b),static_cast<std::uint32_t>(info.components.a)});
        const auto& range=info.subresourceRange;
        output["subresource"]={{"aspectMask",range.aspectMask},{"baseMipLevel",range.baseMipLevel},{"levelCount",range.levelCount},
            {"baseArrayLayer",range.baseArrayLayer},{"layerCount",range.layerCount}};
        next_chain(output,info.pNext,impl_->issues);output["interpretation"]="declared VkImageViewCreateInfo fields; undecoded pNext changes may apply";
    });});
}
void CommandTraceRuntime::image_view_destroyed(VkImageView handle) noexcept {if(impl_)impl_->observe([&]{impl_->destroyed(image_views,id(handle),false);});}
void CommandTraceRuntime::shader_created(VkShaderModule handle,const VkShaderModuleCreateInfo& info) noexcept {
    if(!impl_)return;
    impl_->observe([&]{impl_->record(shaders,id(handle),[&](Json& output){
        output["codeSize"]=info.codeSize;output["sha256"]=nullptr;
        if(!info.pCode||!info.codeSize)impl_->issues|=missing;
        else if(info.codeSize>max_shader_bytes)impl_->issues|=overflow;
        else output["sha256"]=fingerprint(info.pCode,info.codeSize);
        next_info(output,info.pNext,impl_->issues);
    });});
}
void CommandTraceRuntime::shader_destroyed(VkShaderModule handle) noexcept {if(impl_)impl_->observe([&]{impl_->destroyed(shaders,id(handle));});}
void CommandTraceRuntime::graphics_pipelines_created(std::uint32_t count,const VkGraphicsPipelineCreateInfo* infos,const VkPipeline* handles) noexcept {
    if(!impl_)return;
    impl_->observe([&]{std::size_t budget=record_unit_limit;
        if(!array_ok(count,infos,impl_->issues,budget)||!array_ok(count,handles,impl_->issues,budget))return;
        for(std::uint32_t i=0;i<count;++i)if(handles[i])impl_->record(pipelines,id(handles[i]),[&](Json& output){
            const auto& info=infos[i];output["type"]="graphics";output["flags"]=info.flags;
            output["layout"]=id(info.layout);output["renderPass"]=id(info.renderPass);output["subpass"]=info.subpass;
            output["stages"]=Json::array();output["declaredStageCount"]=info.stageCount;std::size_t units=record_unit_limit;
            if(array_ok(info.stageCount,info.pStages,impl_->issues,units))for(std::uint32_t n=0;n<info.stageCount;++n)output["stages"].push_back(impl_->stage(info.pStages[n]));
            output["depthCompareOp"]=nullptr;output["depthWriteEnable"]=nullptr;output["depthTestEnable"]=nullptr;
            if(info.pDepthStencilState){output["depthCompareOp"]=static_cast<std::uint32_t>(info.pDepthStencilState->depthCompareOp);
                output["depthWriteEnable"]=info.pDepthStencilState->depthWriteEnable==VK_TRUE;output["depthTestEnable"]=info.pDepthStencilState->depthTestEnable==VK_TRUE;
                if(info.pDepthStencilState->pNext)impl_->issues|=unsupported;}
            else impl_->issues|=missing;
            output["dynamicStates"]=Json::array();output["dynamicStatesKnown"]=true;
            if(info.pDynamicState){const auto& dynamic=*info.pDynamicState;output["declaredDynamicStateCount"]=dynamic.dynamicStateCount;
                const bool known=array_ok(dynamic.dynamicStateCount,dynamic.pDynamicStates,impl_->issues,units);output["dynamicStatesKnown"]=known;
                if(known)for(std::uint32_t n=0;n<dynamic.dynamicStateCount;++n)output["dynamicStates"].push_back(static_cast<std::uint32_t>(dynamic.pDynamicStates[n]));
                if(dynamic.pNext)impl_->issues|=unsupported;}
            pipeline_rendering(output,info,impl_->issues,units);
        });
    });
}
void CommandTraceRuntime::compute_pipelines_created(std::uint32_t count,const VkComputePipelineCreateInfo* infos,const VkPipeline* handles) noexcept {
    if(!impl_)return;
    impl_->observe([&]{std::size_t budget=record_unit_limit;
        if(!array_ok(count,infos,impl_->issues,budget)||!array_ok(count,handles,impl_->issues,budget))return;
        for(std::uint32_t i=0;i<count;++i)if(handles[i])impl_->record(pipelines,id(handles[i]),[&](Json& output){
            const auto& info=infos[i];output["type"]="compute";output["flags"]=info.flags;output["layout"]=id(info.layout);
            output["stages"]=Json::array({impl_->stage(info.stage)});next_info(output,info.pNext,impl_->issues);
        });
    });
}
void CommandTraceRuntime::pipeline_destroyed(VkPipeline handle) noexcept {if(impl_)impl_->observe([&]{impl_->destroyed(pipelines,id(handle));});}
void CommandTraceRuntime::renderpass_created(VkRenderPass handle,const VkRenderPassCreateInfo& info) noexcept {
    if(!impl_)return;
    impl_->observe([&]{impl_->record(passes,id(handle),[&](Json& output){
        output["version"]=1;output["flags"]=info.flags;output["declaredAttachmentCount"]=info.attachmentCount;
        output["declaredSubpassCount"]=info.subpassCount;output["attachments"]=Json::array();output["subpasses"]=Json::array();
        std::size_t budget=record_unit_limit;const bool attachments=array_ok(info.attachmentCount,info.pAttachments,impl_->issues,budget);
        output["attachmentsKnown"]=attachments;
        if(attachments)for(std::uint32_t i=0;i<info.attachmentCount;++i)output["attachments"].push_back(attachment_description(info.pAttachments[i]));
        const bool subpasses=array_ok(info.subpassCount,info.pSubpasses,impl_->issues,budget);output["subpassesKnown"]=subpasses;
        if(subpasses)for(std::uint32_t i=0;i<info.subpassCount;++i)output["subpasses"].push_back(subpass_description(info.pSubpasses[i],impl_->issues,budget));
        next_info(output,info.pNext,impl_->issues);output["dependenciesDecoded"]=false;if(info.dependencyCount)impl_->issues|=unsupported;
    });});
}
void CommandTraceRuntime::renderpass2_created(VkRenderPass handle,const VkRenderPassCreateInfo2& info) noexcept {
    if(!impl_)return;
    impl_->observe([&]{impl_->record(passes,id(handle),[&](Json& output){
        output["version"]=2;output["flags"]=info.flags;output["declaredAttachmentCount"]=info.attachmentCount;
        output["declaredSubpassCount"]=info.subpassCount;output["attachments"]=Json::array();output["subpasses"]=Json::array();
        std::size_t budget=record_unit_limit;const bool attachments=array_ok(info.attachmentCount,info.pAttachments,impl_->issues,budget);
        output["attachmentsKnown"]=attachments;
        if(attachments)for(std::uint32_t i=0;i<info.attachmentCount;++i){auto item=attachment_description(info.pAttachments[i]);next_info(item,info.pAttachments[i].pNext,impl_->issues);output["attachments"].push_back(std::move(item));}
        const bool subpasses=array_ok(info.subpassCount,info.pSubpasses,impl_->issues,budget);output["subpassesKnown"]=subpasses;
        if(subpasses)for(std::uint32_t i=0;i<info.subpassCount;++i){auto item=subpass_description(info.pSubpasses[i],impl_->issues,budget);item["viewMask"]=info.pSubpasses[i].viewMask;next_info(item,info.pSubpasses[i].pNext,impl_->issues);output["subpasses"].push_back(std::move(item));}
        next_info(output,info.pNext,impl_->issues);output["dependenciesDecoded"]=false;if(info.dependencyCount)impl_->issues|=unsupported;
    });});
}
void CommandTraceRuntime::renderpass_destroyed(VkRenderPass handle) noexcept {if(impl_)impl_->observe([&]{impl_->destroyed(passes,id(handle));});}
void CommandTraceRuntime::framebuffer_created(VkFramebuffer handle,const VkFramebufferCreateInfo& info) noexcept {
    if(!impl_)return;
    impl_->observe([&]{impl_->record(framebuffers,id(handle),[&](Json& output){
        output["renderPass"]=id(info.renderPass);output["flags"]=info.flags;output["width"]=info.width;output["height"]=info.height;output["layers"]=info.layers;
        output["declaredAttachmentCount"]=info.attachmentCount;output["views"]=Json::array();
        const bool imageless=(info.flags&VK_FRAMEBUFFER_CREATE_IMAGELESS_BIT)!=0;output["imageless"]=imageless;
        std::size_t budget=record_unit_limit;const bool known=!imageless&&array_ok(info.attachmentCount,info.pAttachments,impl_->issues,budget);output["viewsKnown"]=known;
        if(known)for(std::uint32_t i=0;i<info.attachmentCount;++i)output["views"].push_back(id(info.pAttachments[i]));
        if(imageless)impl_->issues|=missing;
        next_info(output,info.pNext,impl_->issues);
    });});
}
void CommandTraceRuntime::framebuffer_destroyed(VkFramebuffer handle) noexcept {if(impl_)impl_->observe([&]{impl_->destroyed(framebuffers,id(handle));});}
void CommandTraceRuntime::finish_frame(std::uint64_t frame) noexcept {
    const auto snapshot=collector_.finish_frame();
    if(impl_)impl_->observe([&]{
        try{impl_->finish(snapshot,frame);}
        catch(...){if(impl_->write_failures<retry_limit)++impl_->write_failures;impl_->log("command-trace.serialization.failed; request retained");throw;}
    });
}
}
