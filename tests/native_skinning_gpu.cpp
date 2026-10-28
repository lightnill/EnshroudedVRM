#if __has_include("../src/native_skinning_gpu.hpp")
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define VK_NO_PROTOTYPES
#include <windows.h>
#include "../src/native_skinning_gpu.hpp"
#include <algorithm>
#include <cmath>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <stdexcept>
namespace {
void require(bool v,const char* s){if(!v)throw std::runtime_error(s);}
void checked(VkResult r,const char* s){if(r!=VK_SUCCESS)throw std::runtime_error(std::string(s)+": "+std::to_string(r));}
PFN_vkGetDeviceProcAddr native_get{};PFN_vkCmdPushConstants native_push{};unsigned private_push_writes=0;
void VKAPI_CALL observed_push(VkCommandBuffer cmd,VkPipelineLayout layout,VkShaderStageFlags stages,std::uint32_t offset,std::uint32_t bytes,const void* data){++private_push_writes;native_push(cmd,layout,stages,offset,bytes,data);}
PFN_vkVoidFunction VKAPI_CALL observed_get(VkDevice device,const char* name){
    if(std::strcmp(name,"vkCmdPushConstants")==0)return reinterpret_cast<PFN_vkVoidFunction>(observed_push);
    return native_get(device,name);
}
template<class T>T info(VkStructureType s){T t{};t.sType=s;return t;}
vrm::Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
struct Context {
    HMODULE loader{};PFN_vkGetInstanceProcAddr gipa{};PFN_vkGetDeviceProcAddr get{};VkInstance instance{};VkDevice device{};VkQueue queue{};
    VkPhysicalDeviceMemoryProperties memory{};VkPhysicalDeviceLimits limits{};VkCommandPool pool{};VkCommandBuffer command{};VkFence fence{};bool uncertain=false;
    template<class T>T fn(const char* n){return reinterpret_cast<T>(get(device,n));}
    Context(){loader=LoadLibraryW(L"vulkan-1.dll");require(bool(loader),"Vulkan loader missing");gipa=reinterpret_cast<PFN_vkGetInstanceProcAddr>(reinterpret_cast<void*>(GetProcAddress(loader,"vkGetInstanceProcAddr")));
        auto app=info<VkApplicationInfo>(VK_STRUCTURE_TYPE_APPLICATION_INFO);app.apiVersion=VK_API_VERSION_1_1;
        auto ci=info<VkInstanceCreateInfo>(VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO);ci.pApplicationInfo=&app;checked(reinterpret_cast<PFN_vkCreateInstance>(gipa(nullptr,"vkCreateInstance"))(&ci,nullptr,&instance),"instance");
        auto enumerate=reinterpret_cast<PFN_vkEnumeratePhysicalDevices>(gipa(instance,"vkEnumeratePhysicalDevices"));std::uint32_t count=0;checked(enumerate(instance,&count,nullptr),"devices");std::vector<VkPhysicalDevice> devices(count);checked(enumerate(instance,&count,devices.data()),"devices");
        VkPhysicalDevice physical{};std::uint32_t family=0;
        for(auto candidate:devices){VkPhysicalDeviceProperties props{};reinterpret_cast<PFN_vkGetPhysicalDeviceProperties>(gipa(instance,"vkGetPhysicalDeviceProperties"))(candidate,&props);
            auto queues=reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(gipa(instance,"vkGetPhysicalDeviceQueueFamilyProperties"));std::uint32_t qc=0;queues(candidate,&qc,nullptr);std::vector<VkQueueFamilyProperties> qs(qc);queues(candidate,&qc,qs.data());
            for(unsigned i=0;i<qc;++i)if(qs[i].queueCount&&(qs[i].queueFlags&VK_QUEUE_COMPUTE_BIT)){physical=candidate;family=i;limits=props.limits;std::cout<<"DEVICE "<<props.deviceName<<'\n';break;}
            if(physical)break;
        }require(bool(physical),"compute queue missing");reinterpret_cast<PFN_vkGetPhysicalDeviceMemoryProperties>(gipa(instance,"vkGetPhysicalDeviceMemoryProperties"))(physical,&memory);
        float priority=1;auto qi=info<VkDeviceQueueCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO);qi.queueFamilyIndex=family;qi.queueCount=1;qi.pQueuePriorities=&priority;
        auto di=info<VkDeviceCreateInfo>(VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO);di.queueCreateInfoCount=1;di.pQueueCreateInfos=&qi;checked(reinterpret_cast<PFN_vkCreateDevice>(gipa(instance,"vkCreateDevice"))(physical,&di,nullptr,&device),"device");get=reinterpret_cast<PFN_vkGetDeviceProcAddr>(gipa(instance,"vkGetDeviceProcAddr"));fn<PFN_vkGetDeviceQueue>("vkGetDeviceQueue")(device,family,0,&queue);
        auto pi=info<VkCommandPoolCreateInfo>(VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO);pi.queueFamilyIndex=family;pi.flags=VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;checked(fn<PFN_vkCreateCommandPool>("vkCreateCommandPool")(device,&pi,nullptr,&pool),"pool");
        auto ai=info<VkCommandBufferAllocateInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO);ai.commandPool=pool;ai.level=VK_COMMAND_BUFFER_LEVEL_PRIMARY;ai.commandBufferCount=1;checked(fn<PFN_vkAllocateCommandBuffers>("vkAllocateCommandBuffers")(device,&ai,&command),"command");
        auto fi=info<VkFenceCreateInfo>(VK_STRUCTURE_TYPE_FENCE_CREATE_INFO);checked(fn<PFN_vkCreateFence>("vkCreateFence")(device,&fi,nullptr,&fence),"fence");
    }
    void begin(){checked(fn<PFN_vkResetCommandPool>("vkResetCommandPool")(device,pool,0),"reset");auto bi=info<VkCommandBufferBeginInfo>(VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO);checked(fn<PFN_vkBeginCommandBuffer>("vkBeginCommandBuffer")(command,&bi),"begin");}
    void submit(){checked(fn<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(command),"end");checked(fn<PFN_vkResetFences>("vkResetFences")(device,1,&fence),"reset fence");auto si=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);si.commandBufferCount=1;si.pCommandBuffers=&command;uncertain=true;checked(fn<PFN_vkQueueSubmit>("vkQueueSubmit")(queue,1,&si,fence),"submit");auto r=fn<PFN_vkWaitForFences>("vkWaitForFences")(device,1,&fence,VK_TRUE,5'000'000'000ull);if(r==VK_SUCCESS||r==VK_ERROR_DEVICE_LOST)uncertain=false;checked(r,"completion");}
    ~Context(){if(device&&!uncertain){fn<PFN_vkDestroyFence>("vkDestroyFence")(device,fence,nullptr);fn<PFN_vkDestroyCommandPool>("vkDestroyCommandPool")(device,pool,nullptr);fn<PFN_vkDestroyDevice>("vkDestroyDevice")(device,nullptr);}if(instance&&!uncertain)reinterpret_cast<PFN_vkDestroyInstance>(gipa(instance,"vkDestroyInstance"))(instance,nullptr);if(loader&&!uncertain)FreeLibrary(loader);}
};
struct HostBuffer {
    Context& c;VkBuffer buffer{};VkDeviceMemory memory{};void* mapped{};VkDeviceSize size;
    HostBuffer(Context& ctx,VkDeviceSize bytes):c(ctx),size(bytes){auto bi=info<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);bi.size=bytes;bi.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_TRANSFER_DST_BIT;checked(c.fn<PFN_vkCreateBuffer>("vkCreateBuffer")(c.device,&bi,nullptr,&buffer),"buffer");VkMemoryRequirements req{};c.fn<PFN_vkGetBufferMemoryRequirements>("vkGetBufferMemoryRequirements")(c.device,buffer,&req);unsigned type=c.memory.memoryTypeCount;for(unsigned i=0;i<c.memory.memoryTypeCount;++i)if((req.memoryTypeBits&(1u<<i))&&(c.memory.memoryTypes[i].propertyFlags&(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))==(VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)){type=i;break;}require(type<c.memory.memoryTypeCount,"host memory");auto ai=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);ai.allocationSize=req.size;ai.memoryTypeIndex=type;checked(c.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(c.device,&ai,nullptr,&memory),"allocate");checked(c.fn<PFN_vkBindBufferMemory>("vkBindBufferMemory")(c.device,buffer,memory,0),"bind");checked(c.fn<PFN_vkMapMemory>("vkMapMemory")(c.device,memory,0,bytes,0,&mapped),"map");std::memset(mapped,0,bytes);}
    VkDescriptorBufferInfo binding(){return {buffer,0,size};}
    ~HostBuffer(){if(c.uncertain)return;if(mapped)c.fn<PFN_vkUnmapMemory>("vkUnmapMemory")(c.device,memory);if(buffer)c.fn<PFN_vkDestroyBuffer>("vkDestroyBuffer")(c.device,buffer,nullptr);if(memory)c.fn<PFN_vkFreeMemory>("vkFreeMemory")(c.device,memory,nullptr);}
};
struct Input {vrm::Avatar avatar;vrm::RetargetResult pose;};
Input fixture(std::size_t count=3){Input f;vrm::Primitive p;p.skinned=true;p.indices={0,1,2};
    for(std::size_t i=0;i<count;++i){vrm::Vertex v;v.position={float(int(i%23)-11)*.03f,float(i%31)*.04f,float(int(i%17)-8)*.02f};v.normal={.3f,.7f,.5f};v.uv={.25f,.75f};v.joints={0,1,0,1};v.weights={.25f,.75f,0,0};p.vertices.push_back(v);}f.avatar.meshes.push_back({{p}});f.avatar.vertex_count=count;f.avatar.triangle_count=1;
    f.avatar.nodes.resize(2);f.pose.node_world.resize(2,identity());for(auto& n:f.avatar.nodes){n.active=true;n.local=n.world=identity();}f.avatar.nodes[0].mesh=0;f.avatar.nodes[0].skin=0;f.avatar.skins.push_back({{0,1},{identity(),identity()}});f.pose.palettes.push_back({0,0,{identity(),identity()}});return f;}
}
int main(int argc,char**argv){try{require(argc==2,"shader directory required");Context c;native_get=c.get;native_push=c.fn<PFN_vkCmdPushConstants>("vkCmdPushConstants");evrm::NativeSkinningGpu gpu(c.device,observed_get,c.memory,c.limits,std::filesystem::path(argv[1]),&c.uncertain);unsigned cases=0;double maxError=0;
    auto execute=[&](const Input& f,const std::shared_ptr<evrm::NativeSkinGpuAsset>& asset,const evrm::NativeSkinPose& packed,std::shared_ptr<const evrm::NativeSkinGpuGeometry> previous,bool admitted,bool good,bool history,std::vector<evrm::NativeAvatarVertex>* output=nullptr,bool compare_positions=true){
        HostBuffer frame(c,224),readback(c,packed.asset->vertices.size()*44);auto* words=static_cast<std::uint32_t*>(frame.mapped);words[48]=1;words[49]=admitted?1:0;
        auto work=gpu.frame(asset,packed,frame.binding(),previous);c.begin();gpu.record(c.command,work);
        auto source=work->geometry()->binding();VkBufferCopy copy{0,0,readback.size};c.fn<PFN_vkCmdCopyBuffer>("vkCmdCopyBuffer")(c.command,source.buffer,readback.buffer,1,&copy);
        auto barrier=info<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);barrier.srcAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;c.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(c.command,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);c.submit();
        if((words[48]==1)!=good)std::cerr<<"case="<<cases<<" commit="<<words[48]<<" status="<<work->status_after_completion()<<" expectedGood="<<good<<'\n';
        require((words[48]==1)==good,"skin error must revoke shared commit");require((work->status_after_completion()==0)==good,"skin status mismatch");
        if(good){auto expected=evrm::make_native_avatar_pose(f.avatar,f.pose);require(bool(expected),"reference rejected");auto* actual=static_cast<const evrm::NativeAvatarVertex*>(readback.mapped);for(std::size_t i=0;i<expected.vertices.size();++i){float a[11],b[11];std::memcpy(a,&actual[i],44);std::memcpy(b,&expected.vertices[i],44);for(unsigned j=0;j<8;++j){double err=std::abs(double(a[j])-b[j]);maxError=std::max(maxError,err);require(std::isfinite(a[j])&&((!compare_positions&&j<3)||err<=3e-5*(1+std::abs(double(b[j])))),"CPU/GPU differential");}if(!history)for(unsigned j=0;j<3;++j)require(actual[i].previous_position[j]==actual[i].position[j],"invalid history must reset to current");}
            for(auto index:packed.material_plan.blend_draws){const auto& d=packed.asset->layout->draws.at(index);for(unsigned i=0;i<d.index_count;++i){const auto& v=actual[packed.asset->layout->indices.at(d.first_index+i)];for(unsigned axis=0;axis<3;++axis){if(v.position[axis]<packed.material_plan.blend_min[axis]||v.position[axis]>packed.material_plan.blend_max[axis])std::cerr<<"GPU blend axis="<<axis<<" position="<<v.position[axis]<<" interval="<<packed.material_plan.blend_min[axis]<<","<<packed.material_plan.blend_max[axis]<<'\n';require(v.position[axis]>=packed.material_plan.blend_min[axis]&&v.position[axis]<=packed.material_plan.blend_max[axis],"GPU BLEND outside publication bound");}}}
            if(output)output->assign(actual,actual+expected.vertices.size());}
        ++cases;return work->geometry();
    };
    auto f=fixture(230548);auto source=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(f.avatar,f.pose));require(bool(*source),"source");auto asset=gpu.asset(source);c.begin();gpu.upload(c.command,asset);c.submit();gpu.finish_upload(asset);
    auto packed=evrm::make_native_skin_pose(source,f.pose);std::vector<evrm::NativeAvatarVertex> first;auto old=execute(f,asset,packed,{},false,true,false,&first);
    for(float scale:{.001f,.25f,2.f,100.f}){f.pose.node_world[0]=identity();f.pose.node_world[0][0]=-scale;f.pose.node_world[0][4]=scale*.2f;f.pose.node_world[0][5]=scale*1.7f;f.pose.node_world[0][10]=scale*.8f;packed=evrm::make_native_skin_pose(source,f.pose);execute(f,asset,packed,{},false,true,false);}
    f.pose.node_world[0]=identity();f.pose.node_world[0][12]=2;packed=evrm::make_native_skin_pose(source,f.pose);std::vector<evrm::NativeAvatarVertex> moved;execute(f,asset,packed,old,true,true,true,&moved);for(std::size_t i=0;i<first.size();++i)for(unsigned j=0;j<3;++j)require(moved[i].previous_position[j]==first[i].position[j],"previous committed geometry was not retained");
    execute(f,asset,packed,old,false,true,false);
    auto singular=packed;singular.matrices[0][0]=3;singular.matrices[1][0]=-1;auto bad=execute(f,asset,singular,{},false,false,false);execute(f,asset,packed,bad,false,true,false);
    auto corrupt=packed;corrupt.batches[0].base=0xffffffffu;execute(f,asset,corrupt,{},false,false,false);
    auto other=fixture();auto otherSource=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(other.avatar,other.pose));auto otherAsset=gpu.asset(otherSource);c.begin();gpu.upload(c.command,otherAsset);c.submit();gpu.finish_upload(otherAsset);bool rejected=false;try{gpu.frame(otherAsset,packed,{VK_NULL_HANDLE,0,224},{});}catch(...){rejected=true;}require(rejected,"cross-asset frame must reject");++cases;
    auto boundary=fixture();for(auto& v:boundary.avatar.meshes[0].primitives[0].vertices)v.weights={.250025f,.750075f,0,0};auto boundarySource=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(boundary.avatar,boundary.pose));require(bool(*boundarySource),"CPU accepted boundary weights");auto boundaryAsset=gpu.asset(boundarySource);c.begin();gpu.upload(c.command,boundaryAsset);c.submit();gpu.finish_upload(boundaryAsset);execute(boundary,boundaryAsset,evrm::make_native_skin_pose(boundarySource,boundary.pose),{},false,true,false);
    auto cancellation=fixture();cancellation.avatar.materials.resize(2);cancellation.avatar.materials[1].alpha=vrm::AlphaMode::Blend;
    auto& primitive=cancellation.avatar.meshes[0].primitives[0];primitive.material=0;for(auto& v:primitive.vertices){v.position[0]=10000;v.weights={.25f,.75005f,0,0};}auto transparent=primitive;transparent.material=1;cancellation.avatar.meshes[0].primitives.push_back(transparent);cancellation.avatar.vertex_count=6;cancellation.avatar.triangle_count=2;
    for(auto& m:cancellation.pose.palettes[0].matrices){m[0]=1.0000001192092896f;m[12]=-10000.0009765625f;}
    auto cancellationSource=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(cancellation.avatar,cancellation.pose));require(bool(*cancellationSource),"CPU accepted cancellation asset");auto cancellationAsset=gpu.asset(cancellationSource);c.begin();gpu.upload(c.command,cancellationAsset);c.submit();gpu.finish_upload(cancellationAsset);execute(cancellation,cancellationAsset,evrm::make_native_skin_pose(cancellationSource,cancellation.pose),{},false,true,false,nullptr,false);
    for(float scale:{.001f,.25f,2.f,100.f}){auto& world=cancellation.pose.node_world[0];world=identity();world[0]=-scale;world[4]=scale*.2f;world[5]=scale*1.7f;world[10]=scale*.8f;world[12]=.01f;execute(cancellation,cancellationAsset,evrm::make_native_skin_pose(cancellationSource,cancellation.pose),{},false,true,false,nullptr,false);}
    auto instances=fixture();instances.avatar.nodes[1].mesh=0;instances.avatar.nodes[1].skin=0;instances.pose.node_world[1][12]=2;auto second=instances.pose.palettes[0];second.mesh_node=1;second.matrices[1][13]=1;instances.pose.palettes.push_back(second);
    auto instanceSource=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(instances.avatar,instances.pose));require(bool(*instanceSource),"multiple instance source");auto instanceAsset=gpu.asset(instanceSource);c.begin();gpu.upload(c.command,instanceAsset);c.submit();gpu.finish_upload(instanceAsset);execute(instances,instanceAsset,evrm::make_native_skin_pose(instanceSource,instances.pose),{},false,true,false);
    auto fixed=fixture();fixed.avatar.meshes[0].primitives[0].skinned=false;fixed.pose.node_world[0][0]=-2;fixed.pose.node_world[0][4]=.2f;
    auto fixedSource=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(fixed.avatar,fixed.pose));require(bool(*fixedSource),"static source");auto fixedAsset=gpu.asset(fixedSource);c.begin();gpu.upload(c.command,fixedAsset);c.submit();gpu.finish_upload(fixedAsset);execute(fixed,fixedAsset,evrm::make_native_skin_pose(fixedSource,fixed.pose),{},false,true,false);
    require(private_push_writes==0,"private skin commands must preserve native push constants");++cases;
    std::cout<<"PASS NativeSkinningGPU cases="<<cases<<" max_absolute_error="<<maxError<<"; production pipelines and buffers; no in-game FPS claim\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL NativeSkinningGPU: "<<e.what()<<'\n';return 1;}}
#else
#include <iostream>
int main(){std::cerr<<"FAIL NativeSkinningGPU: resident production pipeline not implemented\n";return 1;}
#endif
