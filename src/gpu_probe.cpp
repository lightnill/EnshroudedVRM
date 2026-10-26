#define VK_NO_PROTOTYPES
#include "runtime_log.hpp"
#include "gpu_probe.hpp"
#include "native_submit.hpp"
#include "gpu_probe_capture_frames.hpp"

#include <algorithm>
#include <cstring>
#include <filesystem>
#include <map>
#include <set>
#include <sstream>
#include <stdexcept>
#include <unordered_map>
#include <vector>
#include <array>
#include <chrono>
#include <tuple>

namespace evrm {
namespace {
std::wstring environment(const wchar_t* name) {
    const auto size = GetEnvironmentVariableW(name, nullptr, 0);
    if (!size || size > 32768) return {};
    std::wstring value(size, L'\0');
    const auto length = GetEnvironmentVariableW(name, value.data(), size);
    if (!length || length >= size) return {};
    value.resize(length);
    return value;
}
enum class SetKind { unknown, global, model, camera };
bool is_uniform(VkDescriptorType type) {
    return type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER || type == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
}
bool is_storage(VkDescriptorType type) {
    return type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER || type == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC;
}
struct Layout {
    SetKind kind{SetKind::unknown};
    std::vector<std::pair<uint32_t, uint32_t>> dynamic_bindings;
    std::map<uint32_t,uint32_t> binding_counts;
    bool anchor_uniform=false,anchor_dynamic=false;
    bool inline_safe=false;
    std::set<uint32_t> inline_unsafe_bindings;
};
struct DescriptorSet {
    VkDescriptorPool pool{};
    uint64_t version=0,generation=0,optional_epoch=0;
    Layout layout;
    // A direct known b24 write may recover direct capture, but cannot repair
    // other bindings after an unsupported span. Copy trust stays separate.
    bool optional_copy_known=true;
    std::map<std::pair<uint32_t, uint32_t>, VkDescriptorBufferInfo> buffers;
    std::map<std::pair<uint32_t, uint32_t>, uint64_t> buffer_generations;
};
bool within_binding(const Layout& layout,uint32_t binding,uint32_t element,uint32_t count) {
    const auto found=layout.binding_counts.find(binding);
    return found!=layout.binding_counts.end()&&element<=found->second&&count<=found->second-element;
}
struct Buffer { VkDeviceSize size{}; VkBufferUsageFlags usage{};uint64_t generation=0;VkDeviceMemory memory{};bool ordinary=false; };
struct Target { std::string role; VkDescriptorBufferInfo binding{}; };
struct BoundTarget {VkDescriptorSet set{};uint32_t binding=0,dynamic=0;};
struct RecordedDraw {
    VkBuffer buffer{},count_buffer{};VkDeviceSize offset=0,count_offset=0;
    uint32_t count=0,stride=0;
    std::map<std::string,BoundTarget> bindings;
};
struct ComputeEvent {
    enum class Kind {dispatch,unsupported,execute};Kind kind=Kind::dispatch;
    BoundTarget binding;uint64_t set_generation=0;bool valid=false;
    PlayerAnchorDispatch groups;
    VkCommandBuffer child{};uint64_t child_generation=0;
};
struct ComputeCommandState {
    bool known=false,has_binding=false,poisoned=false;
    VkPipelineLayout bound_layout{},expected_layout{};
    BoundTarget binding;uint64_t set_generation=0;
    std::vector<ComputeEvent> events;
};
struct DrawCommandState {bool model=false;VkPipelineLayout bound_layout{},expected_layout{};std::map<std::string,BoundTarget> bindings;std::vector<RecordedDraw> draws;uint64_t generation=0;ComputeCommandState compute;std::size_t inline_boundary=0;bool inline_required=false;};
struct SubmittedDraw {RecordedDraw draw;std::map<std::string,Target> bindings;std::map<VkBuffer,uint64_t> generations;std::map<VkDescriptorSet,uint64_t> versions;VkQueue queue{};};
struct SubmittedAnchor {bool valid=false;Target target;VkDescriptorSet set{};uint64_t version=0,set_generation=0,buffer_generation=0,optional_epoch=0;PlayerAnchorDispatch groups;VkQueue queue{};};
struct CaptureRead { Target target; VkDeviceSize size{},destination{}; };
struct ResolveDraw {std::string role,count_role;uint32_t count=0,stride=0;};
struct CapturePlan {
    GpuSnapshot snapshot;
    std::vector<CaptureRead> reads;
    std::vector<ResolveDraw> draw_reads;
    std::map<VkDescriptorSet,uint64_t> descriptor_leases;
    std::map<VkBuffer,uint64_t> generations;
    std::map<VkBuffer,VkDeviceMemory> memories;
    std::vector<SubmittedAnchor> anchor_leases;
    VkDeviceSize total=0;uint32_t family=0;
    std::vector<CapturePlan> additional;
    std::vector<CaptureRead> transfer_reads;
};
constexpr std::size_t anchor_event_limit=1024,anchor_frame_limit=4096;
constexpr VkDeviceSize readback_budget=32u*1024u*1024u;
template<class Handle> uint64_t bits(Handle handle) {
    uint64_t value{};
    static_assert(sizeof(handle) <= sizeof(value));
    std::memcpy(&value, &handle, sizeof(handle));
    return value;
}
void check(VkResult result, const char* action) {
    if (result != VK_SUCCESS) throw std::runtime_error(std::string(action) + ": " + std::to_string(result));
}
void write_capture_file(const std::filesystem::path& path,const char* data,std::size_t size,bool exclusive) {
    if(!exclusive) {
        std::ofstream output(path,std::ios::binary);output.write(data,static_cast<std::streamsize>(size));output.close();
        if(!output)throw std::runtime_error("capture file write failed");
        return;
    }
    auto file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)throw std::runtime_error("exclusive capture file creation failed");
    try {
        while(size) {
            const auto chunk=static_cast<DWORD>(std::min<std::size_t>(size,1024u*1024u));DWORD written=0;
            if(!WriteFile(file,data,chunk,&written,nullptr)||written!=chunk)throw std::runtime_error("capture file write failed");
            data+=written;size-=written;
        }
        const auto closed=CloseHandle(file);file=INVALID_HANDLE_VALUE;
        if(!closed)throw std::runtime_error("capture file close failed");
    }catch(...){if(file!=INVALID_HANDLE_VALUE)CloseHandle(file);throw;}
}
std::size_t save_capture(const std::filesystem::path& directory,const GpuSnapshot& snapshot,uint64_t frame,bool exclusive,bool include_anchor=true) {
    std::size_t total=0;
    for(const auto& item:snapshot.buffers) {
        if(!include_anchor&&item.first=="player_anchor")continue;
        const auto stem="frame-"+std::to_string(frame)+"-"+item.first;const auto& captured=item.second;
        write_capture_file(directory/(stem+".bin"),reinterpret_cast<const char*>(captured.bytes.data()),captured.bytes.size(),exclusive);
        std::ostringstream metadata;
        metadata<<"{\"frame\":"<<frame<<",\"buffer\":"<<bits(captured.binding.buffer)<<",\"offset\":"<<captured.binding.offset
                <<",\"declaredRange\":"<<captured.binding.range<<",\"capturedBytes\":"<<captured.bytes.size()<<",\"queueFamily\":"<<snapshot.queue_family;
        if(item.first=="culled-instances") {metadata<<",\"drawSpans\":[";bool first=true;for(const auto& span:snapshot.culled_spans){if(!first)metadata<<",";first=false;metadata<<"["<<span.first_instance<<","<<span.instance_count<<"]";}metadata<<"]";}
        if(item.first=="player_anchor") {
            metadata<<",\"candidateCount\":"<<snapshot.player_anchor_candidate_count<<",\"dispatchCount\":"<<snapshot.player_anchor_dispatches.size()<<",\"dispatches\":[";
            bool first=true;for(const auto& groups:snapshot.player_anchor_dispatches){if(!first)metadata<<",";first=false;metadata<<"["<<groups.x<<","<<groups.y<<","<<groups.z<<"]";}metadata<<"]";
        }
        metadata<<"}\n";const auto text=metadata.str();write_capture_file(directory/(stem+".json"),text.data(),text.size(),exclusive);total+=captured.bytes.size();
    }
    return total;
}
bool complete_anchor_snapshot(const GpuSnapshot& snapshot) {
    for(const auto& minimum:std::map<std::string,std::size_t>{{"camera",1024},{"instances",128},{"skin",48},{"mesh-draw",80},{"culled-instances",8},{"player_anchor",2048}}) {
        const auto found=snapshot.buffers.find(minimum.first);if(found==snapshot.buffers.end()||found->second.bytes.size()<minimum.second)return false;
    }
    return snapshot.buffers.at("player_anchor").bytes.size()==2048&&snapshot.player_anchor_candidate_count==1&&
           !snapshot.player_anchor_dispatches.empty()&&!snapshot.culled_spans.empty();
}
bool complete_pose_snapshot(const GpuSnapshot& snapshot){
    for(const auto& minimum:std::map<std::string,std::size_t>{{"camera",1024},{"instances",128},{"skin",48},{"mesh-draw",80},{"culled-instances",8}}){
        const auto found=snapshot.buffers.find(minimum.first);if(found==snapshot.buffers.end()||found->second.bytes.size()<minimum.second)return false;
    }
    return !snapshot.culled_spans.empty();
}
}

struct GpuProbe::Impl {
    VkDevice device{};
    PFN_vkGetDeviceProcAddr gdpa{};
    PFN_vkSetDeviceLoaderData set_loader_data{};
    VkPhysicalDeviceMemoryProperties memory_properties{};
    std::vector<VkQueueFamilyProperties> queue_properties;
    std::filesystem::path directory;
    bool anchor_enabled=false;
    VkDeviceSize uniform_alignment=0;
    std::set<uint64_t> requested_frames;
    std::set<uint64_t> completed_frames;
    std::mutex mutex;
    std::mutex readback_mutex;
    std::mutex capture_mutex;
    bool anchor_polled=false;
    std::chrono::steady_clock::time_point anchor_last_poll{};
    unsigned anchor_remaining=0,anchor_batch_errors=0,anchor_logged_errors=0;
    uint64_t anchor_request_serial=0;
    std::filesystem::path anchor_request_directory;
    std::set<uint64_t> anchor_saved_frames;
    std::unordered_map<VkBuffer, Buffer> buffers;
    uint64_t buffer_generation=0;
    std::unordered_map<VkDescriptorSetLayout, Layout> layouts;
    std::unordered_map<VkDescriptorSet, DescriptorSet> sets;
    uint64_t descriptor_version=0;
    uint64_t descriptor_generation=0,command_generation=0;
    std::unordered_map<VkDescriptorUpdateTemplate, std::vector<VkDescriptorUpdateTemplateEntry>> templates;
    std::unordered_map<VkQueue, uint32_t> queues;
    std::map<std::string, Target> targets;
    std::unordered_map<VkCommandBuffer,DrawCommandState> commands;
    std::map<VkCommandBuffer,VkCommandPool> command_pools;
    std::vector<SubmittedDraw> submitted_draws;
    std::vector<SubmittedAnchor> submitted_anchors;
    std::set<VkQueue> anchor_poisoned_queues;
    uint64_t readback_count=0;
    std::array<double,5> readback_timings{};
    std::set<std::string> logged_layouts;
    std::set<std::string> logged_roles;
    struct AsyncSlot {
        VkBuffer buffer{};VkDeviceMemory memory{};VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};
        void* mapped=nullptr;VkDeviceSize capacity=0;VkQueue queue{};uint32_t family=UINT32_MAX;
        bool pending=false,ready=false;uint64_t epoch=0,started_ms=0,sequence=0;
        bool recorded=false,publishable=true;VkCommandBuffer original{};uint64_t original_generation=0;
        CapturePlan plan;
    };
    std::array<AsyncSlot,3> async_slots;
    VkQueue async_queue{};std::map<std::string,std::size_t> async_limits;
    bool async_disabled=false,async_frame=false;
    uint64_t async_sequence=0,async_enqueued=0,async_decoded=0,async_pending_polls=0,async_skipped=0,async_rejected=0,async_observed=0;
    void resolve_command(VkQueue,VkCommandBuffer,std::vector<SubmittedDraw>&,std::size_t first=0,bool at_boundary=false);
    CapturePlan make_plan(VkQueue,const std::map<std::string,std::size_t>&,const std::vector<SubmittedDraw>&,bool);
    CapturePlan make_single_plan(VkQueue,const std::map<std::string,std::size_t>&,const std::vector<SubmittedDraw>&,bool);
    GpuSnapshot decode_plan(const CapturePlan&,const void*,bool);
    void prepare_slot(AsyncSlot&,VkQueue,CapturePlan);
    void record_copy(VkCommandBuffer,AsyncSlot&,const CapturePlan&);
    void mark_submitted(VkQueue,const std::vector<AsyncSlot*>&,std::uint64_t,bool);
    void cancel_recorded(VkCommandBuffer);
    bool complete_slot(AsyncSlot&,bool wait);
    void release_slot(AsyncSlot&);
    void drain_slot(AsyncSlot&);
    void invalidate_optional_provenance(DescriptorSet& set) {
        set.optional_copy_known=false;
        if(set.layout.anchor_uniform){set.buffers.erase({24,0});set.buffer_generations.erase({24,0});}
        ++set.optional_epoch;
    }
    void anchor_capture_error(const std::string& message) noexcept {
        try {
            if(anchor_logged_errors<8){++anchor_logged_errors;log_event("probe.player-anchor.capture.failed "+message);}
            if(++anchor_batch_errors>=3)anchor_remaining=0;
        }catch(...){}
    }
    void poll_anchor_request() {
        const auto now=std::chrono::steady_clock::now();
        if(anchor_polled&&now-anchor_last_poll<std::chrono::seconds(1))return;
        anchor_polled=true;anchor_last_poll=now;
        if(anchor_remaining)return;
        const auto marker=directory/"capture-player.request";
        std::error_code error;
        const auto exists=std::filesystem::is_regular_file(marker,error);
        if(!exists){if(error&&error!=std::errc::no_such_file_or_directory)throw std::runtime_error("capture marker observation failed");return;}
        std::filesystem::path request_directory;
        for(unsigned attempt=0;attempt<64;++attempt) {
            request_directory=directory/("player-anchor-"+std::to_string(GetTickCount64())+"-"+std::to_string(GetCurrentProcessId())+"-"+std::to_string(++anchor_request_serial));
            if(std::filesystem::create_directory(request_directory,error))break;
            if(error)throw std::runtime_error("unique anchor capture directory creation failed");
            request_directory.clear();
        }
        if(request_directory.empty())throw std::runtime_error("unique anchor capture directory budget exhausted");
        if(!std::filesystem::remove(marker,error)||error)throw std::runtime_error("capture marker removal failed");
        anchor_request_directory=std::move(request_directory);anchor_remaining=3;anchor_batch_errors=0;anchor_saved_frames.clear();
        log_event("probe.player-anchor.capture.requested snapshots=3");
    }
    void record_compute(ComputeCommandState& state,ComputeEvent event) {
        if(state.events.size()>=anchor_event_limit){state.poisoned=true;return;}
        state.events.push_back(std::move(event));
    }
    void submit_compute(VkCommandBuffer command,VkQueue queue,uint64_t expected_generation=0,unsigned depth=0,std::size_t* visited=nullptr) {
        std::size_t initial=0;if(!visited)visited=&initial;
        if(depth>32||++*visited>anchor_frame_limit){anchor_poisoned_queues.insert(queue);return;}
        const auto found=commands.find(command);
        if(found==commands.end()||(depth&&(!expected_generation||found->second.generation!=expected_generation))){anchor_poisoned_queues.insert(queue);return;}
        const auto& state=found->second.compute;
        if(state.poisoned)anchor_poisoned_queues.insert(queue);
        for(const auto& event:state.events) {
            if(event.kind==ComputeEvent::Kind::execute){submit_compute(event.child,queue,event.child_generation,depth+1,visited);continue;}
            if(submitted_anchors.size()>=anchor_frame_limit){anchor_poisoned_queues.insert(queue);return;}
            SubmittedAnchor anchor;anchor.queue=queue;anchor.groups=event.groups;
            if(event.kind==ComputeEvent::Kind::dispatch&&event.valid) {
                const auto set=sets.find(event.binding.set);
                if(set!=sets.end()&&set->second.generation==event.set_generation&&set->second.layout.anchor_uniform) {
                    const auto key=std::make_pair(24u,0u);const auto value=set->second.buffers.find(key);const auto lease=set->second.buffer_generations.find(key);
                    if(value!=set->second.buffers.end()&&lease!=set->second.buffer_generations.end()) {
                        auto view=value->second;const auto buffer=buffers.find(view.buffer);
                        if((!uniform_alignment||view.offset%uniform_alignment==0)&&view.offset<=UINT64_MAX-event.binding.dynamic) {
                            view.offset+=event.binding.dynamic;
                            if(buffer!=buffers.end()&&lease->second==buffer->second.generation&&
                               (buffer->second.usage&VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT)&&(buffer->second.usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)&&
                               view.offset<=buffer->second.size&&buffer->second.size-view.offset>=2048&&view.range>=2048&&
                               (view.range==VK_WHOLE_SIZE||view.range<=buffer->second.size-view.offset)) {
                                anchor.valid=true;anchor.target={"player_anchor",view};anchor.set=event.binding.set;
                                anchor.version=set->second.version;anchor.set_generation=set->second.generation;anchor.buffer_generation=buffer->second.generation;anchor.optional_epoch=set->second.optional_epoch;
                            }
                        }
                    }
                }
            }
            submitted_anchors.push_back(std::move(anchor));
        }
    }
    bool anchor_live(const SubmittedAnchor& anchor) const {
        if(!anchor.valid)return false;
        const auto set=sets.find(anchor.set);const auto buffer=buffers.find(anchor.target.binding.buffer);
        return set!=sets.end()&&set->second.generation==anchor.set_generation&&set->second.version==anchor.version&&set->second.optional_epoch==anchor.optional_epoch&&
               buffer!=buffers.end()&&buffer->second.generation==anchor.buffer_generation;
    }
    template<class Function> Function proc(const char* name) const { return reinterpret_cast<Function>(gdpa(device, name)); }
};

void GpuProbe::Impl::resolve_command(VkQueue queue,VkCommandBuffer handle,std::vector<SubmittedDraw>& destination,std::size_t first,bool at_boundary) {
    const auto command=commands.find(handle);if(command==commands.end())return;
    // A missed boundary cannot be recovered from scratch after a later view.
    if(command->second.inline_required&&!at_boundary)return;
        for(std::size_t i=first;i<command->second.draws.size();++i){const auto& draw=command->second.draws[i];SubmittedDraw resolved;resolved.draw=draw;resolved.queue=queue;
            for(const auto& ref:draw.bindings){const auto set=sets.find(ref.second.set);if(set==sets.end())continue;const auto binding=set->second.buffers.find({ref.second.binding,0});if(binding==set->second.buffers.end())continue;
                auto value=binding->second;if(value.offset>UINT64_MAX-ref.second.dynamic)continue;value.offset+=ref.second.dynamic;resolved.bindings[ref.first]={ref.first,value};resolved.versions[ref.second.set]=set->second.version;}
            for(const auto& target:resolved.bindings){const auto buffer=buffers.find(target.second.binding.buffer);resolved.generations[target.second.binding.buffer]=buffer==buffers.end()?0:buffer->second.generation;}
            for(auto handle:{draw.buffer,draw.count_buffer})if(handle){const auto buffer=buffers.find(handle);resolved.generations[handle]=buffer==buffers.end()?0:buffer->second.generation;}
            destination.push_back(std::move(resolved));
        }
 }

CapturePlan GpuProbe::Impl::make_plan(VkQueue queue,const std::map<std::string,std::size_t>& limits,const std::vector<SubmittedDraw>& draws,bool asynchronous) {
    using Key=std::tuple<std::uint64_t,VkDeviceSize,VkDeviceSize>;
    std::vector<Key> keys;
    for(auto it=draws.rbegin();it!=draws.rend();++it){
        const auto found=it->bindings.find("instances");if(it->queue!=queue||found==it->bindings.end())continue;
        const auto& b=found->second.binding;const Key key{bits(b.buffer),b.offset,b.range};
        if(std::find(keys.begin(),keys.end(),key)==keys.end())keys.push_back(key);
        if(keys.size()>4)throw std::runtime_error("Independent view count exceeds capture budget");
    }
    if(keys.size()<2)return make_single_plan(queue,limits,draws,asynchronous);
    CapturePlan result;
    for(const auto& key:keys){
        std::vector<SubmittedDraw> selected;
        for(const auto& draw:draws){const auto found=draw.bindings.find("instances");if(draw.queue!=queue||found==draw.bindings.end())continue;
            const auto& b=found->second.binding;if(Key{bits(b.buffer),b.offset,b.range}==key)selected.push_back(draw);}
        auto view=make_single_plan(queue,limits,selected,asynchronous);if(view.reads.empty())continue;
        if(result.reads.empty()){result=std::move(view);continue;}
        const auto offset=(result.total+15u)&~VkDeviceSize(15u);
        if(view.total>readback_budget||offset>readback_budget-view.total)throw std::runtime_error("Combined views exceed capture budget");
        if(result.transfer_reads.empty())result.transfer_reads=result.reads;
        for(auto& read:view.reads){read.destination+=offset;result.transfer_reads.push_back(read);}
        result.total=offset+view.total;
        result.generations.insert(view.generations.begin(),view.generations.end());
        result.memories.insert(view.memories.begin(),view.memories.end());
        result.additional.push_back(std::move(view));
    }
    return result;
}
CapturePlan GpuProbe::Impl::make_single_plan(VkQueue queue,const std::map<std::string,std::size_t>& limits,const std::vector<SubmittedDraw>& draws,bool asynchronous) {
    CapturePlan plan;auto& snapshot=plan.snapshot;auto& reads=plan.reads;auto& draw_reads=plan.draw_reads;
    auto& descriptor_leases=plan.descriptor_leases;auto& anchor_leases=plan.anchor_leases;
    auto& family=plan.family;auto& total=plan.total;
        {
            std::lock_guard<std::mutex> lock(mutex);
            const auto observed_queue = queues.find(queue);
            if (observed_queue == queues.end()) return plan;
            family = observed_queue->second;
            if (family >= queue_properties.size() || !(queue_properties[family].queueFlags & (VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT | VK_QUEUE_TRANSFER_BIT))) return plan;
            const SubmittedDraw* latest=nullptr;
            for(auto it=draws.rbegin();it!=draws.rend();++it)if(it->queue==queue){latest=&*it;break;}
            // Skin-only standalone diagnostics can use legacy binding capture.
            // Model buffers require actual draws on this same render queue.
            if(!latest&&limits.count("culled-instances"))return plan;
            const auto& targets=latest?latest->bindings:this->targets;
            auto validate_generations=[&](const SubmittedDraw& draw){
                for(const auto& generation:draw.generations){const auto buffer=buffers.find(generation.first);if(!generation.second||buffer==buffers.end()||buffer->second.generation!=generation.second)throw std::runtime_error("Submitted draw references retired/reused buffer");}
                for(const auto& version:draw.versions){const auto set=sets.find(version.first);if(set==sets.end()||set->second.version!=version.second)throw std::runtime_error("Descriptor contents changed after draw submission");descriptor_leases[version.first]=version.second;}
            };
            if(latest)validate_generations(*latest);
            for (const auto& item : targets) {
                if (!limits.count(item.first) || !limits.at(item.first)) continue;
                const auto& binding = item.second.binding;
                const auto buffer = buffers.find(binding.buffer);
                if (buffer == buffers.end() || !(buffer->second.usage & VK_BUFFER_USAGE_TRANSFER_SRC_BIT) || binding.offset >= buffer->second.size) continue;
                const auto available = buffer->second.size - binding.offset;
                const auto size = std::min<VkDeviceSize>(std::min(available, binding.range), static_cast<VkDeviceSize>(limits.at(item.first)));
                if (!size) continue;
                total = (total + 15u) & ~VkDeviceSize(15u);
                if(size>readback_budget||total>readback_budget-size)throw std::runtime_error("Pose readback exceeds budget");
                reads.push_back({item.second, size, total});
                total += size;
            }
            if(limits.count("culled-instances")&&targets.count("culled-instances")) {
                const auto same=[](const VkDescriptorBufferInfo& a,const VkDescriptorBufferInfo& b){return a.buffer==b.buffer&&a.offset==b.offset&&a.range==b.range;};
                auto add_read=[&](const std::string& role,VkBuffer buffer,VkDeviceSize offset,VkDeviceSize size){
                    const auto found=buffers.find(buffer);
                    if(found==buffers.end()||!(found->second.usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)||offset>found->second.size||size>found->second.size-offset)throw std::runtime_error("Indirect command read exceeds live buffer");
                    total=(total+15u)&~VkDeviceSize(15u);if(size>16u*1024u*1024u||total>32u*1024u*1024u-size)throw std::runtime_error("Indirect command readback exceeds budget");
                    reads.push_back({{role,{buffer,offset,size}},size,total});total+=size;
                };
                for(const auto& submitted:draws) {
                    if(submitted.queue!=queue)continue;
                    bool matches=true;
                    for(const auto* role:{"camera","skin","instances","mesh-draw","culled-instances"}){
                        const auto a=targets.find(role),b=submitted.bindings.find(role);
                        if(a==targets.end()||b==submitted.bindings.end()||!same(a->second.binding,b->second.binding)){matches=false;break;}
                    }
                    if(!matches)continue;
                    validate_generations(submitted);
                    const auto& draw=submitted.draw;
                    const auto stride=draw.count==1?uint32_t(sizeof(VkDrawIndexedIndirectCommand)):draw.stride;
                    if(!draw.count||draw.count>262144||stride<sizeof(VkDrawIndexedIndirectCommand)||stride%4||stride>4096||draw.offset%4||draw.count_offset%4)throw std::runtime_error("Unsupported indirect command stride/count/alignment");
                    if(draw_reads.size()>=4096)throw std::runtime_error("Too many submitted indirect ranges");
                    ResolveDraw resolve;resolve.role="draw-indirect-"+std::to_string(draw_reads.size());resolve.count=draw.count;resolve.stride=stride;
                    add_read(resolve.role,draw.buffer,draw.offset,VkDeviceSize(draw.count-1)*stride+sizeof(VkDrawIndexedIndirectCommand));
                    if(draw.count_buffer){resolve.count_role=resolve.role+"-count";add_read(resolve.count_role,draw.count_buffer,draw.count_offset,4);}
                    draw_reads.push_back(std::move(resolve));
                }
            }
            // The optional diagnostic is considered only after all mandatory
            // roles and indirect helper reads have established their budget.
            const auto anchor_limit=limits.find("player_anchor");
            if(latest&&anchor_enabled&&anchor_limit!=limits.end()&&anchor_limit->second>=2048) {
                bool valid=!anchor_poisoned_queues.count(queue);
                std::map<std::tuple<uint64_t,VkDeviceSize,VkDeviceSize>,Target> candidates;
                for(const auto& anchor:submitted_anchors) {
                    if(anchor.queue!=queue)continue;
                    if(!anchor_live(anchor)){valid=false;continue;}
                    const auto& binding=anchor.target.binding;
                    candidates.emplace(std::make_tuple(bits(binding.buffer),binding.offset,binding.range),anchor.target);
                    anchor_leases.push_back(anchor);
                }
                snapshot.player_anchor_candidate_count=static_cast<uint32_t>(candidates.size());
                const auto destination=(total+15u)&~VkDeviceSize(15u);
                if(valid&&candidates.size()==1&&destination<=readback_budget-2048) {
                    reads.push_back({candidates.begin()->second,2048,destination});total=destination+2048;
                    for(const auto& anchor:anchor_leases)snapshot.player_anchor_dispatches.push_back(anchor.groups);
                }else anchor_leases.clear();
            }
    if(asynchronous) {
        for(const auto& read:reads) {
            const auto found=buffers.find(read.target.binding.buffer);
            if(found==buffers.end()||!found->second.memory||!found->second.ordinary)throw std::runtime_error("Async source has no ordinary live binding");
            plan.generations[found->first]=found->second.generation;plan.memories[found->first]=found->second.memory;
        }
    }
        }
    return plan;
}

GpuSnapshot GpuProbe::Impl::decode_plan(const CapturePlan& plan,const void* data,bool asynchronous) {
    auto snapshot=plan.snapshot;const auto family=plan.family;
    const auto& reads=plan.reads;const auto& draw_reads=plan.draw_reads;
    const auto& descriptor_leases=plan.descriptor_leases;const auto& anchor_leases=plan.anchor_leases;
    if(asynchronous) {
        std::lock_guard<std::mutex> lock(mutex);
        for(const auto& generation:plan.generations){const auto found=buffers.find(generation.first);if(found==buffers.end()||found->second.generation!=generation.second||found->second.memory!=plan.memories.at(generation.first))throw std::runtime_error("Async source retired or rebound");}
    }
            snapshot.queue_family = family;
            for (const auto& read : reads) {
                const auto* first = static_cast<const std::uint8_t*>(data) + read.destination;
                snapshot.buffers[read.target.role] = {read.target.binding, std::vector<std::uint8_t>(first, first + read.size)};
            }
        {std::lock_guard<std::mutex> lock(mutex);
            for(const auto& version:descriptor_leases){const auto set=sets.find(version.first);if(set==sets.end()||set->second.version!=version.second)throw std::runtime_error("Descriptor contents changed during pose readback");}
            bool anchor_valid=true;for(const auto& anchor:anchor_leases)if(!anchor_live(anchor)){anchor_valid=false;break;}
            if(!anchor_valid){snapshot.buffers.erase("player_anchor");snapshot.player_anchor_dispatches.clear();}
        }
        for(const auto& draw:draw_reads) {
            auto count=draw.count;
            if(!draw.count_role.empty()){uint32_t actual=0;std::memcpy(&actual,snapshot.buffers.at(draw.count_role).bytes.data(),4);count=std::min(count,actual);}
            const auto& commands=snapshot.buffers.at(draw.role).bytes;
            const auto culled_count=snapshot.buffers.at("culled-instances").bytes.size()/8;
            for(uint32_t i=0;i<count;++i){VkDrawIndexedIndirectCommand command{};std::memcpy(&command,commands.data()+std::size_t(i)*draw.stride,sizeof(command));
                if(!command.indexCount||!command.instanceCount)continue;
                if(uint64_t(command.firstInstance)+command.instanceCount>culled_count)throw std::runtime_error("Drawn culled span exceeds captured descriptor");
                snapshot.culled_spans.push_back({command.firstInstance,command.instanceCount});
            }
        }
    for(const auto& view:plan.additional)snapshot.additional_views.push_back(decode_plan(view,data,asynchronous));
    return snapshot;
}

GpuProbe::GpuProbe(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                   PFN_vkGetInstanceProcAddr gipa, PFN_vkGetDeviceProcAddr gdpa,
                   PFN_vkSetDeviceLoaderData set_loader_data) : impl(std::make_unique<Impl>()) {
    impl->device = device; impl->gdpa = gdpa; impl->set_loader_data = set_loader_data;
    const auto path = environment(L"ENSHROUDED_VRM_CAPTURE_DIR");
    if (path.empty()) return;
    impl->directory = path;
    impl->anchor_enabled=environment(L"ENSHROUDED_VRM_CAPTURE_PLAYER_ANCHOR")==L"1";
    std::filesystem::create_directories(impl->directory);
    impl->requested_frames=probe_capture_frames(environment(L"ENSHROUDED_VRM_CAPTURE_FRAMES"));
    const auto memory = reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance, "vkGetPhysicalDeviceMemoryProperties"));
    const auto families = reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance, "vkGetPhysicalDeviceQueueFamilyProperties"));
    if (!memory || !families) throw std::runtime_error("missing physical-device observation functions");
    memory(physical, &impl->memory_properties);
    uint32_t count{};
    families(physical, &count, nullptr);
    impl->queue_properties.resize(count);
    families(physical, &count, impl->queue_properties.data());
    if(impl->anchor_enabled) {
        const auto properties=reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance,"vkGetPhysicalDeviceProperties"));
        if(properties){VkPhysicalDeviceProperties value{};properties(physical,&value);impl->uniform_alignment=value.limits.minUniformBufferOffsetAlignment;}
    }
    log_event("probe.enabled");
}
GpuProbe::~GpuProbe() {
    std::lock_guard<std::mutex> lock(impl->readback_mutex);
    for(auto& slot:impl->async_slots){impl->drain_slot(slot);impl->release_slot(slot);}
}
bool GpuProbe::enabled() const { return !impl->directory.empty(); }
bool GpuProbe::player_anchor_capture_enabled() const {return enabled()&&impl->anchor_enabled;}
void GpuProbe::capture_snapshot(const GpuSnapshot& snapshot,uint64_t frame) noexcept {
    if(!player_anchor_capture_enabled())return;
    try {
        std::lock_guard<std::mutex> lock(impl->capture_mutex);
        try {
            impl->poll_anchor_request();
            if(!impl->anchor_remaining||impl->anchor_saved_frames.count(frame)||!complete_pose_snapshot(snapshot))return;
            if(snapshot.additional_views.size()>3)throw std::runtime_error("Diagnostic view budget exceeded");
            std::size_t budget=0;
            const auto count_bytes=[&](const GpuSnapshot& view){for(const auto& buffer:view.buffers){
                if(buffer.second.bytes.size()>readback_budget-budget)throw std::runtime_error("Diagnostic byte budget exceeded");
                budget+=buffer.second.bytes.size();
            }};
            count_bytes(snapshot);
            for(const auto& view:snapshot.additional_views){if(!view.additional_views.empty())throw std::runtime_error("Nested diagnostic views");count_bytes(view);}
            const auto directory=impl->anchor_request_directory/("frame-"+std::to_string(frame));
            if(!std::filesystem::create_directory(directory))throw std::runtime_error("snapshot directory already exists");
            auto total=save_capture(directory,snapshot,frame,true,complete_anchor_snapshot(snapshot));
            for(std::size_t i=0;i<snapshot.additional_views.size();++i){
                const auto child=directory/("view-"+std::to_string(i+1));
                if(!std::filesystem::create_directory(child))throw std::runtime_error("diagnostic view directory already exists");
                total+=save_capture(child,snapshot.additional_views[i],frame,true,false);
            }
            const auto manifest="{\"complete\":true,\"frame\":"+std::to_string(frame)+",\"queueFamily\":"+std::to_string(snapshot.queue_family)+
                ",\"bufferCount\":"+std::to_string(snapshot.buffers.size())+",\"viewCount\":"+std::to_string(snapshot.additional_views.size()+1)+",\"capturedBytes\":"+std::to_string(total)+"}\n";
            write_capture_file(directory/"snapshot.pending",manifest.data(),manifest.size(),true);
            if(!MoveFileW((directory/"snapshot.pending").c_str(),(directory/"snapshot.json").c_str()))throw std::runtime_error("complete snapshot manifest publication failed");
            impl->anchor_saved_frames.insert(frame);--impl->anchor_remaining;
            log_event("probe.player-anchor.captured frame="+std::to_string(frame)+" remaining="+std::to_string(impl->anchor_remaining));
        }catch(const std::exception& error){impl->anchor_capture_error(error.what());}
        catch(...){impl->anchor_capture_error("unknown");}
    }catch(...){}
}
bool GpuProbe::needs_transfer_source(const VkBufferCreateInfo& info) const {
    return enabled() && info.size <= 64u * 1024u * 1024u &&
           (info.usage & (VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT)) &&
           !(info.flags & (VK_BUFFER_CREATE_PROTECTED_BIT | VK_BUFFER_CREATE_SPARSE_BINDING_BIT));
}
void GpuProbe::buffer_created(VkBuffer buffer, const VkBufferCreateInfo& info) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->buffers[buffer] = {info.size, info.usage,++impl->buffer_generation,VK_NULL_HANDLE,!info.pNext&&!info.flags};
}
void GpuProbe::buffer_destroyed(VkBuffer buffer) {
    if (!enabled()) return;
    {
        std::lock_guard<std::mutex> lock(impl->mutex);
        impl->buffers.erase(buffer);
        for(auto it=impl->targets.begin();it!=impl->targets.end();) {
            if(it->second.binding.buffer==buffer)it=impl->targets.erase(it);
            else ++it;
        }
    }
    // Native destruction happens after this callback. Metadata retirement stops
    // new copies, while this guard lets a copy already in flight finish first.
    std::lock_guard<std::mutex> completed(impl->readback_mutex);
    for(auto& slot:impl->async_slots)if(slot.plan.generations.count(buffer)){impl->drain_slot(slot);slot.ready=false;slot.plan={};}
}
void GpuProbe::layout_created(VkDescriptorSetLayout handle, const VkDescriptorSetLayoutCreateInfo& info) {
    if (!enabled()) return;
    std::map<uint32_t, VkDescriptorSetLayoutBinding> bindings;
    for (uint32_t i = 0; i < info.bindingCount; ++i) bindings.emplace(info.pBindings[i].binding, info.pBindings[i]);
    auto type = [&](uint32_t binding) { const auto found = bindings.find(binding); return found == bindings.end() ? VK_DESCRIPTOR_TYPE_MAX_ENUM : found->second.descriptorType; };
    Layout layout;
    layout.inline_safe=!(info.flags&~VK_DESCRIPTOR_SET_LAYOUT_CREATE_UPDATE_AFTER_BIND_POOL_BIT);
    if(info.pNext){const auto* flags=static_cast<const VkDescriptorSetLayoutBindingFlagsCreateInfo*>(info.pNext);
        layout.inline_safe=layout.inline_safe&&flags->sType==VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO&&
            !flags->pNext&&flags->bindingCount==info.bindingCount&&(!flags->bindingCount||flags->pBindingFlags);
        if(layout.inline_safe)for(uint32_t i=0;i<flags->bindingCount;++i)if(flags->pBindingFlags[i]&~VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT)
            layout.inline_unsafe_bindings.insert(info.pBindings[i].binding);
    }
    for(const auto& binding:bindings)layout.binding_counts[binding.first]=binding.second.descriptorCount;
    const auto anchor=bindings.find(24);
    layout.anchor_uniform=anchor!=bindings.end()&&anchor->second.descriptorCount==1&&is_uniform(anchor->second.descriptorType);
    layout.anchor_dynamic=layout.anchor_uniform&&anchor->second.descriptorType==VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC;
    bool model = true;
    for (uint32_t i = 0; i <= 10; ++i) model = model && is_storage(type(i));
    if (is_uniform(type(0)) && is_storage(type(1)) && is_storage(type(2)) && is_storage(type(3)) &&
        is_storage(type(4)) && is_storage(type(5)) && is_storage(type(6)) && is_uniform(type(7))) layout.kind = SetKind::global;
    else if (model) layout.kind = SetKind::model;
    else if (is_uniform(type(16))) layout.kind = SetKind::camera;
    for (const auto& item : bindings)
        if (item.second.descriptorType == VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC || item.second.descriptorType == VK_DESCRIPTOR_TYPE_STORAGE_BUFFER_DYNAMIC)
            for (uint32_t index = 0; index < item.second.descriptorCount; ++index) layout.dynamic_bindings.emplace_back(item.first, index);
    std::lock_guard<std::mutex> lock(impl->mutex);
    std::string signature;
    for (const auto& item : bindings) signature += std::to_string(item.first) + ":" + std::to_string(item.second.descriptorType) + ":" + std::to_string(item.second.descriptorCount) + ",";
    if (impl->logged_layouts.size() < 64 && impl->logged_layouts.insert(signature).second)
        log_event("probe.layout kind=" + std::to_string(static_cast<int>(layout.kind)) + " bindings=" + signature);
    impl->layouts[handle] = std::move(layout);
}
void GpuProbe::layout_destroyed(VkDescriptorSetLayout handle) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> lock(impl->mutex); impl->layouts.erase(handle);
}
void GpuProbe::sets_allocated(const VkDescriptorSetAllocateInfo& info, const VkDescriptorSet* sets) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    for (uint32_t i = 0; i < info.descriptorSetCount; ++i) {
        DescriptorSet set; set.pool = info.descriptorPool;set.version=++impl->descriptor_version;set.generation=++impl->descriptor_generation;
        const auto layout = impl->layouts.find(info.pSetLayouts[i]);
        if (layout != impl->layouts.end()) set.layout = layout->second;
        impl->sets[sets[i]] = std::move(set);
    }
}
void GpuProbe::sets_freed(uint32_t count, const VkDescriptorSet* sets) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    for (uint32_t i = 0; i < count; ++i) impl->sets.erase(sets[i]);
}
void GpuProbe::pool_reset(VkDescriptorPool pool) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    for (auto it = impl->sets.begin(); it != impl->sets.end();) {
        if (it->second.pool == pool) it = impl->sets.erase(it); else ++it;
    }
}
void GpuProbe::descriptors_written(uint32_t count, const VkWriteDescriptorSet* writes) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& write = writes[i];
        const auto found = impl->sets.find(write.dstSet);
        if(found==impl->sets.end())continue;
        const bool ambiguous=impl->anchor_enabled&&
            !within_binding(found->second.layout,write.dstBinding,write.dstArrayElement,write.descriptorCount);
        if (!write.pBufferInfo || (!is_uniform(write.descriptorType) && !is_storage(write.descriptorType))) {
            if(ambiguous)impl->invalidate_optional_provenance(found->second);
            continue;
        }
        found->second.version=++impl->descriptor_version;
        for (uint32_t j = 0; j < write.descriptorCount; ++j) {
            found->second.buffers[{write.dstBinding, write.dstArrayElement + j}] = write.pBufferInfo[j];
            if(impl->anchor_enabled){const auto buffer=impl->buffers.find(write.pBufferInfo[j].buffer);
                found->second.buffer_generations[{write.dstBinding,write.dstArrayElement+j}]=buffer==impl->buffers.end()?0:buffer->second.generation;}
        }
        if(ambiguous)impl->invalidate_optional_provenance(found->second);
    }
}
void GpuProbe::descriptors_copied(uint32_t count, const VkCopyDescriptorSet* copies) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    for (uint32_t i = 0; i < count; ++i) {
        const auto& copy = copies[i];
        auto source = impl->sets.find(copy.srcSet), destination = impl->sets.find(copy.dstSet);
        if(destination==impl->sets.end())continue;
        bool ambiguous=impl->anchor_enabled&&(source==impl->sets.end()||!source->second.optional_copy_known||
            !within_binding(destination->second.layout,copy.dstBinding,copy.dstArrayElement,copy.descriptorCount)||
            !within_binding(source->second.layout,copy.srcBinding,copy.srcArrayElement,copy.descriptorCount));
        if(source==impl->sets.end()){if(ambiguous)impl->invalidate_optional_provenance(destination->second);continue;}
        destination->second.version=++impl->descriptor_version;
        for (uint32_t j = 0; j < copy.descriptorCount; ++j) {
            const auto value = source->second.buffers.find({copy.srcBinding, copy.srcArrayElement + j});
            if (value != source->second.buffers.end()) {
                destination->second.buffers[{copy.dstBinding, copy.dstArrayElement + j}] = value->second;
                if(impl->anchor_enabled){const auto generation=source->second.buffer_generations.find({copy.srcBinding,copy.srcArrayElement+j});
                    destination->second.buffer_generations[{copy.dstBinding,copy.dstArrayElement+j}]=generation==source->second.buffer_generations.end()?0:generation->second;
                    if(generation==source->second.buffer_generations.end()||!generation->second)ambiguous=true;}
            }else if(impl->anchor_enabled)ambiguous=true;
        }
        if(ambiguous)impl->invalidate_optional_provenance(destination->second);
    }
}
void GpuProbe::template_created(VkDescriptorUpdateTemplate handle, const VkDescriptorUpdateTemplateCreateInfo& info) {
    if (!enabled() || info.templateType != VK_DESCRIPTOR_UPDATE_TEMPLATE_TYPE_DESCRIPTOR_SET) return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    impl->templates[handle] = {info.pDescriptorUpdateEntries, info.pDescriptorUpdateEntries + info.descriptorUpdateEntryCount};
}
void GpuProbe::template_destroyed(VkDescriptorUpdateTemplate handle) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> lock(impl->mutex); impl->templates.erase(handle);
}
void GpuProbe::template_written(VkDescriptorSet handle, VkDescriptorUpdateTemplate update, const void* data) {
    if (!enabled() || !data) return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    const auto entries = impl->templates.find(update);
    auto set = impl->sets.find(handle);
    if (entries == impl->templates.end() || set == impl->sets.end()) return;
    set->second.version=++impl->descriptor_version;
    bool ambiguous=false;
    for (const auto& entry : entries->second) {
        if(impl->anchor_enabled&&
           !within_binding(set->second.layout,entry.dstBinding,entry.dstArrayElement,entry.descriptorCount))ambiguous=true;
        if (!is_uniform(entry.descriptorType) && !is_storage(entry.descriptorType)) continue;
        for (uint32_t i = 0; i < entry.descriptorCount; ++i) {
            VkDescriptorBufferInfo buffer{};
            std::memcpy(&buffer, static_cast<const char*>(data) + entry.offset + i * entry.stride, sizeof(buffer));
            set->second.buffers[{entry.dstBinding, entry.dstArrayElement + i}] = buffer;
            if(impl->anchor_enabled){const auto native=impl->buffers.find(buffer.buffer);
                set->second.buffer_generations[{entry.dstBinding,entry.dstArrayElement+i}]=native==impl->buffers.end()?0:native->second.generation;}
        }
    }
    if(ambiguous)impl->invalidate_optional_provenance(set->second);
}
void GpuProbe::sets_bound(VkCommandBuffer command,VkPipelineBindPoint point,VkPipelineLayout layout,uint32_t first_set,uint32_t count, const VkDescriptorSet* handles,
                         uint32_t dynamic_count, const uint32_t* offsets) {
    if (!enabled()) return;
    if(point==VK_PIPELINE_BIND_POINT_COMPUTE) {
        if(!impl->anchor_enabled)return;
        std::lock_guard<std::mutex> lock(impl->mutex);
        auto& command_state=impl->commands[command];if(!command_state.generation)command_state.generation=++impl->command_generation;
        auto& state=command_state.compute;
        if(state.bound_layout!=layout)state.has_binding=false;
        state.bound_layout=layout;
        uint64_t required=0;bool valid=(!count||handles)&&(!dynamic_count||offsets);
        for(uint32_t i=0;i<count&&handles;++i){const auto set=impl->sets.find(handles[i]);if(set==impl->sets.end()){valid=false;continue;}required+=set->second.layout.dynamic_bindings.size();}
        valid=valid&&required==dynamic_count;
        if(!valid)state.has_binding=false;
        uint64_t dynamic_index=0;
        for(uint32_t i=0;i<count&&handles;++i) {
            const auto slot=uint64_t(first_set)+i;if(slot==2)state.has_binding=false;
            const auto set=impl->sets.find(handles[i]);if(set==impl->sets.end())continue;
            uint32_t anchor_dynamic=0;
            for(const auto& binding:set->second.layout.dynamic_bindings) {
                if(binding==std::make_pair(24u,0u)&&dynamic_index<dynamic_count&&offsets)anchor_dynamic=offsets[dynamic_index];
                ++dynamic_index;
            }
            if(slot!=2||!valid||!set->second.layout.anchor_uniform)continue;
            if(set->second.layout.anchor_dynamic&&(!impl->uniform_alignment||anchor_dynamic%impl->uniform_alignment))continue;
            const auto value=set->second.buffers.find({24,0});if(value==set->second.buffers.end())continue;
            if(impl->uniform_alignment&&value->second.offset%impl->uniform_alignment)continue;
            state.binding={handles[i],24,anchor_dynamic};state.set_generation=set->second.generation;state.has_binding=true;
        }
        return;
    }
    if(point!=VK_PIPELINE_BIND_POINT_GRAPHICS)return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    auto& command_state=impl->commands[command];
    if(command_state.bound_layout!=layout)command_state.bindings.clear();
    command_state.bound_layout=layout;
    uint32_t dynamic_index{};
    for (uint32_t i = 0; i < count; ++i) {
        const auto slot=uint64_t(first_set)+i;
        auto clear_role=[&](const char* role){command_state.bindings.erase(role);impl->targets.erase(role);};
        if(slot==0){clear_role("skin");clear_role("previous-skin");}
        if(slot==1)clear_role("camera");
        if(slot==2){clear_role("mesh-draw");clear_role("instances");clear_role("culled-instances");}
        const auto set = impl->sets.find(handles[i]);
        if (set == impl->sets.end()) continue;
        std::map<std::pair<uint32_t, uint32_t>, uint32_t> dynamics;
        for (const auto& binding : set->second.layout.dynamic_bindings) {
            if (dynamic_index < dynamic_count) dynamics[binding] = offsets[dynamic_index];
            ++dynamic_index;
        }
        auto take = [&](uint32_t binding, const char* role) {
            const auto key = std::make_pair(binding, 0u);
            const auto found = set->second.buffers.find(key);
            if (found == set->second.buffers.end()) return;
            auto value = found->second;
            const auto dynamic = dynamics.find(key);
            if (dynamic != dynamics.end()) value.offset += dynamic->second;
            if (impl->logged_roles.insert(role).second) log_event("probe.binding.available role=" + std::string(role));
            impl->targets[role] = {role, value};
            command_state.bindings[role]={handles[i],binding,dynamic==dynamics.end()?0:dynamic->second};
        };
        if(slot==0&&set->second.layout.kind==SetKind::global){take(5,"skin");take(6,"previous-skin");}
        if(slot==1&&set->second.layout.kind==SetKind::camera)take(16,"camera");
        if(slot==2&&set->second.layout.kind==SetKind::model){take(1,"mesh-draw");take(4,"instances");take(5,"culled-instances");}
    }
}
void GpuProbe::command_begin(VkCommandBuffer command) {if(!enabled())return;std::lock_guard<std::mutex> copies(impl->readback_mutex);impl->cancel_recorded(command);std::lock_guard<std::mutex> lock(impl->mutex);auto& state=impl->commands[command];state={};state.generation=++impl->command_generation;}
void GpuProbe::command_freed(uint32_t count,const VkCommandBuffer* commands) {if(!enabled())return;std::lock_guard<std::mutex> copies(impl->readback_mutex);for(uint32_t i=0;i<count;++i)impl->cancel_recorded(commands[i]);std::lock_guard<std::mutex> lock(impl->mutex);for(uint32_t i=0;i<count;++i){impl->commands.erase(commands[i]);impl->command_pools.erase(commands[i]);}}
void GpuProbe::pipeline_bound(VkCommandBuffer command,bool model,VkPipelineLayout layout) {
    if(!enabled())return;
    std::lock_guard<std::mutex> lock(impl->mutex);auto& state=impl->commands[command];state.model=model;state.expected_layout=layout;
    if(model&&state.bound_layout!=layout)state.bindings.clear();
}
void GpuProbe::compute_pipeline_bound(VkCommandBuffer command,bool known,VkPipelineLayout layout) {
    if(!player_anchor_capture_enabled())return;
    std::lock_guard<std::mutex> lock(impl->mutex);auto& command_state=impl->commands[command];if(!command_state.generation)command_state.generation=++impl->command_generation;
    auto& state=command_state.compute;state.known=known;state.expected_layout=layout;
    if(known&&state.bound_layout!=layout)state.has_binding=false;
}
void GpuProbe::compute_dispatched(VkCommandBuffer command,uint32_t x,uint32_t y,uint32_t z) {
    if(!player_anchor_capture_enabled()||!x||!y||!z)return;
    std::lock_guard<std::mutex> lock(impl->mutex);auto& state=impl->commands[command].compute;if(!state.known)return;
    ComputeEvent event;event.binding=state.binding;event.set_generation=state.set_generation;event.groups={x,y,z};
    event.valid=state.has_binding&&state.expected_layout&&state.bound_layout==state.expected_layout;
    impl->record_compute(state,std::move(event));
}
void GpuProbe::compute_dispatch_unsupported(VkCommandBuffer command) {
    if(!player_anchor_capture_enabled())return;
    std::lock_guard<std::mutex> lock(impl->mutex);auto& state=impl->commands[command].compute;if(!state.known)return;
    ComputeEvent event;event.kind=ComputeEvent::Kind::unsupported;impl->record_compute(state,std::move(event));
}
void GpuProbe::draw_indexed_indirect(VkCommandBuffer command,VkBuffer buffer,VkDeviceSize offset,uint32_t count,uint32_t stride,VkBuffer count_buffer,VkDeviceSize count_offset) {
    if(!enabled()||!count)return;
    std::lock_guard<std::mutex> lock(impl->mutex);auto& state=impl->commands[command];
    if(!state.model||!state.expected_layout||state.bound_layout!=state.expected_layout)return;
    for(const auto* role:{"camera","skin","instances","mesh-draw","culled-instances"})if(!state.bindings.count(role))return;
    state.draws.push_back({buffer,count_buffer,offset,count_offset,count,stride,state.bindings});
}
void GpuProbe::execute(VkCommandBuffer command,uint32_t count,const VkCommandBuffer* children) {
    if(!enabled())return;
    std::lock_guard<std::mutex> lock(impl->mutex);auto& state=impl->commands[command];auto& draws=state.draws;
    for(uint32_t i=0;i<count;++i){const auto child=impl->commands.find(children[i]);if(child!=impl->commands.end())draws.insert(draws.end(),child->second.draws.begin(),child->second.draws.end());
        if(impl->anchor_enabled){ComputeEvent event;event.kind=ComputeEvent::Kind::execute;event.child=children[i];event.child_generation=child==impl->commands.end()?0:child->second.generation;impl->record_compute(state.compute,std::move(event));}}
}
void GpuProbe::submitted(VkQueue queue,uint32_t count,const VkSubmitInfo* submits) {
    if(!enabled())return;
    std::lock_guard<std::mutex> lock(impl->mutex);
    for(uint32_t i=0;i<count;++i)for(uint32_t c=0;c<submits[i].commandBufferCount;++c){
        const auto command=impl->commands.find(submits[i].pCommandBuffers[c]);if(command==impl->commands.end())continue;
        impl->resolve_command(queue,submits[i].pCommandBuffers[c],impl->submitted_draws);
        if(impl->anchor_enabled)impl->submit_compute(submits[i].pCommandBuffers[c],queue);
    }
}
void GpuProbe::submitted2(VkQueue queue,uint32_t count,const VkSubmitInfo2* submits) {
    // Reuse identical submission resolution without holding the observation lock.
    for(uint32_t i=0;i<count;++i){std::vector<VkCommandBuffer> commands;commands.reserve(submits[i].commandBufferInfoCount);for(uint32_t c=0;c<submits[i].commandBufferInfoCount;++c)commands.push_back(submits[i].pCommandBufferInfos[c].commandBuffer);
        VkSubmitInfo legacy{};legacy.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;legacy.commandBufferCount=static_cast<uint32_t>(commands.size());legacy.pCommandBuffers=commands.data();submitted(queue,1,&legacy);}
}
void GpuProbe::finish_frame() {if(!enabled())return;std::lock_guard<std::mutex> copying(impl->readback_mutex);impl->async_frame=false;std::lock_guard<std::mutex> lock(impl->mutex);impl->submitted_draws.clear();impl->submitted_anchors.clear();impl->anchor_poisoned_queues.clear();}
void GpuProbe::queue_observed(VkQueue queue, uint32_t family) {
    if (!enabled()) return;
    std::lock_guard<std::mutex> lock(impl->mutex); impl->queues[queue] = family;
}

GpuSnapshot GpuProbe::readback(VkQueue queue, const std::map<std::string, std::size_t>& limits, bool already_idle,
                             uint32_t wait_count, const VkSemaphore* waits, bool* consumed_waits) noexcept {
    GpuSnapshot snapshot;
    (void)already_idle;
    if (!enabled()) return snapshot;
    try {
        std::lock_guard<std::mutex> copying(impl->readback_mutex);
        const auto started=std::chrono::steady_clock::now();
        const auto plan=impl->make_plan(queue,limits,impl->submitted_draws,false);
        const auto& reads=plan.transfer_reads.empty()?plan.reads:plan.transfer_reads;const auto family=plan.family;const auto total=plan.total;
        snapshot=plan.snapshot;
        if (reads.empty()) return snapshot;
#define PROC(name) const auto name = impl->proc<PFN_##name>(#name)
        PROC(vkCreateBuffer); PROC(vkDestroyBuffer); PROC(vkGetBufferMemoryRequirements);
        PROC(vkAllocateMemory); PROC(vkFreeMemory); PROC(vkBindBufferMemory); PROC(vkMapMemory); PROC(vkUnmapMemory);
        PROC(vkCreateCommandPool); PROC(vkDestroyCommandPool); PROC(vkAllocateCommandBuffers); PROC(vkBeginCommandBuffer);
        PROC(vkCmdPipelineBarrier); PROC(vkCmdCopyBuffer); PROC(vkEndCommandBuffer); PROC(vkQueueSubmit); PROC(vkQueueWaitIdle);
        PROC(vkCreateFence); PROC(vkDestroyFence); PROC(vkWaitForFences); PROC(vkGetFenceStatus);
#undef PROC
        struct Resources {
            VkDevice device; PFN_vkDestroyBuffer destroy_buffer; PFN_vkFreeMemory free_memory; PFN_vkDestroyCommandPool destroy_pool;
            PFN_vkDestroyFence destroy_fence;PFN_vkQueueWaitIdle wait_idle;PFN_vkGetFenceStatus fence_status;VkQueue queue;
            VkBuffer buffer{}; VkDeviceMemory memory{}; VkCommandPool pool{};
            VkFence fence{};bool submitted=false,completed=false;
            void release() {
                // Only an exceptional failed fence wait needs a queue-wide
                // drain before releasing a command buffer that may be pending.
                if(submitted&&!completed) {
                    auto result=wait_idle(queue);
                    // Allocation/unknown errors do not prove completion. Keep
                    // both staging and native source-buffer leases alive until
                    // this fence completes or the device is definitively lost.
                    while(result!=VK_SUCCESS&&result!=VK_ERROR_DEVICE_LOST) {
                        result=fence_status(device,fence);
                        if(result!=VK_SUCCESS&&result!=VK_ERROR_DEVICE_LOST)Sleep(1);
                    }
                }
                if(fence)destroy_fence(device,fence,nullptr);
                if(pool)destroy_pool(device,pool,nullptr);
                if(buffer)destroy_buffer(device,buffer,nullptr);
                if(memory)free_memory(device,memory,nullptr);
                fence={};pool={};buffer={};memory={};submitted=false;
            }
            ~Resources(){release();}
        } owned{impl->device,vkDestroyBuffer,vkFreeMemory,vkDestroyCommandPool,vkDestroyFence,vkQueueWaitIdle,vkGetFenceStatus,queue};
        VkBufferCreateInfo buffer_ci{};
        buffer_ci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO; buffer_ci.size = total; buffer_ci.usage = VK_BUFFER_USAGE_TRANSFER_DST_BIT;
        check(vkCreateBuffer(impl->device, &buffer_ci, nullptr, &owned.buffer), "capture staging buffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(impl->device, owned.buffer, &requirements);
        uint32_t memory_type = impl->memory_properties.memoryTypeCount;
        constexpr auto required = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
        for (uint32_t i = 0; i < impl->memory_properties.memoryTypeCount; ++i) {
            const auto flags=impl->memory_properties.memoryTypes[i].propertyFlags;
            if (!(requirements.memoryTypeBits & (1u << i)) || (flags & required) != required) continue;
            if (memory_type == impl->memory_properties.memoryTypeCount) memory_type=i;
            // CPU reads from uncached upload memory can dominate a frame.
            // Retain coherent visibility while preferring host-cached reads.
            if (flags & VK_MEMORY_PROPERTY_HOST_CACHED_BIT) { memory_type=i; break; }
        }
        if (memory_type >= impl->memory_properties.memoryTypeCount) throw std::runtime_error("no coherent staging memory");
        VkMemoryAllocateInfo allocation{};
        allocation.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO; allocation.allocationSize = requirements.size; allocation.memoryTypeIndex = memory_type;
        check(vkAllocateMemory(impl->device, &allocation, nullptr, &owned.memory), "capture staging allocation");
        check(vkBindBufferMemory(impl->device, owned.buffer, owned.memory, 0), "capture staging binding");
        VkCommandPoolCreateInfo pool_ci{};
        pool_ci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO; pool_ci.queueFamilyIndex = family; pool_ci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        check(vkCreateCommandPool(impl->device, &pool_ci, nullptr, &owned.pool), "capture command pool");
        VkCommandBufferAllocateInfo command_ai{};
        command_ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO; command_ai.commandPool = owned.pool;
        command_ai.commandBufferCount = 1; command_ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        VkCommandBuffer command{};
        check(vkAllocateCommandBuffers(impl->device, &command_ai, &command), "capture command allocation");
        if (impl->set_loader_data) check(impl->set_loader_data(impl->device, command), "capture loader data");
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO; begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(vkBeginCommandBuffer(command, &begin), "capture command begin");
        VkMemoryBarrier before{};
        before.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER; before.srcAccessMask = VK_ACCESS_MEMORY_WRITE_BIT; before.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 1, &before, 0, nullptr, 0, nullptr);
        for (const auto& read : reads) {
            const VkBufferCopy region{read.target.binding.offset, read.destination, read.size};
            vkCmdCopyBuffer(command, read.target.binding.buffer, owned.buffer, 1, &region);
        }
        VkMemoryBarrier after{};
        after.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER; after.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; after.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        vkCmdPipelineBarrier(command, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 1, &after, 0, nullptr, 0, nullptr);
        check(vkEndCommandBuffer(command), "capture command end");
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO; submit.commandBufferCount = 1; submit.pCommandBuffers = &command;
        const std::vector<VkPipelineStageFlags> wait_stages(wait_count, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        submit.waitSemaphoreCount = wait_count; submit.pWaitSemaphores = waits; submit.pWaitDstStageMask = wait_stages.data();
        VkFenceCreateInfo fence_info{};fence_info.sType=VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        check(vkCreateFence(impl->device,&fence_info,nullptr,&owned.fence),"capture fence");
        const auto prepared=std::chrono::steady_clock::now();
        check(vkQueueSubmit(queue, 1, &submit, owned.fence), "capture submission");
        const auto submitted_at=std::chrono::steady_clock::now();
        owned.submitted=true;
        if (consumed_waits) *consumed_waits = true;
        check(vkWaitForFences(impl->device,1,&owned.fence,VK_TRUE,UINT64_MAX),"capture completion");
        owned.completed=true;
        const auto completed_at=std::chrono::steady_clock::now();
        void* data{};
        check(vkMapMemory(impl->device, owned.memory, 0, total, 0, &data), "capture map");
        try {snapshot=impl->decode_plan(plan,data,false);}catch(...){vkUnmapMemory(impl->device,owned.memory);throw;}
        vkUnmapMemory(impl->device,owned.memory);
        const auto decoded_at=std::chrono::steady_clock::now();
        owned.release();
        const auto released_at=std::chrono::steady_clock::now();
        const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
        const std::array<double,5> elapsed{ms(started,prepared),ms(prepared,submitted_at),ms(submitted_at,completed_at),ms(completed_at,decoded_at),ms(decoded_at,released_at)};
        for(std::size_t i=0;i<elapsed.size();++i)impl->readback_timings[i]+=elapsed[i];
        if(++impl->readback_count%300==0){std::string detail="probe.timing-ms prepare,submit,completion,decode,cleanup=";for(auto value:impl->readback_timings)detail+=std::to_string(value/300)+",";log_event(detail);impl->readback_timings={};}
        return snapshot;
    } catch (const std::exception& error) {
        log_event("probe.readback.failed " + std::string(error.what()));
    } catch (...) { log_event("probe.readback.failed unknown"); }
    return {};
}

#include "gpu_probe_inline.inl"
#include "gpu_probe_async.inl"

void GpuProbe::capture(VkQueue queue, uint64_t frame, bool already_idle, uint32_t wait_count,
                       const VkSemaphore* waits, bool* consumed_waits) noexcept {
    if (!enabled()) return;
    try {
        uint64_t requested_frame=0;bool automatic=false,anchor_due=false;
        {
            std::lock_guard<std::mutex> lock(impl->mutex);
            const auto request = std::find_if(impl->requested_frames.begin(), impl->requested_frames.end(),
                [&](uint64_t due) { return due <= frame && !impl->completed_frames.count(due); });
            if (request != impl->requested_frames.end()){requested_frame=*request;automatic=true;}
        }
        if(impl->anchor_enabled){std::lock_guard<std::mutex> lock(impl->capture_mutex);
            try{impl->poll_anchor_request();}catch(const std::exception& error){impl->anchor_capture_error(error.what());}catch(...){impl->anchor_capture_error("unknown");}
            anchor_due=impl->anchor_remaining!=0;}
        if(!automatic&&!anchor_due)return;
        std::map<std::string, std::size_t> limits{
            {"camera", 1024}, {"instances", 4u*1024u*1024u}, {"skin", 4u*1024u*1024u},
            {"previous-skin", 4u*1024u*1024u}, {"mesh-draw", 4u*1024u*1024u}, {"culled-instances", 4u*1024u*1024u}
        };
        // Standalone smoke fixture explicitly requests a known host-uploaded
        // skin buffer. Normal capture and avatar paths require submitted draws.
        if(environment(L"ENSHROUDED_VRM_CAPTURE_SKIN_ONLY")==L"1")limits={{"skin",4u*1024u*1024u}};
        if(impl->anchor_enabled)limits["player_anchor"]=2048;
        const auto snapshot = readback(queue, limits, already_idle, wait_count, waits, consumed_waits);
        if(impl->anchor_enabled)capture_snapshot(snapshot,frame);
        if (snapshot.buffers.empty()||!automatic) return;
        // Optional diagnostic bytes/provenance are persisted only by the
        // fixed-marker three-snapshot writer above, never automatic captures.
        const auto total=save_capture(impl->directory,snapshot,frame,false,false);
        {std::lock_guard<std::mutex> lock(impl->mutex);impl->completed_frames.insert(requested_frame);}
        log_event("probe.captured frame=" + std::to_string(frame) +
            " buffers=" + std::to_string(snapshot.buffers.size()) + " bytes=" + std::to_string(total));
    } catch (const std::exception& error) { log_event("probe.capture.failed " + std::string(error.what())); }
    catch (...) { log_event("probe.capture.failed unknown"); }
}

bool GpuProbe::queue_family(VkQueue queue, uint32_t& family) const {
    std::lock_guard<std::mutex> lock(impl->mutex);
    const auto found = impl->queues.find(queue);
    if (found == impl->queues.end()) return false;
    family = found->second;
    return true;
}
}
