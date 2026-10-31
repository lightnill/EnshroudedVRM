// Resource ownership/failure tests use the real skinning implementation with a
// deterministic Vulkan boundary. Large memory requirements are simulated; no
// private model, game process, or large physical GPU allocation is needed.
#define VK_NO_PROTOTYPES
#include "../src/native_skinning_gpu.hpp"
#include <algorithm>
#include <array>
#include <cstring>
#include <iostream>
#include <map>
#include <stdexcept>
#include <type_traits>

namespace {
void check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class T>T handle(std::uint64_t id){if constexpr(std::is_pointer<T>::value)return reinterpret_cast<T>(std::uintptr_t(id));else return static_cast<T>(id);}
vrm::Matrix identity(){return {1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1};}
struct Buffer {VkDeviceSize bytes=0,reserved=0;VkBufferUsageFlags usage=0;VkDeviceMemory memory{};};
struct Memory {VkDeviceSize bytes=0;unsigned type=0;std::vector<unsigned char> mapped;};
struct Counts {unsigned buffers=0,allocations=0,maps=0,pools=0,sets=0,writes=0,destroy_buffers=0,frees=0,unmaps=0,destroy_pools=0,destroy_pipelines=0;};
struct Driver {
    std::uint64_t serial=1;Counts count;VkDeviceSize floor=0,next_requirement=0,allocated=0,peak=0;
    bool fail_memory=false,fail_pool=false,fail_set=false;
    std::map<VkBuffer,Buffer> buffers;std::map<VkDeviceMemory,Memory> memories;
    std::map<VkDescriptorPool,std::vector<VkDescriptorSet>> pools;
    std::map<VkDescriptorSet,std::array<VkDescriptorBufferInfo,8>> sets;
    VkDescriptorSet last_set{};
    template<class T>T next(){return handle<T>(++serial);}
};
Driver* driver=nullptr;
VkResult VKAPI_CALL create_buffer(VkDevice,const VkBufferCreateInfo* i,const VkAllocationCallbacks*,VkBuffer* b){*b=driver->next<VkBuffer>();driver->buffers[*b]={i->size,std::max((i->size+15)/16*16,std::max(driver->floor,driver->next_requirement)),i->usage,{}};driver->next_requirement=0;++driver->count.buffers;return VK_SUCCESS;}
void VKAPI_CALL requirements(VkDevice,VkBuffer b,VkMemoryRequirements* r){*r={driver->buffers.at(b).reserved,16,3};}
VkResult VKAPI_CALL allocate(VkDevice,const VkMemoryAllocateInfo* i,const VkAllocationCallbacks*,VkDeviceMemory* m){if(driver->fail_memory){driver->fail_memory=false;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}*m=driver->next<VkDeviceMemory>();driver->memories[*m]={i->allocationSize,i->memoryTypeIndex,{}};driver->allocated+=i->allocationSize;driver->peak=std::max(driver->peak,driver->allocated);++driver->count.allocations;return VK_SUCCESS;}
VkResult VKAPI_CALL bind(VkDevice,VkBuffer b,VkDeviceMemory m,VkDeviceSize){driver->buffers.at(b).memory=m;return VK_SUCCESS;}
VkResult VKAPI_CALL map(VkDevice,VkDeviceMemory m,VkDeviceSize,VkDeviceSize bytes,VkMemoryMapFlags,void** p){auto& allocation=driver->memories.at(m);check(allocation.type==1,"host resource reused device-local allocation");allocation.mapped.resize(std::size_t(bytes),0xCD);*p=allocation.mapped.data();++driver->count.maps;return VK_SUCCESS;}
void VKAPI_CALL unmap(VkDevice,VkDeviceMemory){++driver->count.unmaps;}
void VKAPI_CALL destroy_buffer(VkDevice,VkBuffer b,const VkAllocationCallbacks*){check(driver->buffers.erase(b)==1,"buffer destroyed twice");++driver->count.destroy_buffers;}
void VKAPI_CALL free_memory(VkDevice,VkDeviceMemory m,const VkAllocationCallbacks*){driver->allocated-=driver->memories.at(m).bytes;driver->memories.erase(m);++driver->count.frees;}
VkResult VKAPI_CALL create_set_layout(VkDevice,const VkDescriptorSetLayoutCreateInfo*,const VkAllocationCallbacks*,VkDescriptorSetLayout* s){*s=driver->next<VkDescriptorSetLayout>();return VK_SUCCESS;}
void VKAPI_CALL destroy_set_layout(VkDevice,VkDescriptorSetLayout,const VkAllocationCallbacks*){check(driver->pools.empty(),"descriptor cache outlived its layout");}
VkResult VKAPI_CALL create_layout(VkDevice,const VkPipelineLayoutCreateInfo*,const VkAllocationCallbacks*,VkPipelineLayout* l){*l=driver->next<VkPipelineLayout>();return VK_SUCCESS;}
void VKAPI_CALL destroy_layout(VkDevice,VkPipelineLayout,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL create_shader(VkDevice,const VkShaderModuleCreateInfo*,const VkAllocationCallbacks*,VkShaderModule* s){*s=driver->next<VkShaderModule>();return VK_SUCCESS;}
void VKAPI_CALL destroy_shader(VkDevice,VkShaderModule,const VkAllocationCallbacks*){}
VkResult VKAPI_CALL create_pipeline(VkDevice,VkPipelineCache,unsigned count,const VkComputePipelineCreateInfo*,const VkAllocationCallbacks*,VkPipeline* p){for(unsigned i=0;i<count;++i)p[i]=driver->next<VkPipeline>();return VK_SUCCESS;}
void VKAPI_CALL destroy_pipeline(VkDevice,VkPipeline,const VkAllocationCallbacks*){++driver->count.destroy_pipelines;}
VkResult VKAPI_CALL create_pool(VkDevice,const VkDescriptorPoolCreateInfo*,const VkAllocationCallbacks*,VkDescriptorPool* p){if(driver->fail_pool){driver->fail_pool=false;return VK_ERROR_OUT_OF_DEVICE_MEMORY;}*p=driver->next<VkDescriptorPool>();driver->pools[*p]={};++driver->count.pools;return VK_SUCCESS;}
void VKAPI_CALL destroy_pool(VkDevice,VkDescriptorPool p,const VkAllocationCallbacks*){for(auto set:driver->pools.at(p))driver->sets.erase(set);driver->pools.erase(p);++driver->count.destroy_pools;}
VkResult VKAPI_CALL allocate_set(VkDevice,const VkDescriptorSetAllocateInfo* i,VkDescriptorSet* s){if(driver->fail_set){driver->fail_set=false;return VK_ERROR_OUT_OF_POOL_MEMORY;}check(i->descriptorSetCount==1,"unexpected descriptor pack shape");*s=driver->next<VkDescriptorSet>();driver->pools.at(i->descriptorPool).push_back(*s);driver->sets[*s]={};++driver->count.sets;return VK_SUCCESS;}
void VKAPI_CALL update_sets(VkDevice,unsigned count,const VkWriteDescriptorSet* w,unsigned,const VkCopyDescriptorSet*){check(count==8,"reused descriptor pack was not fully rewritten");for(unsigned i=0;i<count;++i){check(w[i].descriptorCount==1&&w[i].dstBinding<8,"invalid descriptor write");const auto& binding=*w[i].pBufferInfo;check(binding.range==driver->buffers.at(binding.buffer).bytes,"buffer cache changed logical descriptor range");driver->sets.at(w[i].dstSet)[w[i].dstBinding]=binding;driver->last_set=w[i].dstSet;}++driver->count.writes;}
void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,unsigned,const VkMemoryBarrier*,unsigned,const VkBufferMemoryBarrier*,unsigned,const VkImageMemoryBarrier*){}
void VKAPI_CALL copy(VkCommandBuffer,VkBuffer source,VkBuffer destination,unsigned,const VkBufferCopy*){check((driver->buffers.at(source).usage&VK_BUFFER_USAGE_TRANSFER_SRC_BIT)!=0,"staging cache key lost transfer-source usage");check((driver->buffers.at(destination).usage&VK_BUFFER_USAGE_TRANSFER_DST_BIT)!=0,"buffer cache key lost transfer-destination usage");}
void VKAPI_CALL bind_pipeline(VkCommandBuffer,VkPipelineBindPoint,VkPipeline){}
void VKAPI_CALL bind_sets(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,unsigned,unsigned,const VkDescriptorSet*,unsigned,const unsigned*){}
void VKAPI_CALL dispatch(VkCommandBuffer,unsigned,unsigned,unsigned){}
PFN_vkVoidFunction VKAPI_CALL get(VkDevice,const char* name){
#define FN(n,f) if(std::strcmp(name,n)==0)return reinterpret_cast<PFN_vkVoidFunction>(f)
    FN("vkCreateBuffer",create_buffer);FN("vkGetBufferMemoryRequirements",requirements);FN("vkAllocateMemory",allocate);FN("vkBindBufferMemory",bind);FN("vkMapMemory",map);FN("vkUnmapMemory",unmap);FN("vkDestroyBuffer",destroy_buffer);FN("vkFreeMemory",free_memory);
    FN("vkCreateDescriptorSetLayout",create_set_layout);FN("vkDestroyDescriptorSetLayout",destroy_set_layout);FN("vkCreatePipelineLayout",create_layout);FN("vkDestroyPipelineLayout",destroy_layout);FN("vkCreateShaderModule",create_shader);FN("vkDestroyShaderModule",destroy_shader);FN("vkCreateComputePipelines",create_pipeline);FN("vkDestroyPipeline",destroy_pipeline);
    FN("vkCreateDescriptorPool",create_pool);FN("vkDestroyDescriptorPool",destroy_pool);FN("vkAllocateDescriptorSets",allocate_set);FN("vkUpdateDescriptorSets",update_sets);FN("vkCmdPipelineBarrier",barrier);FN("vkCmdCopyBuffer",copy);FN("vkCmdBindPipeline",bind_pipeline);FN("vkCmdBindDescriptorSets",bind_sets);FN("vkCmdDispatch",dispatch);
#undef FN
    throw std::runtime_error(std::string("unexpected Vulkan call: ")+name);
}
struct Input {std::shared_ptr<evrm::NativeSkinAsset> source;evrm::NativeSkinPose pose;};
Input input(std::size_t count=3){vrm::Avatar avatar;vrm::Primitive primitive;primitive.skinned=true;primitive.indices={0,1,2};for(std::size_t i=0;i<count;++i){vrm::Vertex v;v.position={float(i)*.01f,0,0};v.normal={0,1,0};v.joints={0,0,0,0};v.weights={1,0,0,0};primitive.vertices.push_back(v);}avatar.meshes.push_back({{primitive}});avatar.vertex_count=count;avatar.triangle_count=1;avatar.nodes.resize(1);avatar.nodes[0].active=true;avatar.nodes[0].local=avatar.nodes[0].world=identity();avatar.nodes[0].mesh=0;avatar.nodes[0].skin=0;avatar.skins.push_back({{0},{identity()}});vrm::RetargetResult pose;pose.node_world={identity()};pose.palettes.push_back({0,0,{identity()}});auto source=std::make_shared<evrm::NativeSkinAsset>(evrm::make_native_skin_asset(avatar,pose));check(bool(*source),"synthetic source rejected");return {source,evrm::make_native_skin_pose(source,pose)};}
struct Session {
    Driver state;bool quarantine=false;VkPhysicalDeviceMemoryProperties memory{};VkPhysicalDeviceLimits limits{};std::unique_ptr<evrm::NativeSkinningGpu> gpu;
    VkBuffer prepared{};VkDeviceMemory prepared_memory{};VkCommandBuffer command=handle<VkCommandBuffer>(1);
    explicit Session(const std::filesystem::path& shaders){driver=&state;memory.memoryTypeCount=2;memory.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;memory.memoryTypes[1].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;limits.maxComputeWorkGroupInvocations=64;limits.maxComputeWorkGroupSize[0]=64;limits.maxComputeWorkGroupCount[0]=65535;limits.maxPerStageDescriptorStorageBuffers=limits.maxDescriptorSetStorageBuffers=7;limits.maxPerStageDescriptorUniformBuffers=limits.maxDescriptorSetUniformBuffers=1;limits.maxStorageBufferRange=128*1024*1024;gpu=std::make_unique<evrm::NativeSkinningGpu>(handle<VkDevice>(1),get,memory,limits,shaders,&quarantine);VkBufferCreateInfo b{};b.size=224;b.usage=VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;create_buffer({},&b,nullptr,&prepared);VkMemoryAllocateInfo m{};m.allocationSize=224;m.memoryTypeIndex=1;allocate({},&m,nullptr,&prepared_memory);bind({},prepared,prepared_memory,0);}
    ~Session(){gpu.reset();if(!quarantine){destroy_buffer({},prepared,nullptr);free_memory({},prepared_memory,nullptr);}}
    std::shared_ptr<evrm::NativeSkinGpuAsset> asset(const Input& f){auto a=gpu->asset(f.source);gpu->upload(command,a);gpu->finish_upload(a);return a;}
    std::shared_ptr<evrm::NativeSkinGpuFrame> frame(const Input& f,const std::shared_ptr<evrm::NativeSkinGpuAsset>& a,std::shared_ptr<const evrm::NativeSkinGpuGeometry> previous={}){return gpu->frame(a,f.pose,{prepared,0,224},std::move(previous));}
    auto bindings()const{return state.sets.at(state.last_set);}
};
template<class F>void rejects(F&& action,const char* message){bool rejected=false;try{action();}catch(const std::runtime_error&){rejected=true;}check(rejected,message);}
void warm_reuse(const std::filesystem::path& shaders){Session s(shaders);auto f=input();auto a=s.asset(f);{auto frame=s.frame(f,a);}const auto before=s.state.count;for(unsigned i=0;i<8;++i){auto frame=s.frame(f,a);check(frame->status_after_completion()==0,"reused status was not reset");}check(s.state.count.allocations==before.allocations,"retired frame buffers allocated again");check(s.state.count.pools==before.pools&&s.state.count.sets==before.sets,"retired descriptor pack allocated again");check(s.state.count.writes==before.writes+8,"reused descriptors were not rewritten per frame");}
void mouth_weight_reuse(const std::filesystem::path& shaders){
    Session s(shaders);auto f=input();auto mouth=std::make_shared<vrm::MouthData>();mouth->available=1;
    for(unsigned n=0;n<3;++n){vrm::MouthDeltaRecord record;record.vertex=n;record.position[0][1]=.02f;mouth->records.push_back(record);}
    mouth->ranges.push_back({0,0,0,3});f.source->mouth=mouth;f.source->mouth_lookup={0,1,2};f.pose.mouth_safe=true;
    auto asset=s.asset(f);check(asset->mouth_available(),"resource fixture mouth not available");
    {auto frame=s.gpu->frame(asset,f.pose,{s.prepared,0,224},{},{1,0,0,0,0});}
    const auto before=s.state.count;
    for(unsigned n=0;n<16;++n){const float strength=n%3?float(n%5+1)*.2f:0.f;
        auto frame=s.gpu->frame(asset,f.pose,{s.prepared,0,224},{},{strength,0,0,0,0});const auto binding=s.bindings()[7];
        check(binding.range==64,"changing mouth control size");const auto& memory=s.state.memories.at(s.state.buffers.at(binding.buffer).memory);
        float stored=0;std::uint32_t active=0;std::memcpy(&active,memory.mapped.data()+28,4);std::memcpy(&stored,memory.mapped.data()+32,4);
        check(active==std::uint32_t(strength>0)&&stored==strength,"warm control retained previous mouth weights");}
    check(s.state.count.allocations==before.allocations&&s.state.count.buffers==before.buffers&&s.state.count.pools==before.pools&&s.state.count.sets==before.sets,
        "changing mouth weights created native resources after warmup");
}
void retained_leases(const std::filesystem::path& shaders){Session s(shaders);auto f=input();auto a=s.asset(f);auto first=s.frame(f,a);const auto first_bindings=s.bindings();const auto first_set=s.state.last_set;auto geometry=first->geometry();auto second=s.frame(f,a,geometry);const auto second_bindings=s.bindings();check(first_bindings[4].buffer!=second_bindings[4].buffer,"live output reused");check(first_bindings[5].buffer!=second_bindings[5].buffer&&first_bindings[1].buffer!=second_bindings[1].buffer,"live frame scratch reused");check(s.state.last_set!=first_set,"descriptor pack reused while its original frame was retained");first.reset();{auto third=s.frame(f,a,geometry);const auto third_bindings=s.bindings();check(third_bindings[1].buffer==first_bindings[1].buffer,"retired scratch was not reused");check(third_bindings[4].buffer!=first_bindings[4].buffer,"history output recycled while retained");check(third_bindings[3].buffer==first_bindings[4].buffer,"retained history descriptor changed");}second.reset();geometry.reset();auto fourth=s.frame(f,a);check(s.bindings()[4].buffer==first_bindings[4].buffer,"output not reusable after final geometry lease");}
void reset_and_keys(const std::filesystem::path& shaders){Session s(shaders);auto f=input(16);auto a=s.asset(f);VkDescriptorSet old_set{};VkBuffer old_output{};{auto frame=s.frame(f,a);old_set=s.state.last_set;old_output=s.bindings()[4].buffer;for(unsigned binding:{3u,5u,7u}){const auto buffer=s.bindings()[binding].buffer;auto& memory=s.state.memories.at(s.state.buffers.at(buffer).memory);if(!memory.mapped.empty())std::fill(memory.mapped.begin(),memory.mapped.end(),0xFF);}}auto changed=f;changed.pose.matrices.resize(11,identity());changed.pose.batches[0].count=11;auto frame=s.frame(changed,a);const auto b=s.bindings();check(s.state.last_set==old_set,"retired descriptor set was not reused");check(b[1].range==704&&b[4].range==704&&b[1].buffer!=b[4].buffer,"same-size different-usage resources aliased");check(b[4].buffer==old_output,"exact output key was not reused");check(frame->status_after_completion()==0,"old rejection status survived reuse");s.gpu->record(s.command,frame);rejects([&]{s.gpu->record(s.command,frame);},"fresh frame recording guard lost");}
void bounded_caches(const std::filesystem::path& shaders){
    {Session s(shaders);auto f=input();auto a=s.asset(f);const auto base=s.state.buffers.size();std::vector<std::shared_ptr<evrm::NativeSkinGpuFrame>> frames;for(unsigned i=0;i<40;++i)frames.push_back(s.frame(f,a));check(s.state.buffers.size()==base+280,"live cache limit discarded live buffers");frames.clear();check(s.state.buffers.size()<=base+128,"buffer cache exceeded entry bound");check(s.state.pools.size()<=32,"descriptor cache exceeded pack bound");const auto before=s.state.count;{auto frame=s.frame(f,a);}check(s.state.count.allocations==before.allocations,"full busy cache prevented subsequent retired reuse");}
    {Session s(shaders);auto f=input();auto a=s.asset(f);const auto base=s.state.allocated;s.state.floor=32ull*1024*1024;{auto frame=s.frame(f,a);}check(s.state.allocated<=base+128ull*1024*1024,"buffer cache exceeded actual reserved byte bound");check(s.state.allocated==base+128ull*1024*1024,"busy cache fallback failed to preserve bounded reusable allocations");const auto before=s.state.count.allocations;{auto frame=s.frame(f,a);}check(s.state.count.allocations==before+3,"uncached fallback did not reuse four bounded cached allocations");}
}
void failure_and_budget(const std::filesystem::path& shaders){Session s(shaders);auto f=input();auto a=s.asset(f);auto held=s.frame(f,a);const auto held_bindings=s.bindings();s.state.fail_memory=true;rejects([&]{s.frame(f,a);},"allocation failure did not reject frame");check(s.state.buffers.count(held_bindings[4].buffer)==1,"failure destroyed retained output");s.state.fail_pool=true;rejects([&]{s.frame(f,a);},"descriptor-pool failure did not reject frame");s.state.fail_set=true;rejects([&]{s.frame(f,a);},"descriptor allocation failure did not reject frame");held.reset();auto geometry=std::shared_ptr<const evrm::NativeSkinGpuGeometry>{};{auto frame=s.frame(f,a);geometry=frame->geometry();}const auto kept=geometry->binding().buffer;s.state.floor=512ull*1024*1024;auto changed=f;changed.pose.matrices.resize(2,identity());changed.pose.batches[0].count=2;const auto frees=s.state.count.frees;rejects([&]{s.frame(changed,a);},"aggregate allocation budget exceeded");check(s.state.count.frees>frees,"idle cached memory was not evicted before budget rejection");check(s.state.buffers.count(kept)==1,"budget eviction destroyed history output");check(s.state.peak<=512ull*1024*1024,"aggregate budget omitted cached/live memory");s.state.floor=0;auto recovered=s.frame(f,a,geometry);check(s.bindings()[3].buffer==kept,"failure lost history resource identity");}
void asset_budget_reclaims_idle(const std::filesystem::path& shaders){Session s(shaders);auto f=input();auto a=s.asset(f);s.state.floor=32ull*1024*1024;{auto frame=s.frame(f,a);}s.state.floor=0;s.state.next_requirement=400ull*1024*1024;const auto frees=s.state.count.frees;auto second=s.asset(f);check(s.state.count.frees>frees,"asset allocation failed to reclaim idle cached memory");check(s.state.peak<=512ull*1024*1024,"asset allocation exceeded aggregate budget");}
void quarantine_and_owner(const std::filesystem::path& shaders){
    {Session s(shaders);auto f=input();auto a=s.asset(f);auto frame=s.frame(f,a);auto geometry=frame->geometry();s.gpu.reset();check(s.state.count.destroy_pipelines==0,"wrapper destruction released leased pipeline");frame.reset();a.reset();check(s.state.count.destroy_pipelines==0,"geometry lease lost pipeline owner");geometry.reset();check(s.state.count.destroy_pipelines==2&&s.state.pools.empty(),"resource cache introduced owner cycle");}
    {Session s(shaders);auto f=input();auto a=s.asset(f);auto frame=s.frame(f,a);s.quarantine=true;const auto before=s.state.count;rejects([&]{s.frame(f,a);},"quarantine permitted resource acquisition");frame.reset();a.reset();s.gpu.reset();check(s.state.count.frees==before.frees&&s.state.count.destroy_buffers==before.destroy_buffers&&s.state.count.unmaps==before.unmaps&&s.state.count.destroy_pools==before.destroy_pools&&s.state.count.destroy_pipelines==before.destroy_pipelines,"quarantine performed unsafe native destruction");}
}
}
int main(int argc,char** argv){try{check(argc==2,"shader directory required");const std::filesystem::path shaders(argv[1]);unsigned cases=0,failures=0;auto run=[&](const char* name,auto test){try{test(shaders);++cases;std::cout<<"PASS "<<name<<'\n';}catch(const std::exception& e){++failures;std::cerr<<"FAIL "<<name<<": "<<e.what()<<'\n';}};run("warm retired allocations",warm_reuse);run("changing mouth weights reuse resources",mouth_weight_reuse);run("retained frame and geometry leases",retained_leases);run("mapped reset and exact resource keys",reset_and_keys);run("bounded caches and uncached fallback",bounded_caches);run("allocation faults and idle-only budget eviction",failure_and_budget);run("asset upload reclaims idle cache budget",asset_budget_reclaims_idle);run("quarantine and leased pipeline owner",quarantine_and_owner);std::cout<<(failures?"FAIL":"PASS")<<" NativeSkinningGPUReuse cases="<<cases<<" failures="<<failures<<"; deterministic Vulkan ownership boundary\n";return failures?1:0;}catch(const std::exception& e){std::cerr<<"FAIL NativeSkinningGPUReuse: "<<e.what()<<'\n';return 1;}}
