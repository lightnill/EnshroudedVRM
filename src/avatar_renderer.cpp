#define VK_NO_PROTOTYPES
#include "runtime_log.hpp"
#include "avatar_renderer.hpp"
#include "graphics_state.hpp"
#include "gpu_probe.hpp"
#include "enshrouded_bridge.hpp"
#include "retarget.hpp"
#include "spring_bones.hpp"
#include "spring_frame.hpp"
#include "native_instance_qualifiers.hpp"
#include "native_tool_slots.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <stdexcept>
#include <set>
#include <mutex>
#include <chrono>
#include <charconv>
#include <limits>
#include <type_traits>

namespace evrm {
namespace {
std::wstring env(const wchar_t* name) {
    const auto count=GetEnvironmentVariableW(name,nullptr,0);if(!count||count>32768)return{};
    std::wstring value(count,L'\0');const auto n=GetEnvironmentVariableW(name,value.data(),count);
    if(!n||n>=count)return{};
    value.resize(n);
    return value;
}
std::string narrow(const std::wstring& value) { return std::string(value.begin(),value.end()); }
void check(VkResult result,const char* operation) {if(result!=VK_SUCCESS)throw std::runtime_error(std::string(operation)+": "+std::to_string(result));}
vrm::Matrix product(const vrm::Matrix& a,const vrm::Matrix& b) {
    vrm::Matrix out{};for(int c=0;c<4;++c)for(int r=0;r<4;++r)for(int k=0;k<4;++k)out[c*4+r]+=a[k*4+r]*b[c*4+k];return out;
}
std::array<float,3> transform(const vrm::Matrix& m,const std::array<float,3>& v,float w) {
    return {m[0]*v[0]+m[4]*v[1]+m[8]*v[2]+m[12]*w,
            m[1]*v[0]+m[5]*v[1]+m[9]*v[2]+m[13]*w,
            m[2]*v[0]+m[6]*v[1]+m[10]*v[2]+m[14]*w};
}
std::vector<uint32_t> shader_file(const std::filesystem::path& path) {
    std::ifstream file(path,std::ios::binary|std::ios::ate);if(!file)throw std::runtime_error("Missing avatar shader");
    const auto size=file.tellg();if(size<20||size>1048576||size%4)throw std::runtime_error("Invalid avatar shader length");
    std::vector<uint32_t> words(static_cast<std::size_t>(size)/4);file.seekg(0);file.read(reinterpret_cast<char*>(words.data()),size);
    if(!file||words[0]!=0x07230203)throw std::runtime_error("Invalid avatar SPIR-V");
    return words;
}
struct GpuVertex {float position[3],normal[3],uv[2];};
struct FrameConstants {float rows[16];float screen[4];};
struct MaterialConstants {float color[4];float cutoff;uint32_t alpha,unlit,gamma;};
struct Buffer {VkBuffer handle{};VkDeviceMemory memory{};VkDeviceSize size{};void* mapped{};};
struct Image {VkImage handle{};VkDeviceMemory memory{};VkImageView view{};};
struct Draw {std::size_t node{},primitive{},first_vertex{},first_index{},index_count{};int material=-1;};
template<class T> void trace_number(std::string& line,T value) {
    char text[64];
    const auto result=[&] {
        if constexpr(std::is_floating_point_v<T>) {
            if(!std::isfinite(value))throw std::runtime_error("Nonfinite trace value");
            return std::to_chars(text,text+sizeof text,value,std::chars_format::general,std::numeric_limits<T>::max_digits10);
        } else return std::to_chars(text,text+sizeof text,value);
    }();
    if(result.ec!=std::errc{})throw std::runtime_error("Trace number overflow");
    line.append(text,result.ptr);
}
template<class T,std::size_t N> void trace_array(std::string& line,const std::array<T,N>& values) {
    line+='[';for(std::size_t i=0;i<N;++i){if(i)line+=',';trace_number(line,values[i]);}line+=']';
}
void trace_semantic(std::string& line,const std::string& name) {
    if(name.empty()||name.size()>64||!std::all_of(name.begin(),name.end(),[](unsigned char c){return(c>='a'&&c<='z')||(c>='A'&&c<='Z')||(c>='0'&&c<='9')||c=='_';}))
        throw std::runtime_error("Invalid trace semantic");
    line+='"';line+=name;line+="\":";
}
struct PlayerTrace {
    using Clock=std::chrono::steady_clock;
    Clock::time_point next_poll{};
    HANDLE output=INVALID_HANDLE_VALUE;
    unsigned samples=0;
    bool requested=false;
    void finish(const char* status) noexcept {
        if(!requested)return;
        const bool closed=output==INVALID_HANDLE_VALUE||CloseHandle(output)!=0;
        output=INVALID_HANDLE_VALUE;requested=false;
        try{log_event("avatar.trace.end frames="+std::to_string(samples)+" status="+(closed?status:"close-error"));}catch(...){}
    }
    void sample(uint64_t frame_number,const PlayerFrame& frame,const vrm::RetargetResult& pose,const vrm::Avatar& avatar) noexcept {
        try {
            const auto now=Clock::now();
            if(output==INVALID_HANDLE_VALUE) {
                if(now<next_poll)return;
                next_poll=now+std::chrono::seconds(1);
                const auto configured=env(L"ENSHROUDED_VRM_CAPTURE_DIR");if(configured.empty())return;
                const auto directory=std::filesystem::path(configured),marker=directory/L"trace-player.request";
                std::error_code error;
                if(!std::filesystem::is_regular_file(marker,error)||error)return;
                if(!std::filesystem::remove(marker,error)||error)return;
                requested=true;samples=0;
                const auto stem="trace-player-"+std::to_string(frame_number);
                std::string filename;
                for(unsigned suffix=0;suffix<16;++suffix) {
                    filename=stem+(suffix?"-"+std::to_string(suffix):"")+".jsonl";
                    output=CreateFileW((directory/filename).c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL|FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
                    if(output!=INVALID_HANDLE_VALUE)break;
                    if(GetLastError()!=ERROR_FILE_EXISTS&&GetLastError()!=ERROR_ALREADY_EXISTS)break;
                }
                log_event("avatar.trace.begin frame="+std::to_string(frame_number)+" file="+filename);
                if(output==INVALID_HANDLE_VALUE){finish("open-error");return;}
            }
            if(frame.source_pose.current.size()>128||avatar.humanoid.size()>128)throw std::runtime_error("Trace joint limit");
            std::string line;line.reserve(64*1024);
            line="{\"frame\":";trace_number(line,frame_number);
            line+=",\"sample\":";trace_number(line,samples+1);
            line+=",\"timeMs\":";trace_number(line,std::chrono::duration<double,std::milli>(now.time_since_epoch()).count());
            line+=",\"nativeClip\":";trace_array(line,frame.camera_world_to_clip);
            line+=",\"stableClip\":";trace_array(line,frame.camera_stable_world_to_clip);
            line+=",\"jitterUV\":";trace_array(line,frame.camera_jitter_uv);
            line+=",\"actorWorld\":";trace_array(line,frame.actor_world);
            line+=",\"sourcePose\":{";bool first=true;
            for(const auto& joint:frame.source_pose.current){if(!first)line+=',';first=false;trace_semantic(line,joint.first);trace_array(line,joint.second);}
            line+="},\"targetPose\":{";first=true;
            for(const auto& joint:avatar.humanoid){
                if(joint.second<0||static_cast<std::size_t>(joint.second)>=pose.node_world.size())throw std::runtime_error("Invalid trace target joint");
                if(!first)line+=',';
                first=false;trace_semantic(line,joint.first);trace_array(line,pose.node_world[joint.second]);
            }
            line+="}}\n";
            if(line.size()>512*1024)throw std::runtime_error("Trace line limit");
            DWORD written=0;
            if(!WriteFile(output,line.data(),static_cast<DWORD>(line.size()),&written,nullptr)||written!=line.size()){finish("write-error");return;}
            if(++samples==120)finish("complete");
        }catch(...){finish("diagnostic-error");}
    }
    ~PlayerTrace(){finish("shutdown");}
};
}
struct AvatarRenderer::Impl {
    VkDevice device{};PFN_vkGetDeviceProcAddr gdpa{};PFN_vkSetDeviceLoaderData set_loader{};
    VkPhysicalDeviceMemoryProperties memory{};
    std::vector<VkQueueFamilyProperties> queue_properties;
    std::mutex resource_mutex;
    vrm::Avatar avatar;
    std::unique_ptr<vrm::SpringBones> springs;
    std::unique_ptr<SecondaryHandContact> secondary_contact;
    SecondaryContactActivation secondary_activation;
    struct ViewMotion {
        std::unique_ptr<vrm::SpringBones> springs;
        std::unique_ptr<vrm::Retargeter> retargeter;
        SecondaryContactActivation contact;
        std::array<std::uint32_t,3> origin{};
        bool origin_known=false;
    };
    std::map<std::uint64_t,ViewMotion> view_motion;
    ViewMotion& motion(std::uint64_t source){
        if(!view_motion.count(source)&&view_motion.size()>=4)view_motion.clear();
        return view_motion[source];
    }
    vrm::RetargetResult retarget_pose(const vrm::SourcePose& source,const vrm::RetargetOptions& options,std::uint64_t view=0){
        const auto found=view_motion.find(view);
        if(found!=view_motion.end()&&found->second.retargeter)return found->second.retargeter->update(source,options);
        // Rejected sources must not evict accepted spring/contact histories.
        auto prepared=std::make_unique<vrm::Retargeter>(avatar);
        auto result=prepared->update(source,options);
        if(result)motion(view).retargeter=std::move(prepared);
        return result;
    }
    double contact_ms=0,contact_residual=0;
    unsigned contact_samples=0,contact_two_hand=0,contact_bow=0,contact_clamped=0;
    std::array<std::uint32_t,3> spring_origin{};
    bool spring_origin_known=false;
    double spring_ms=0,spring_max_offset=0;
    unsigned spring_samples=0,spring_steps=0,spring_resets=0;
    const char* spring_error=nullptr;
    std::unique_ptr<EnshroudedBridge> bridge;
    std::filesystem::path shader_directory;
    bool active=false,initialized=false;
    bool native_mode=false,native_replacement=false,gpu_skinning=true;
    std::shared_ptr<const vrm::Avatar> native_asset;
    std::shared_ptr<const NativeSkinAsset> native_skin_asset;
    std::shared_ptr<const NativeAvatarPublication> publication;
    float debug_offset=0.75f;
    std::string last_error;
    uint64_t frame_count=0,capture_sequence=0;
    PlayerTrace player_trace;
    std::array<double,5> timings{};
    unsigned native_timing_count=0;
    VkQueue queue{};uint32_t family=0;
    VkCommandPool command_pool{};VkCommandBuffer command{};
    Buffer vertices,indices,uniform;
    std::vector<Draw> draws;std::size_t vertex_count=0;
    std::vector<Image> textures;Image white,self_depth;
    VkImageView scene_view{};VkImage scene_image{};
    uint64_t scene_generation=0,current_swapchain_generation=0;
    VkSampler depth_sampler{};std::vector<VkSampler> samplers;
    VkDescriptorSetLayout descriptor_layout{};VkDescriptorPool descriptor_pool{};
    std::vector<VkDescriptorSet> descriptor_sets;
    VkPipelineLayout pipeline_layout{};VkPipeline opaque{},blend{};
    VkShaderModule vertex_shader{},fragment_shader{};
    VkRenderPass renderpass{};std::vector<VkImageView> swapchain_views;std::vector<VkFramebuffer> framebuffers;
    VkSwapchainKHR current_swapchain{};VkFormat current_format{};VkExtent2D extent{};
    template<class F> F function(const char* name) const {return reinterpret_cast<F>(gdpa(device,name));}
#define V(name) function<PFN_##name>(#name)
    void report(const std::string& error) {if(error!=last_error){log_event("avatar.unavailable "+error);last_error=error;}}
    void apply_secondary_contact(vrm::RetargetResult&pose,const vrm::SourcePose&source,NativeAvatarPublication&candidate) noexcept {
        if(!native_replacement||!secondary_contact||!secondary_contact->ready())return;
        const auto started=std::chrono::steady_clock::now();
        try {
            auto& secondary_activation=motion(candidate.source_instances).contact;
            std::uint64_t owner=0,observed=0;
            const auto now=GetTickCount64();
            const auto raw=read_native_hand_contact_mode(candidate,now,owner,&observed);
            const auto mode=secondary_activation.observe(raw,candidate.published_milliseconds,observed,owner,candidate.source_buffer_epoch);
            if(mode!=SecondaryHandContactMode::None) {
                auto revised=pose;
                const auto result=secondary_contact->update(source,candidate.tool_hands,candidate.height_scale,candidate.basis_xz,mode,revised);
                if(result.applied) {
                    const auto hands=make_tool_hand_corrections(avatar,source,revised,candidate.height_scale,candidate.basis_xz);
                    std::uint64_t owner_after=0;
                    if(hands[0].valid&&hands[1].valid&&read_native_hand_contact_mode(candidate,GetTickCount64(),owner_after)==mode&&owner_after==owner) {
                        auto baseline=std::make_shared<NativeSecondaryContactBaseline>();
                        baseline->pose=std::move(pose);baseline->hands=candidate.tool_hands;
                        pose=std::move(revised);candidate.tool_hands=hands;
                        candidate.secondary_baseline=std::move(baseline);candidate.secondary_mode=mode;candidate.secondary_owner=owner;
                        if(mode==SecondaryHandContactMode::TwoHanded)++contact_two_hand;else ++contact_bow;
                        if(result.clamped)++contact_clamped;
                        contact_residual=std::max(contact_residual,result.residual);
                    } else secondary_activation={};
                }
            }
        }catch(...){view_motion.clear();} // Optional contact cannot suppress a valid avatar.
        contact_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-started).count();
        if(++contact_samples==300) {
            log_event_lazy([&]{return "avatar.secondary-contact samples=300 two-hand="+std::to_string(contact_two_hand)+" bow="+std::to_string(contact_bow)+" clamped="+std::to_string(contact_clamped)+" max-residual="+std::to_string(contact_residual)+" ms-per-pose="+std::to_string(contact_ms/contact_samples);});
            contact_samples=contact_two_hand=contact_bow=contact_clamped=0;contact_ms=contact_residual=0;
        }
    }
    void apply_springs(vrm::RetargetResult& pose,const PlayerFrame& frame,std::uint64_t captured_ms,std::uint64_t epoch,float height_scale,std::uint64_t source=0) {
        if(!springs||!springs->joint_count())return;
        auto& state=motion(source);
        if(!state.springs)state.springs=vrm::make_optional_springs(avatar,vrm::SpringMotion::Inertial);
        if(!state.springs||!state.springs->ready())return;
        auto& springs=state.springs;
        auto& spring_origin=state.origin;auto& spring_origin_known=state.origin_known;
        vrm::SpringFrame input;input.timestamp_ms=captured_ms;input.source_epoch=epoch;
        input.actor_id=frame.parts.front().instance_id;
        const vrm::Matrix basis=avatar.version==vrm::VrmVersion::V1?vrm::Matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}:vrm::Matrix{-1,0,0,0,0,1,0,0,0,0,-1,0,0,0,0,1};
        input.model_to_world=product(frame.actor_world,basis);
        for(int c=0;c<3;++c)for(int r=0;r<3;++r)input.model_to_world[c*4+r]*=height_scale;
        if(spring_origin_known)input.origin_delta=spring_origin_delta(frame.camera_packed_origin,spring_origin);
        spring_origin=frame.camera_packed_origin;spring_origin_known=true;
        const auto begin=std::chrono::steady_clock::now();
        const auto update=springs->update(pose,input);
        spring_ms+=std::chrono::duration<double,std::milli>(std::chrono::steady_clock::now()-begin).count();
        spring_steps+=update.steps;spring_resets+=update.reset?1u:0u;spring_max_offset=std::max(spring_max_offset,update.max_offset);
        if(update.error){if(spring_error!=update.error){spring_error=update.error;log_event_lazy([&]{return std::string("avatar.spring-unavailable ")+spring_error;});}}
        else spring_error=nullptr;
        if(++spring_samples==300) {
            log_event_lazy([&]{return "avatar.spring-timing-ms mean="+std::to_string(spring_ms/spring_samples)+" joints="+std::to_string(springs->joint_count())+" steps="+std::to_string(spring_steps)+" resets="+std::to_string(spring_resets)+" max-offset="+std::to_string(spring_max_offset);});
            spring_samples=spring_steps=spring_resets=0;spring_ms=spring_max_offset=0;
        }
    }
    std::shared_ptr<NativeAvatarPublication> build_native_view(const GpuSnapshot& snapshot,std::uint64_t serial,std::uint64_t presentation) {
        const auto started=std::chrono::steady_clock::now();
        const auto frame=bridge->read_player_frame(snapshot);if(!frame)throw std::runtime_error(frame.error);
        const auto identified=std::chrono::steady_clock::now();
        vrm::RetargetOptions options;options.align_rest_directions=true;
        if(avatar.version==vrm::VrmVersion::V0)options.source_to_avatar_basis={-1,0,0,0,0,1,0,0,0,0,-1,0,0,0,0,1};
        options.source_rest_height=frame.source_pose.rest.at("head")[13];options.avatar_rest_height=avatar.nodes[avatar.humanoid.at("head")].world[13];
        const auto source=snapshot.buffers.at("instances").binding.buffer;
        auto pose=retarget_pose(frame.source_pose,options,reinterpret_cast<std::uintptr_t>(source));if(!pose)throw std::runtime_error("Retarget: "+pose.error);
        apply_springs(pose,frame,snapshot.captured_milliseconds,snapshot.source_epoch,options.source_rest_height/options.avatar_rest_height,reinterpret_cast<std::uintptr_t>(source));
        const auto posed=std::chrono::steady_clock::now();
        auto publication=std::make_shared<NativeAvatarPublication>();publication->asset=native_asset;
        publication->serial=serial;publication->presentation_serial=presentation;publication->published_milliseconds=snapshot.captured_milliseconds;
        publication->source_buffer_epoch=snapshot.source_epoch;
        publication->height_scale=options.source_rest_height/options.avatar_rest_height;
        publication->basis_xz=avatar.version==vrm::VrmVersion::V0?-1.f:1.f;publication->diagnostic_x=debug_offset;
        publication->tool_hands=make_tool_hand_corrections(avatar,frame.source_pose,pose,publication->height_scale,publication->basis_xz);
        const auto instances=snapshot.buffers.find("instances");if(instances==snapshot.buffers.end())throw std::runtime_error("Native instance metadata unavailable");
        for(const auto& part:frame.parts) {
            const auto offset=std::size_t(part.instance_id)*128+112;
            if(offset>instances->second.bytes.size()||4>instances->second.bytes.size()-offset)throw std::runtime_error("Native instance tag unavailable");
            std::uint32_t tag=0;std::memcpy(&tag,instances->second.bytes.data()+offset,4);
            publication->parts.push_back({part.instance_id,part.model_index,part.skin_matrix_offset,tag});
        }
        if(native_replacement) {
            std::string reason;
            if(!collect_native_instance_qualifiers(instances->second.bytes.data(),instances->second.bytes.size(),
                publication->parts,publication->instance_qualifiers,reason)) throw std::runtime_error("Native qualifiers: "+reason);
            publication->source_instances=reinterpret_cast<std::uintptr_t>(instances->second.binding.buffer);
            publication->source_instance_offset=instances->second.binding.offset;publication->source_instance_range=instances->second.binding.range;
        }
        apply_secondary_contact(pose,frame.source_pose,*publication);
        if(gpu_skinning){
            if(!native_skin_asset){
                auto asset=std::make_shared<NativeSkinAsset>(make_native_skin_asset(avatar,pose));
                if(!*asset)throw std::runtime_error("Native skin asset: "+asset->error);
                native_skin_asset=std::move(asset);
            }
            auto skinning=std::make_shared<NativeSkinPose>(make_native_skin_pose(native_skin_asset,pose));
            if(!*skinning)throw std::runtime_error("Native skin pose: "+skinning->error);
            publication->geometry=native_skin_asset->layout;publication->skinning=std::move(skinning);
        }else{
            auto geometry=std::make_shared<NativeAvatarPose>(make_native_avatar_pose(avatar,pose));
            if(!*geometry)throw std::runtime_error("Native geometry: "+geometry->error);
            publication->geometry=std::move(geometry);
        }
        player_trace.sample(serial,frame,pose,avatar);
        const auto finished=std::chrono::steady_clock::now();
        const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
        timings[2]+=ms(started,identified);timings[3]+=ms(identified,posed);timings[4]+=ms(posed,finished);
        return publication;
    }
    uint32_t memory_type(uint32_t bits,VkMemoryPropertyFlags flags) {
        for(uint32_t i=0;i<memory.memoryTypeCount;++i)if((bits&(1u<<i))&&(memory.memoryTypes[i].propertyFlags&flags)==flags)return i;
        throw std::runtime_error("No compatible avatar memory type");
    }
    void destroy_buffer(Buffer& b) {
        if(b.mapped)V(vkUnmapMemory)(device,b.memory);
        if(b.handle)V(vkDestroyBuffer)(device,b.handle,nullptr);
        if(b.memory)V(vkFreeMemory)(device,b.memory,nullptr);
        b={};
    }
    void create_buffer(Buffer& b,VkDeviceSize size,VkBufferUsageFlags usage) {
        VkBufferCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;info.size=size;info.usage=usage;
        check(V(vkCreateBuffer)(device,&info,nullptr,&b.handle),"avatar buffer");
        VkMemoryRequirements req{};V(vkGetBufferMemoryRequirements)(device,b.handle,&req);
        VkMemoryAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=req.size;
        allocation.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(V(vkAllocateMemory)(device,&allocation,nullptr,&b.memory),"avatar buffer memory");
        check(V(vkBindBufferMemory)(device,b.handle,b.memory,0),"avatar buffer bind");
        check(V(vkMapMemory)(device,b.memory,0,size,0,&b.mapped),"avatar buffer map");b.size=size;
    }
    void destroy_image(Image& image) {
        if(image.view)V(vkDestroyImageView)(device,image.view,nullptr);
        if(image.handle)V(vkDestroyImage)(device,image.handle,nullptr);
        if(image.memory)V(vkFreeMemory)(device,image.memory,nullptr);
        image={};
    }
    void create_image(Image& image,uint32_t width,uint32_t height,VkFormat format,VkImageUsageFlags usage,VkImageAspectFlags aspect) {
        VkImageCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;info.imageType=VK_IMAGE_TYPE_2D;info.format=format;
        info.extent={width,height,1};info.mipLevels=1;info.arrayLayers=1;info.samples=VK_SAMPLE_COUNT_1_BIT;info.tiling=VK_IMAGE_TILING_OPTIMAL;info.usage=usage;
        check(V(vkCreateImage)(device,&info,nullptr,&image.handle),"avatar image");
        VkMemoryRequirements req{};V(vkGetImageMemoryRequirements)(device,image.handle,&req);
        VkMemoryAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;allocation.allocationSize=req.size;
        allocation.memoryTypeIndex=memory_type(req.memoryTypeBits,VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(V(vkAllocateMemory)(device,&allocation,nullptr,&image.memory),"avatar image memory");
        check(V(vkBindImageMemory)(device,image.handle,image.memory,0),"avatar image bind");
        image.view=create_view(image.handle,format,aspect);
    }
    VkImageView create_view(VkImage image,VkFormat format,VkImageAspectFlags aspect) {
        VkImageViewCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;info.image=image;info.viewType=VK_IMAGE_VIEW_TYPE_2D;info.format=format;
        info.subresourceRange={aspect,0,1,0,1};VkImageView view{};check(V(vkCreateImageView)(device,&info,nullptr,&view),"avatar image view");return view;
    }
    void begin_command() {
        check(V(vkResetCommandPool)(device,command_pool,0),"avatar command reset");
        VkCommandBufferBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;begin.flags=VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        check(V(vkBeginCommandBuffer)(command,&begin),"avatar command begin");
    }
    void submit_wait(uint32_t wait_count=0,const VkSemaphore* waits=nullptr,bool* consumed=nullptr) {
        check(V(vkEndCommandBuffer)(command),"avatar command end");
        std::vector<VkPipelineStageFlags> stages(wait_count,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
        VkSubmitInfo submit{};submit.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;submit.commandBufferCount=1;submit.pCommandBuffers=&command;
        submit.waitSemaphoreCount=wait_count;submit.pWaitSemaphores=waits;submit.pWaitDstStageMask=stages.data();
        check(V(vkQueueSubmit)(queue,1,&submit,VK_NULL_HANDLE),"avatar queue submit");
        if(consumed)*consumed=true;
        check(V(vkQueueWaitIdle)(queue),"avatar queue wait");
    }
    void image_barrier(VkImage image,VkImageLayout old_layout,VkImageLayout new_layout,VkAccessFlags src,VkAccessFlags dst,VkImageAspectFlags aspect) {
        VkImageMemoryBarrier b{};b.sType=VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;b.srcAccessMask=src;b.dstAccessMask=dst;
        b.oldLayout=old_layout;b.newLayout=new_layout;b.srcQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;b.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;
        b.image=image;b.subresourceRange={aspect,0,1,0,1};
        V(vkCmdPipelineBarrier)(command,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,0,0,nullptr,0,nullptr,1,&b);
    }
    void upload_texture(Image& destination,const vrm::Image& source) {
        create_image(destination,source.width,source.height,VK_FORMAT_R8G8B8A8_SRGB,VK_IMAGE_USAGE_TRANSFER_DST_BIT|VK_IMAGE_USAGE_SAMPLED_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        Buffer staging;
        try {
            create_buffer(staging,source.rgba.size(),VK_BUFFER_USAGE_TRANSFER_SRC_BIT);std::memcpy(staging.mapped,source.rgba.data(),source.rgba.size());
            begin_command();image_barrier(destination.handle,VK_IMAGE_LAYOUT_UNDEFINED,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,0,VK_ACCESS_TRANSFER_WRITE_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
            VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={static_cast<uint32_t>(source.width),static_cast<uint32_t>(source.height),1};
            V(vkCmdCopyBufferToImage)(command,staging.handle,destination.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&copy);
            image_barrier(destination.handle,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_TRANSFER_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
            submit_wait();destroy_buffer(staging);
        }catch(...){V(vkQueueWaitIdle)(queue);destroy_buffer(staging);throw;}
    }
    VkSampler make_sampler(const vrm::Texture* texture, bool depth = false) {
        auto wrap=[](int value){return value==33071?VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE:value==33648?VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT:VK_SAMPLER_ADDRESS_MODE_REPEAT;};
        VkSamplerCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
        info.magFilter=texture&&texture->mag_filter==9728?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
        info.minFilter=texture&&(texture->min_filter==9728||texture->min_filter==9984||texture->min_filter==9986)?VK_FILTER_NEAREST:VK_FILTER_LINEAR;
        if(depth)info.minFilter=info.magFilter=VK_FILTER_NEAREST;
        info.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;info.addressModeU=texture?wrap(texture->wrap_s):VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        info.addressModeV=texture?wrap(texture->wrap_t):VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;info.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        VkSampler sampler{};check(V(vkCreateSampler)(device,&info,nullptr,&sampler),"avatar sampler");return sampler;
    }
    void create_geometry() {
        std::vector<uint32_t> all_indices;
        for(std::size_t n=0;n<avatar.nodes.size();++n)if(avatar.nodes[n].active&&avatar.nodes[n].mesh>=0) {
            const auto& mesh=avatar.meshes[avatar.nodes[n].mesh];
            for(std::size_t p=0;p<mesh.primitives.size();++p) {
                const auto& primitive=mesh.primitives[p];draws.push_back({n,p,vertex_count,all_indices.size(),primitive.indices.size(),primitive.material});
                vertex_count+=primitive.vertices.size();all_indices.insert(all_indices.end(),primitive.indices.begin(),primitive.indices.end());
            }
        }
        if(!vertex_count||all_indices.empty())throw std::runtime_error("No active avatar geometry");
        create_buffer(vertices,vertex_count*sizeof(GpuVertex),VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);
        create_buffer(indices,all_indices.size()*4,VK_BUFFER_USAGE_INDEX_BUFFER_BIT);std::memcpy(indices.mapped,all_indices.data(),all_indices.size()*4);
        create_buffer(uniform,sizeof(FrameConstants),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
    }
    void create_descriptors() {
        const std::array<VkDescriptorSetLayoutBinding,5> bindings{{
            {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {2,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {3,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr},
            {4,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_FRAGMENT_BIT,nullptr}}};
        VkDescriptorSetLayoutCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;info.bindingCount=bindings.size();info.pBindings=bindings.data();
        check(V(vkCreateDescriptorSetLayout)(device,&info,nullptr,&descriptor_layout),"avatar descriptor layout");
        const uint32_t count=static_cast<uint32_t>(avatar.materials.size()+1);
        const std::array<VkDescriptorPoolSize,3> sizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,count},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,count*2},{VK_DESCRIPTOR_TYPE_SAMPLER,count*2}}};
        VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.maxSets=count;pool.poolSizeCount=sizes.size();pool.pPoolSizes=sizes.data();
        check(V(vkCreateDescriptorPool)(device,&pool,nullptr,&descriptor_pool),"avatar descriptor pool");
        std::vector<VkDescriptorSetLayout> layouts(count,descriptor_layout);descriptor_sets.resize(count);
        VkDescriptorSetAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocation.descriptorPool=descriptor_pool;allocation.descriptorSetCount=count;allocation.pSetLayouts=layouts.data();
        check(V(vkAllocateDescriptorSets)(device,&allocation,descriptor_sets.data()),"avatar descriptor sets");
        samplers.resize(count);depth_sampler=make_sampler(nullptr,true);
        for(uint32_t i=0;i<count;++i) {
            const auto texture=i<avatar.materials.size()?avatar.materials[i].base_texture:-1;
            const auto* t=texture>=0?&avatar.textures[texture]:nullptr;samplers[i]=make_sampler(t);
            const auto image=t?textures[t->image].view:white.view;
            const VkDescriptorBufferInfo buffer{uniform.handle,0,sizeof(FrameConstants)};
            const std::array<VkDescriptorImageInfo,4> images{{{VK_NULL_HANDLE,image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{samplers[i],VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED},
                {VK_NULL_HANDLE,scene_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},{depth_sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED}}};
            std::array<VkWriteDescriptorSet,5> writes{};
            for(uint32_t b=0;b<5;++b){auto& w=writes[b];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=descriptor_sets[i];w.dstBinding=b;w.descriptorCount=1;w.descriptorType=bindings[b].descriptorType;if(b)w.pImageInfo=&images[b-1];else w.pBufferInfo=&buffer;}
            V(vkUpdateDescriptorSets)(device,writes.size(),writes.data(),0,nullptr);
        }
        const VkPushConstantRange range{VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof(MaterialConstants)};
        VkPipelineLayoutCreateInfo layout{};layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=1;layout.pSetLayouts=&descriptor_layout;layout.pushConstantRangeCount=1;layout.pPushConstantRanges=&range;
        check(V(vkCreatePipelineLayout)(device,&layout,nullptr,&pipeline_layout),"avatar pipeline layout");
    }
    void replace_scene_depth(const SceneDepth& depth) {
        if(scene_image==depth.image&&scene_generation==depth.generation)return;
        const auto replacement=create_view(depth.image,depth.format,VK_IMAGE_ASPECT_DEPTH_BIT);
        const auto old_view=scene_view;
        scene_view=replacement;scene_image=depth.image;scene_generation=depth.generation;
        for(auto set:descriptor_sets){const VkDescriptorImageInfo image{VK_NULL_HANDLE,scene_view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};VkWriteDescriptorSet write{};write.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dstSet=set;write.dstBinding=3;write.descriptorCount=1;write.descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;write.pImageInfo=&image;V(vkUpdateDescriptorSets)(device,1,&write,0,nullptr);}
        if(old_view)V(vkDestroyImageView)(device,old_view,nullptr);
    }
    VkPipeline make_pipeline(bool transparent) {
        const std::array<VkPipelineShaderStageCreateInfo,2> stages{{{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_VERTEX_BIT,vertex_shader,"vs_main",nullptr},
            {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,nullptr,0,VK_SHADER_STAGE_FRAGMENT_BIT,fragment_shader,"ps_main",nullptr}}};
        const VkVertexInputBindingDescription binding{0,sizeof(GpuVertex),VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription,3> attrs{{{0,0,VK_FORMAT_R32G32B32_SFLOAT,0},{1,0,VK_FORMAT_R32G32B32_SFLOAT,12},{2,0,VK_FORMAT_R32G32_SFLOAT,24}}};
        VkPipelineVertexInputStateCreateInfo input{};input.sType=VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;input.vertexBindingDescriptionCount=1;input.pVertexBindingDescriptions=&binding;input.vertexAttributeDescriptionCount=attrs.size();input.pVertexAttributeDescriptions=attrs.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{};assembly.sType=VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;assembly.topology=VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};viewport.sType=VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;viewport.viewportCount=1;viewport.scissorCount=1;
        VkPipelineRasterizationStateCreateInfo raster{};raster.sType=VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;raster.polygonMode=VK_POLYGON_MODE_FILL;raster.cullMode=VK_CULL_MODE_NONE;raster.frontFace=VK_FRONT_FACE_COUNTER_CLOCKWISE;raster.lineWidth=1;
        VkPipelineMultisampleStateCreateInfo multisample{};multisample.sType=VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;multisample.rasterizationSamples=VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{};depth.sType=VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;depth.depthTestEnable=VK_TRUE;depth.depthWriteEnable=transparent?VK_FALSE:VK_TRUE;depth.depthCompareOp=VK_COMPARE_OP_GREATER_OR_EQUAL;
        VkPipelineColorBlendAttachmentState attachment{};attachment.colorWriteMask=15;attachment.blendEnable=transparent?VK_TRUE:VK_FALSE;attachment.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;attachment.colorBlendOp=VK_BLEND_OP_ADD;attachment.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;attachment.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;attachment.alphaBlendOp=VK_BLEND_OP_ADD;
        VkPipelineColorBlendStateCreateInfo color{};color.sType=VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;color.attachmentCount=1;color.pAttachments=&attachment;
        const std::array<VkDynamicState,2> states{VK_DYNAMIC_STATE_VIEWPORT,VK_DYNAMIC_STATE_SCISSOR};VkPipelineDynamicStateCreateInfo dynamic{};dynamic.sType=VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;dynamic.dynamicStateCount=states.size();dynamic.pDynamicStates=states.data();
        VkGraphicsPipelineCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;info.stageCount=stages.size();info.pStages=stages.data();info.pVertexInputState=&input;info.pInputAssemblyState=&assembly;info.pViewportState=&viewport;info.pRasterizationState=&raster;info.pMultisampleState=&multisample;info.pDepthStencilState=&depth;info.pColorBlendState=&color;info.pDynamicState=&dynamic;info.layout=pipeline_layout;info.renderPass=renderpass;
        VkPipeline pipeline{};check(V(vkCreateGraphicsPipelines)(device,VK_NULL_HANDLE,1,&info,nullptr,&pipeline),"avatar graphics pipeline");return pipeline;
    }
    void destroy_target() {
        for(auto framebuffer:framebuffers)V(vkDestroyFramebuffer)(device,framebuffer,nullptr);
        framebuffers.clear();
        for(auto view:swapchain_views)V(vkDestroyImageView)(device,view,nullptr);
        swapchain_views.clear();
        if(opaque)V(vkDestroyPipeline)(device,opaque,nullptr);
        opaque={};
        if(blend)V(vkDestroyPipeline)(device,blend,nullptr);
        blend={};
        if(renderpass)V(vkDestroyRenderPass)(device,renderpass,nullptr);
        renderpass={};
        destroy_image(self_depth);
        current_swapchain={};
        current_swapchain_generation=0;
    }
    void create_target(const SwapchainState& swapchain) {
        destroy_target();extent=swapchain.extent;current_format=swapchain.format;
        create_image(self_depth,extent.width,extent.height,VK_FORMAT_D32_SFLOAT,VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        std::array<VkAttachmentDescription,2> attachments{};attachments[0].format=swapchain.format;attachments[0].samples=VK_SAMPLE_COUNT_1_BIT;attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachments[0].storeOp=VK_ATTACHMENT_STORE_OP_STORE;attachments[0].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachments[0].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;attachments[0].initialLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;attachments[0].finalLayout=VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
        attachments[1].format=VK_FORMAT_D32_SFLOAT;attachments[1].samples=VK_SAMPLE_COUNT_1_BIT;attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_CLEAR;attachments[1].storeOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;attachments[1].stencilLoadOp=VK_ATTACHMENT_LOAD_OP_DONT_CARE;attachments[1].stencilStoreOp=VK_ATTACHMENT_STORE_OP_DONT_CARE;attachments[1].initialLayout=VK_IMAGE_LAYOUT_UNDEFINED;attachments[1].finalLayout=VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
        const VkAttachmentReference color{0,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL},depth{1,VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
        VkSubpassDescription subpass{};subpass.pipelineBindPoint=VK_PIPELINE_BIND_POINT_GRAPHICS;subpass.colorAttachmentCount=1;subpass.pColorAttachments=&color;subpass.pDepthStencilAttachment=&depth;
        VkRenderPassCreateInfo pass{};pass.sType=VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;pass.attachmentCount=attachments.size();pass.pAttachments=attachments.data();pass.subpassCount=1;pass.pSubpasses=&subpass;
        check(V(vkCreateRenderPass)(device,&pass,nullptr,&renderpass),"avatar render pass");
        for(auto image:swapchain.images) {
            const auto view=create_view(image,swapchain.format,VK_IMAGE_ASPECT_COLOR_BIT);swapchain_views.push_back(view);
            const std::array<VkImageView,2> views{view,self_depth.view};VkFramebufferCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;info.renderPass=renderpass;info.attachmentCount=views.size();info.pAttachments=views.data();info.width=extent.width;info.height=extent.height;info.layers=1;
            VkFramebuffer framebuffer{};check(V(vkCreateFramebuffer)(device,&info,nullptr,&framebuffer),"avatar framebuffer");framebuffers.push_back(framebuffer);
        }
        opaque=make_pipeline(false);blend=make_pipeline(true);current_swapchain=swapchain.swapchain;current_swapchain_generation=swapchain.generation;
    }
    void initialize(VkQueue selected_queue,uint32_t selected_family,const SceneDepth& depth,const SwapchainState& swapchain) {
        queue=selected_queue;family=selected_family;
        VkCommandPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pool.queueFamilyIndex=family;pool.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        check(V(vkCreateCommandPool)(device,&pool,nullptr,&command_pool),"avatar command pool");
        VkCommandBufferAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;allocation.commandPool=command_pool;allocation.commandBufferCount=1;allocation.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        check(V(vkAllocateCommandBuffers)(device,&allocation,&command),"avatar command buffer");
        if(set_loader)check(set_loader(device,command),"avatar loader data");
        create_geometry();textures.resize(avatar.images.size());
        std::set<int> used_images;for(const auto& material:avatar.materials)if(material.base_texture>=0)used_images.insert(avatar.textures[material.base_texture].image);
        for(int image:used_images)upload_texture(textures[image],avatar.images[image]);
        const vrm::Image white_pixels{1,1,{255,255,255,255}};upload_texture(white,white_pixels);
        scene_view=create_view(depth.image,depth.format,VK_IMAGE_ASPECT_DEPTH_BIT);scene_image=depth.image;scene_generation=depth.generation;create_descriptors();
        auto make_shader=[&](const char* name,VkShaderModule& shader){const auto code=shader_file(shader_directory/name);VkShaderModuleCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;info.codeSize=code.size()*4;info.pCode=code.data();check(V(vkCreateShaderModule)(device,&info,nullptr,&shader),"avatar shader module");};
        make_shader("avatar.vert.spv",vertex_shader);make_shader("avatar.frag.spv",fragment_shader);
        create_target(swapchain);initialized=true;log_event("avatar.gpu-ready vertices="+std::to_string(vertex_count)+" draws="+std::to_string(draws.size()));
    }
    void skin_vertices(const vrm::RetargetResult& pose,const vrm::Matrix& actor) {
        auto* output=static_cast<GpuVertex*>(vertices.mapped);
        for(const auto& draw:draws) {
            const auto& node=avatar.nodes[draw.node];const auto& primitive=avatar.meshes[node.mesh].primitives[draw.primitive];
            const vrm::SkinPalette* palette=nullptr;if(primitive.skinned)for(const auto& candidate:pose.palettes)if(candidate.mesh_node==static_cast<int>(draw.node)){palette=&candidate;break;}
            if(primitive.skinned&&!palette)throw std::runtime_error("Missing avatar skin palette");
            for(std::size_t i=0;i<primitive.vertices.size();++i) {
                const auto& vertex=primitive.vertices[i];std::array<float,3> p{},normal{};
                if(palette)for(int influence=0;influence<4;++influence) {
                    const auto& matrix=palette->matrices.at(vertex.joints[influence]);const auto sp=transform(matrix,vertex.position,1),sn=transform(matrix,vertex.normal,0);
                    for(int c=0;c<3;++c){p[c]+=sp[c]*vertex.weights[influence];normal[c]+=sn[c]*vertex.weights[influence];}
                }else{p=vertex.position;normal=vertex.normal;}
                p=transform(pose.node_world[draw.node],p,1);normal=transform(actor,transform(pose.node_world[draw.node],normal,0),0);
                float length=std::sqrt(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]);if(!std::isfinite(length)||length<1e-10f)throw std::runtime_error("Invalid avatar skinned normal");
                auto& v=output[draw.first_vertex+i];for(int c=0;c<3;++c){if(!std::isfinite(p[c]))throw std::runtime_error("Invalid avatar skinned position");v.position[c]=p[c];v.normal[c]=normal[c]/length;}std::copy(vertex.uv.begin(),vertex.uv.end(),v.uv);
            }
        }
    }
    void render(const VkPresentInfoKHR& present,const SwapchainState& swapchain,const SceneDepth& depth,const PlayerFrame& frame,const vrm::RetargetResult& pose,bool& consumed) {
        auto actor=frame.actor_world;
        const auto source_height=frame.source_pose.rest.at("head")[13];
        const auto avatar_height=avatar.nodes[avatar.humanoid.at("head")].world[13];
        const auto scale=source_height/avatar_height;if(!std::isfinite(scale)||scale<0.1f||scale>10)throw std::runtime_error("Unsupported avatar height");
        const vrm::Matrix basis=avatar.version==vrm::VrmVersion::V1?vrm::Matrix{1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1}:vrm::Matrix{-1,0,0,0,0,1,0,0,0,0,-1,0,0,0,0,1};
        actor=product(actor,basis);for(int c=0;c<3;++c)for(int r=0;r<3;++r)actor[c*4+r]*=scale;actor[12]+=debug_offset;
        skin_vertices(pose,actor);
        const auto mvp=product(frame.camera_stable_world_to_clip,actor);FrameConstants constants{};
        for(int row=0;row<4;++row)for(int col=0;col<4;++col)constants.rows[row*4+col]=mvp[col*4+row];
        constants.screen[0]=1.0f/extent.width;constants.screen[1]=1.0f/extent.height;
        constants.screen[2]=frame.camera_jitter_uv[0];constants.screen[3]=frame.camera_jitter_uv[1];
        std::memcpy(uniform.mapped,&constants,sizeof constants);
        begin_command();
        image_barrier(depth.image,depth.layout,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_SHADER_READ_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        const auto image_index=present.pImageIndices[0];
        image_barrier(swapchain.images.at(image_index),VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_ACCESS_MEMORY_WRITE_BIT,VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        std::array<VkClearValue,2> clears{};clears[1].depthStencil.depth=0;
        VkRenderPassBeginInfo begin{};begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;begin.renderPass=renderpass;begin.framebuffer=framebuffers.at(image_index);begin.renderArea={{0,0},extent};begin.clearValueCount=clears.size();begin.pClearValues=clears.data();
        V(vkCmdBeginRenderPass)(command,&begin,VK_SUBPASS_CONTENTS_INLINE);
        const VkViewport viewport{0,0,static_cast<float>(extent.width),static_cast<float>(extent.height),0,1};const VkRect2D scissor{{0,0},extent};
        V(vkCmdSetViewport)(command,0,1,&viewport);V(vkCmdSetScissor)(command,0,1,&scissor);
        const VkDeviceSize offset=0;V(vkCmdBindVertexBuffers)(command,0,1,&vertices.handle,&offset);V(vkCmdBindIndexBuffer)(command,indices.handle,0,VK_INDEX_TYPE_UINT32);
        for(int pass=0;pass<2;++pass) {
            V(vkCmdBindPipeline)(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pass?blend:opaque);
            for(const auto& draw:draws) {
                const vrm::Material fallback{};const auto& material=draw.material>=0?avatar.materials[draw.material]:fallback;
                const bool transparent=material.alpha==vrm::AlphaMode::Blend;if(transparent!=(pass==1))continue;
                const auto set=descriptor_sets[draw.material>=0?static_cast<std::size_t>(draw.material):avatar.materials.size()];
                V(vkCmdBindDescriptorSets)(command,VK_PIPELINE_BIND_POINT_GRAPHICS,pipeline_layout,0,1,&set,0,nullptr);
                MaterialConstants parameters{};std::copy(material.base_color.begin(),material.base_color.end(),parameters.color);parameters.cutoff=material.alpha_cutoff;
                parameters.alpha=material.alpha==vrm::AlphaMode::Mask?1:transparent?2:0;parameters.unlit=material.unlit?1:0;
                parameters.gamma=current_format==VK_FORMAT_R8G8B8A8_SRGB||current_format==VK_FORMAT_B8G8R8A8_SRGB?0:1;
                V(vkCmdPushConstants)(command,pipeline_layout,VK_SHADER_STAGE_FRAGMENT_BIT,0,sizeof parameters,&parameters);
                V(vkCmdDrawIndexed)(command,static_cast<uint32_t>(draw.index_count),1,static_cast<uint32_t>(draw.first_index),static_cast<int32_t>(draw.first_vertex),0);
            }
        }
        V(vkCmdEndRenderPass)(command);
        image_barrier(swapchain.images.at(image_index),VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT,VK_ACCESS_MEMORY_READ_BIT,VK_IMAGE_ASPECT_COLOR_BIT);
        if(depth.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)image_barrier(depth.image,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,depth.layout,VK_ACCESS_SHADER_READ_BIT,VK_ACCESS_MEMORY_READ_BIT,VK_IMAGE_ASPECT_DEPTH_BIT);
        submit_wait(present.waitSemaphoreCount,present.pWaitSemaphores,&consumed);
    }
    ~Impl() {
        if(!device)return;
        destroy_target();
        if(vertex_shader)V(vkDestroyShaderModule)(device,vertex_shader,nullptr);
        if(fragment_shader)V(vkDestroyShaderModule)(device,fragment_shader,nullptr);
        if(descriptor_pool)V(vkDestroyDescriptorPool)(device,descriptor_pool,nullptr);
        if(pipeline_layout)V(vkDestroyPipelineLayout)(device,pipeline_layout,nullptr);
        if(descriptor_layout)V(vkDestroyDescriptorSetLayout)(device,descriptor_layout,nullptr);
        for(auto sampler:samplers)if(sampler)V(vkDestroySampler)(device,sampler,nullptr);
        if(depth_sampler)V(vkDestroySampler)(device,depth_sampler,nullptr);
        if(scene_view)V(vkDestroyImageView)(device,scene_view,nullptr);
        for(auto& image:textures)destroy_image(image);
        destroy_image(white);
        destroy_buffer(vertices);
        destroy_buffer(indices);
        destroy_buffer(uniform);
        if(command_pool)V(vkDestroyCommandPool)(device,command_pool,nullptr);
    }
#undef V
};
AvatarRenderer::AvatarRenderer(VkInstance instance,VkPhysicalDevice physical,VkDevice device,PFN_vkGetInstanceProcAddr gipa,PFN_vkGetDeviceProcAddr gdpa,PFN_vkSetDeviceLoaderData set_loader):impl(std::make_unique<Impl>()) {
    const auto path=env(L"ENSHROUDED_VRM_MODEL");if(path.empty())return;
    impl->device=device;impl->gdpa=gdpa;impl->set_loader=set_loader;
    const auto get_memory=reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"));get_memory(physical,&impl->memory);
    const auto get_queues=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance,"vkGetPhysicalDeviceQueueFamilyProperties"));
    uint32_t queue_count=0;get_queues(physical,&queue_count,nullptr);impl->queue_properties.resize(queue_count);get_queues(physical,&queue_count,impl->queue_properties.data());impl->queue_properties.resize(queue_count);
    auto loaded=vrm::load_vrm(std::filesystem::path(path));if(!loaded)throw std::runtime_error("Avatar import: "+loaded.error);impl->avatar=std::move(loaded.avatar);
    if(env(L"ENSHROUDED_VRM_SPRING_BONES")!=L"0"&&!impl->avatar.spring_joints.empty()) {
        impl->springs=vrm::make_optional_springs(impl->avatar,vrm::SpringMotion::Inertial);
        if(!impl->springs)log_event("avatar.spring-disabled allocation failure");
        else if(!impl->springs->ready()){log_event_lazy([&]{return "avatar.spring-disabled "+impl->springs->error();});impl->springs.reset();}
        else log_event_lazy([&]{return "avatar.spring-ready motion=inertial-world joints="+std::to_string(impl->springs->joint_count())+" colliders="+std::to_string(impl->avatar.spring_colliders.size());});
    }
    impl->bridge=std::make_unique<EnshroudedBridge>(std::filesystem::path(env(L"ENSHROUDED_VRM_PROFILE")),std::filesystem::path(env(L"ENSHROUDED_VRM_CATALOG")),narrow(env(L"ENSHROUDED_VRM_VERIFIED_BUILD")));
    if(!impl->bridge->ready())throw std::runtime_error("Game pose profile: "+impl->bridge->error());
    impl->shader_directory=std::filesystem::path(env(L"ENSHROUDED_VRM_SHADERS"));
    const auto offset=env(L"ENSHROUDED_VRM_DEBUG_OFFSET");if(!offset.empty())impl->debug_offset=std::stof(offset);
    impl->active=true;log_event("avatar.cpu-ready title="+impl->avatar.title+" vertices="+std::to_string(impl->avatar.vertex_count));
    impl->native_replacement=env(L"ENSHROUDED_VRM_NATIVE_REPLACEMENT")==L"1";
    impl->native_mode=env(L"ENSHROUDED_VRM_NATIVE_ADDITIVE")==L"1"||impl->native_replacement;
    impl->gpu_skinning=env(L"ENSHROUDED_VRM_GPU_SKINNING")!=L"0";
    if(impl->native_mode)log_event(impl->gpu_skinning?"avatar.skinning mode=gpu":"avatar.skinning mode=cpu-comparison");
    if(impl->native_mode)impl->native_asset=std::make_shared<const vrm::Avatar>(impl->avatar);
    if(impl->native_replacement) {
        impl->secondary_contact=std::make_unique<SecondaryHandContact>(impl->avatar);
        log_event(impl->secondary_contact->ready()?"avatar.secondary-contact-ready":"avatar.secondary-contact-disabled unsupported arm chain/constraints");
    }
    for(const auto& warning:impl->avatar.warnings)log_event("avatar.limitation "+warning);
}
AvatarRenderer::~AvatarRenderer()=default;
bool AvatarRenderer::enabled() const{return impl->active;}
std::shared_ptr<const NativeAvatarPublication> AvatarRenderer::native_publication()const noexcept {
    try {std::lock_guard<std::mutex> resources(impl->resource_mutex);return impl->publication;}catch(...){return{};}
}
bool AvatarRenderer::before_present(VkQueue queue,const VkPresentInfoKHR& present,GpuProbe& probe,GraphicsState& graphics,bool& consumed,uint64_t source_buffer_epoch) noexcept {
    consumed=false;if(!enabled()||present.swapchainCount!=1)return false;
    try {
        std::lock_guard<std::mutex> resources(impl->resource_mutex);
        const auto previous=impl->publication;
        if(impl->native_mode)impl->publication.reset();
        const auto capture_sequence=++impl->capture_sequence;
        uint32_t family=0;
        if(!probe.queue_family(queue,family)||family>=impl->queue_properties.size()||!(impl->queue_properties[family].queueFlags&VK_QUEUE_GRAPHICS_BIT)){impl->report("Presentation queue does not support avatar graphics");return false;}
        const auto depth=graphics.scene_depth();const auto swapchain=graphics.swapchain(present.pSwapchains[0]);
        if(!depth||swapchain.images.empty()){impl->report("Scene depth/swapchain not ready");return false;}
        if(depth.format!=VK_FORMAT_D32_SFLOAT||depth.samples!=VK_SAMPLE_COUNT_1_BIT||!(depth.usage&VK_IMAGE_USAGE_SAMPLED_BIT)||depth.layout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL){impl->report("Unsupported scene depth layout/format");return false;}
        const auto accessible=[&](VkSharingMode sharing,uint32_t owner,const std::vector<uint32_t>& families){return sharing==VK_SHARING_MODE_CONCURRENT?std::find(families.begin(),families.end(),family)!=families.end():owner==family;};
        if(!accessible(depth.sharing_mode,depth.owner_queue_family,depth.queue_families)){impl->report("Scene depth is owned by another queue family");return false;}
        if(!(swapchain.usage&VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)||!swapchain.extent.width||!swapchain.extent.height||
            (swapchain.format!=VK_FORMAT_R8G8B8A8_UNORM&&swapchain.format!=VK_FORMAT_R8G8B8A8_SRGB&&swapchain.format!=VK_FORMAT_B8G8R8A8_UNORM&&swapchain.format!=VK_FORMAT_B8G8R8A8_SRGB)){impl->report("Unsupported presentation image usage/format");return false;}
        const auto image_index=present.pImageIndices[0];
        if(image_index>=swapchain.images.size()||image_index>=swapchain.image_owners.size()||!accessible(swapchain.sharing_mode,swapchain.image_owners[image_index],swapchain.queue_families)){impl->report("Presentation image is owned by another queue family");return false;}
        const auto started=std::chrono::steady_clock::now();
        auto limits=impl->bridge->readback_limits();
        const bool capture_anchor=probe.player_anchor_capture_enabled();
        if(capture_anchor)limits["player_anchor"]=2048;
        GpuSnapshot snapshot;
        const auto hold=[&](const std::shared_ptr<const NativeAvatarPublication>& source){
            std::uint64_t owner=0,observed=0;
            const auto raw=source?read_native_hand_contact_mode(*source,GetTickCount64(),owner,&observed):SecondaryHandContactMode::None;
            if(source)impl->motion(source->source_instances).contact.observe(raw,source->published_milliseconds,observed,owner,source_buffer_epoch);
            auto value=hold_native_contact_publication(source,source_buffer_epoch,GetTickCount64(),impl->frame_count+1,raw,owner,
                [](const vrm::Avatar&asset,const vrm::RetargetResult&baseline){return std::make_shared<NativeAvatarPose>(make_native_avatar_pose(asset,baseline));});
            if(!value)return std::shared_ptr<NativeAvatarPublication>{};
            auto result=std::make_shared<NativeAvatarPublication>(*value);result->additional_views.clear();
            result->serial=++impl->frame_count;result->presentation_serial=capture_sequence;return result;
        };
        if(impl->native_mode&&source_buffer_epoch&&!capture_anchor) {
            probe.configure_async(queue,limits);
            snapshot=probe.poll_async(queue);
            if(snapshot.asynchronous&&snapshot.source_epoch!=source_buffer_epoch)snapshot={};
            if(snapshot.buffers.empty()&&probe.async_frame_scheduled(queue)) {
                if(auto held=retain_native_publication_views({},previous,hold)){
                    impl->publication=std::move(held);return true;
                }
                impl->report("Buffered pose pending; no current pose to hold");return false;
            }
        }
        if(snapshot.buffers.empty()) {
            const auto sampled_ms=GetTickCount64();
            snapshot=probe.readback(queue,limits,false,present.waitSemaphoreCount,present.pWaitSemaphores,&consumed);
            snapshot.source_epoch=source_buffer_epoch;snapshot.captured_milliseconds=sampled_ms;
        }
        const auto copied=std::chrono::steady_clock::now();
        if(capture_anchor)probe.capture_snapshot(snapshot,capture_sequence);
        if(impl->native_mode){
            if(snapshot.additional_views.size()>3)throw std::runtime_error("Native view budget exceeded");
            auto children=std::move(snapshot.additional_views);snapshot.additional_views.clear();
            std::vector<GpuSnapshot> views;views.push_back(std::move(snapshot));
            for(auto& child:children){if(!child.additional_views.empty())throw std::runtime_error("Nested native views");views.push_back(std::move(child));}
            std::shared_ptr<NativeAvatarPublication> root;
            std::set<std::uint64_t> sources;
            for(auto& view:views){
                if(!view.asynchronous)view.source_epoch=source_buffer_epoch;
                if(!view.captured_milliseconds)view.captured_milliseconds=views.front().captured_milliseconds;
                try{
                    const auto source=reinterpret_cast<std::uintptr_t>(view.buffers.at("instances").binding.buffer);
                    if(!sources.insert(source).second)throw std::runtime_error("Duplicate native view allocation");
                    if(view.source_epoch!=source_buffer_epoch)throw std::runtime_error("Native view source epoch changed");
                    auto publication=impl->build_native_view(view,++impl->frame_count,capture_sequence);
                    if(!root)root=std::move(publication);else root->additional_views.push_back(std::move(publication));
                }catch(const std::exception& error){impl->report("View pose: "+std::string(error.what()));}
            }
            impl->publication=retain_native_publication_views(root,previous,[&](const auto& view){
                return sources.count(view->source_instances)?std::shared_ptr<NativeAvatarPublication>{}:hold(view);
            });
            if(!impl->publication)return false;
            const auto finished=std::chrono::steady_clock::now();
            impl->timings[0]+=std::chrono::duration<double,std::milli>(finished-started).count();
            impl->timings[1]+=std::chrono::duration<double,std::milli>(copied-started).count();
            if(++impl->native_timing_count==300){std::string detail="avatar.native-timing-ms total,readback,identify,retarget,geometry=";
                for(auto value:impl->timings)detail+=std::to_string(value/impl->native_timing_count)+",";
                log_event(detail);impl->timings={};impl->native_timing_count=0;}
            if(impl->frame_count<=4||impl->frame_count%300==0)log_event("avatar.native-pose-published count="+std::to_string(impl->frame_count)+" views="+std::to_string(1+impl->publication->additional_views.size()));
            impl->last_error.clear();return true;
        }
        const auto frame=impl->bridge->read_player_frame(snapshot);if(!frame){impl->report(frame.error);return false;}
        const auto identified=std::chrono::steady_clock::now();
        if(!impl->native_mode) {
            if(!impl->initialized)impl->initialize(queue,snapshot.queue_family,depth,swapchain);
            if(impl->queue!=queue||impl->family!=snapshot.queue_family)throw std::runtime_error("Graphics queue changed");
            if(impl->current_swapchain!=swapchain.swapchain||impl->current_swapchain_generation!=swapchain.generation)impl->create_target(swapchain);
            impl->replace_scene_depth(depth);
        }
        vrm::RetargetOptions options;options.align_rest_directions=true;
        if(impl->avatar.version==vrm::VrmVersion::V0)options.source_to_avatar_basis={-1,0,0,0,0,1,0,0,0,0,-1,0,0,0,0,1};
        options.source_rest_height=frame.source_pose.rest.at("head")[13];options.avatar_rest_height=impl->avatar.nodes[impl->avatar.humanoid.at("head")].world[13];
        auto pose=impl->retarget_pose(frame.source_pose,options);if(!pose){impl->report("Retarget: "+pose.error);return false;}
        impl->apply_springs(pose,frame,snapshot.captured_milliseconds,snapshot.source_epoch,options.source_rest_height/options.avatar_rest_height);
        const auto posed=std::chrono::steady_clock::now();

        auto rendering_present=present;
        if(consumed){rendering_present.waitSemaphoreCount=0;rendering_present.pWaitSemaphores=nullptr;}
        impl->render(rendering_present,swapchain,depth,frame,pose,consumed);
        const auto finished=std::chrono::steady_clock::now();
        const auto ms=[](auto a,auto b){return std::chrono::duration<double,std::milli>(b-a).count();};
        const std::array<double,5> elapsed{ms(started,finished),ms(started,copied),ms(copied,identified),ms(identified,posed),ms(posed,finished)};
        for(std::size_t i=0;i<elapsed.size();++i)impl->timings[i]+=elapsed[i];
        ++impl->frame_count;if(impl->frame_count==1||impl->frame_count%300==0)log_event("avatar.frame-rendered count="+std::to_string(impl->frame_count)+" source-joints="+std::to_string(frame.observed_joint_count)+" parts="+std::to_string(frame.parts.size()));
        impl->player_trace.sample(impl->frame_count,frame,pose,impl->avatar);
        if(impl->frame_count%300==0){std::string detail="avatar.timing-ms total,readback,identify,retarget,render=";for(auto value:impl->timings)detail+=std::to_string(value/300)+",";log_event(detail);impl->timings={};}
        impl->last_error.clear();return true;
    }catch(const std::exception& error){impl->report(error.what());impl->active=false;return false;}catch(...){impl->report("Unknown renderer failure");impl->active=false;return false;}
}
void AvatarRenderer::native_image_destroyed(VkImage image) noexcept {
    try {
        std::lock_guard<std::mutex> resources(impl->resource_mutex);
        if(impl->scene_image!=image)return;
        if(impl->scene_view)impl->function<PFN_vkDestroyImageView>("vkDestroyImageView")(impl->device,impl->scene_view,nullptr);
        impl->scene_view={};impl->scene_image={};impl->scene_generation=0;
    }catch(...){log_event("avatar.image-retirement.failed");}
}
void AvatarRenderer::native_swapchain_destroyed(VkSwapchainKHR swapchain) noexcept {
    try {
        std::lock_guard<std::mutex> resources(impl->resource_mutex);
        if(impl->current_swapchain==swapchain)impl->destroy_target();
    }catch(...){log_event("avatar.swapchain-retirement.failed");}
}
}
