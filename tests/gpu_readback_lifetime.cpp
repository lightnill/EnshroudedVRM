#include "../src/gpu_probe.hpp"
#include "../src/standalone_startup.hpp"
#include <cstring>
#include <iostream>
#include <map>
#include <vector>
using namespace evrm;
namespace evrm { const RuntimeStartupContext& runtime_startup_context() noexcept { static RuntimeStartupContext c=[] {RuntimeStartupContext x;x.status=RuntimeStartupStatus::Ready;return x;}();return c;} }
template<class T> T h(uintptr_t x){return reinterpret_cast<T>(x);}
static uintptr_t next_id=1000; static std::vector<unsigned char> staging(65536); static bool fence_ready=true;
static void VKAPI_CALL memory(VkPhysicalDevice,VkPhysicalDeviceMemoryProperties* p){p->memoryTypeCount=1;p->memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;}
static void VKAPI_CALL families(VkPhysicalDevice,uint32_t* n,VkQueueFamilyProperties* p){*n=1;if(p)p[0].queueFlags=VK_QUEUE_GRAPHICS_BIT;}
static VkResult VKAPI_CALL create_buffer(VkDevice,const VkBufferCreateInfo*,const VkAllocationCallbacks*,VkBuffer* x){*x=h<VkBuffer>(++next_id);return VK_SUCCESS;}
static void VKAPI_CALL requirements(VkDevice,VkBuffer,VkMemoryRequirements* p){p->size=65536;p->memoryTypeBits=1;}
static VkResult VKAPI_CALL allocate(VkDevice,const VkMemoryAllocateInfo*,const VkAllocationCallbacks*,VkDeviceMemory* x){*x=h<VkDeviceMemory>(++next_id);return VK_SUCCESS;}
static VkResult VKAPI_CALL bind(VkDevice,VkBuffer,VkDeviceMemory,VkDeviceSize){return VK_SUCCESS;}
static VkResult VKAPI_CALL mapmem(VkDevice,VkDeviceMemory,VkDeviceSize,VkDeviceSize,VkMemoryMapFlags,void** x){*x=staging.data();return VK_SUCCESS;}
static VkResult VKAPI_CALL pool(VkDevice,const VkCommandPoolCreateInfo*,const VkAllocationCallbacks*,VkCommandPool* x){*x=h<VkCommandPool>(++next_id);return VK_SUCCESS;}
static VkResult VKAPI_CALL commands(VkDevice,const VkCommandBufferAllocateInfo*,VkCommandBuffer* x){*x=h<VkCommandBuffer>(++next_id);return VK_SUCCESS;}
static VkResult VKAPI_CALL fence(VkDevice,const VkFenceCreateInfo*,const VkAllocationCallbacks*,VkFence* x){*x=h<VkFence>(++next_id);return VK_SUCCESS;}
static VkResult VKAPI_CALL reset_fences(VkDevice,uint32_t,const VkFence*){return VK_SUCCESS;}
static VkResult VKAPI_CALL reset_pool(VkDevice,VkCommandPool,VkCommandPoolResetFlags){return VK_SUCCESS;}
static VkResult VKAPI_CALL begin(VkCommandBuffer,const VkCommandBufferBeginInfo*){return VK_SUCCESS;}
static VkResult VKAPI_CALL end(VkCommandBuffer){return VK_SUCCESS;}
static void VKAPI_CALL barrier(VkCommandBuffer,VkPipelineStageFlags,VkPipelineStageFlags,VkDependencyFlags,uint32_t,const VkMemoryBarrier*,uint32_t,const VkBufferMemoryBarrier*,uint32_t,const VkImageMemoryBarrier*){}
static void VKAPI_CALL copy(VkCommandBuffer,VkBuffer,VkBuffer,uint32_t count,const VkBufferCopy* regions){for(uint32_t i=0;i<count;++i)std::memset(staging.data()+regions[i].dstOffset,0x5a,static_cast<size_t>(regions[i].size));}
static VkResult VKAPI_CALL submit(VkQueue,uint32_t,const VkSubmitInfo*,VkFence){return VK_SUCCESS;}
static VkResult VKAPI_CALL status(VkDevice,VkFence){return fence_ready?VK_SUCCESS:VK_NOT_READY;}
static VkResult VKAPI_CALL wait(VkDevice,uint32_t,const VkFence*,VkBool32,uint64_t){return VK_SUCCESS;}
static VkResult VKAPI_CALL idle(VkQueue){return VK_SUCCESS;}
static void VKAPI_CALL unmap(VkDevice,VkDeviceMemory){}
static void VKAPI_CALL destroy_fence(VkDevice,VkFence,const VkAllocationCallbacks*){}
static void VKAPI_CALL destroy_pool(VkDevice,VkCommandPool,const VkAllocationCallbacks*){}
static void VKAPI_CALL destroy_buffer(VkDevice,VkBuffer,const VkAllocationCallbacks*){}
static void VKAPI_CALL free_mem(VkDevice,VkDeviceMemory,const VkAllocationCallbacks*){}
static PFN_vkVoidFunction VKAPI_CALL gdpa(VkDevice,const char* n){
#define P(name,fn) if(!std::strcmp(n,name))return reinterpret_cast<PFN_vkVoidFunction>(fn)
 P("vkCreateBuffer",create_buffer);P("vkGetBufferMemoryRequirements",requirements);P("vkAllocateMemory",allocate);P("vkBindBufferMemory",bind);P("vkMapMemory",mapmem);P("vkCreateCommandPool",pool);P("vkAllocateCommandBuffers",commands);P("vkCreateFence",fence);P("vkResetFences",reset_fences);P("vkResetCommandPool",reset_pool);P("vkBeginCommandBuffer",begin);P("vkEndCommandBuffer",end);P("vkCmdPipelineBarrier",barrier);P("vkCmdCopyBuffer",copy);P("vkQueueSubmit",submit);P("vkGetFenceStatus",status);P("vkWaitForFences",wait);P("vkQueueWaitIdle",idle);P("vkUnmapMemory",unmap);P("vkDestroyFence",destroy_fence);P("vkDestroyCommandPool",destroy_pool);P("vkDestroyBuffer",destroy_buffer);P("vkFreeMemory",free_mem);return nullptr;
#undef P
}
static PFN_vkVoidFunction VKAPI_CALL gipa(VkInstance,const char* n){return !std::strcmp(n,"vkGetPhysicalDeviceMemoryProperties")?reinterpret_cast<PFN_vkVoidFunction>(memory):reinterpret_cast<PFN_vkVoidFunction>(families);}
struct Fixture {
 GpuProbe p{h<VkInstance>(1),h<VkPhysicalDevice>(2),h<VkDevice>(3),gipa,gdpa,nullptr}; VkQueue q=h<VkQueue>(4);VkCommandBuffer cmd=h<VkCommandBuffer>(5);VkDescriptorSet sets[3]{h<VkDescriptorSet>(10),h<VkDescriptorSet>(11),h<VkDescriptorSet>(12)};VkDescriptorSetLayout layouts[3]{h<VkDescriptorSetLayout>(20),h<VkDescriptorSetLayout>(21),h<VkDescriptorSetLayout>(22)}; VkBuffer buf=h<VkBuffer>(30);VkDeviceMemory mem=h<VkDeviceMemory>(31);VkPipelineLayout pl=h<VkPipelineLayout>(40);
 Fixture(){
  p.queue_observed(q,0);VkBufferCreateInfo bi{};bi.size=4096;bi.usage=VK_BUFFER_USAGE_TRANSFER_SRC_BIT|VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;p.buffer_created(buf,bi);p.buffer_bound(buf,mem,true);
  std::vector<VkDescriptorSetLayoutBinding> b[3];for(unsigned i=0;i<8;++i){VkDescriptorSetLayoutBinding x{};x.binding=i;x.descriptorCount=1;x.descriptorType=(i==0||i==7)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;b[0].push_back(x);}VkDescriptorSetLayoutBinding c{};c.binding=16;c.descriptorCount=1;c.descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;b[1].push_back(c);for(unsigned i=0;i<11;++i){VkDescriptorSetLayoutBinding x{};x.binding=i;x.descriptorCount=1;x.descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;b[2].push_back(x);}
  for(int i=0;i<3;++i){VkDescriptorSetLayoutCreateInfo li{};li.bindingCount=static_cast<uint32_t>(b[i].size());li.pBindings=b[i].data();p.layout_created(layouts[i],li);}VkDescriptorSetAllocateInfo ai{};ai.descriptorSetCount=3;ai.pSetLayouts=layouts;p.sets_allocated(ai,sets);
  for(int i=0;i<3;++i)for(auto& x:b[i])write(i,x.binding,x.descriptorType);
  VkCommandBufferAllocateInfo ca{};ca.commandPool=h<VkCommandPool>(50);ca.commandBufferCount=1;p.commands_allocated(ca,&cmd);p.command_begin(cmd);p.sets_bound(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pl,0,3,sets,0,nullptr);p.pipeline_bound(cmd,true,pl);p.draw_indexed_indirect(cmd,buf,0,1,sizeof(VkDrawIndexedIndirectCommand));p.configure_async(q,{{"instances",128}});
 }
 void write(int set,unsigned binding,VkDescriptorType type=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER){VkDescriptorBufferInfo info{buf,0,128};VkWriteDescriptorSet w{};w.dstSet=sets[set];w.dstBinding=binding;w.descriptorCount=1;w.descriptorType=type;w.pBufferInfo=&info;p.descriptors_written(1,&w);}
 void run(){VkSubmitInfo s{};s.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;s.commandBufferCount=1;s.pCommandBuffers=&cmd;if(p.submit_async(q,1,&s,VK_NULL_HANDLE,submit,7)!=VK_SUCCESS)throw 1;}
 // NativeState and GpuProbe use independent generation counters. Their source
 // leases must remain separate, never compared as if they shared a counter.
 NativePreviewContext subject_context(){return native_preview_subjects().configure({{1,2,3,4,5,6,7,8,9,10},123,3,30,77,456,0});}
 NativePreviewStamp subject(const NativePreviewContext& c,NativePreviewSubject kind){auto& core=native_preview_subjects();auto t=core.begin_request(c.epoch);core.finish_request(t,kind);return core.copied(core.copy_proposal(c.epoch));}
 void record_subject(const NativePreviewStamp& stamp,bool reset=true){
  native_preview_subject_thread().working=stamp;if(reset){p.command_begin(cmd);p.sets_bound(cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,pl,0,3,sets,0,nullptr);p.pipeline_bound(cmd,true,pl);}
  p.draw_indexed_indirect(cmd,buf,0,1,sizeof(VkDrawIndexedIndirectCommand));
 }
};
int main(){try {
 {Fixture f;const auto c=f.subject_context();const auto local=f.subject(c,NativePreviewSubject::local);f.record_subject(local);if(!f.p.capture_inline(f.cmd,0))return 20;f.run();
  f.subject(c,NativePreviewSubject::other);const auto captured=f.p.poll_async(f.q);
  if(!same_native_preview_stamp(captured.preview_subject,local)){std::cerr<<"FAIL actual completed inline copy lost or relabeled its local subject\n";return 21;}}
 {Fixture f;const auto c=f.subject_context();const auto other=f.subject(c,NativePreviewSubject::other);f.record_subject(other);if(!f.p.capture_inline(f.cmd,0))return 22;f.run();
  f.subject(c,NativePreviewSubject::local);const auto captured=f.p.poll_async(f.q);
  if(!same_native_preview_stamp(captured.preview_subject,other)||captured.preview_subject.local()){std::cerr<<"FAIL later local publication relabeled captured survivor\n";return 23;}}
 {Fixture f;const auto c=f.subject_context();f.record_subject(f.subject(c,NativePreviewSubject::local));f.record_subject(f.subject(c,NativePreviewSubject::other),false);
  if(!f.p.capture_inline(f.cmd,0))return 24;
  f.run();const auto captured=f.p.poll_async(f.q);
  if(captured.preview_subject.request){std::cerr<<"FAIL descriptor-preserving local/other draws retained single-subject authority\n";return 25;}}
 {Fixture f;const auto c=f.subject_context();f.record_subject(f.subject(c,NativePreviewSubject::other));f.record_subject(f.subject(c,NativePreviewSubject::local),false);
  if(!f.p.capture_inline(f.cmd,0))return 26;
  f.run();if(f.p.poll_async(f.q).preview_subject.request){std::cerr<<"FAIL other/local draw order inherited latest tag\n";return 27;}}
 {Fixture f;const auto c=f.subject_context();f.record_subject(f.subject(c,NativePreviewSubject::local));native_preview_subject_thread().working={};
  if(!f.p.capture_inline(f.cmd,0))return 28;
  f.run();if(f.p.poll_async(f.q).preview_subject.request){std::cerr<<"FAIL copy outside matching original task retained preview authority\n";return 29;}}
 native_preview_subject_thread()={};native_preview_subjects().retire_owner(123);
 {Fixture f;const auto c=f.subject_context();const auto old=f.subject(c,NativePreviewSubject::local);f.record_subject(old);if(!f.p.capture_inline(f.cmd,0))return 30;f.run();
  native_preview_subjects().begin_lifetime();native_preview_subjects().end_lifetime();const auto next=f.subject_context();const auto captured=f.p.poll_async(f.q);
  if(!same_native_preview_stamp(captured.preview_subject,old)||native_preview_subjects().current(captured.preview_subject)||next.epoch==c.epoch){std::cerr<<"FAIL delayed completion inherited a reconstructed same-address world lifetime\n";return 31;}}
 native_preview_subject_thread()={};native_preview_subjects().retire_owner(123);
 {Fixture f;f.run();f.write(2,4);auto x=f.p.poll_async(f.q);if(x.buffers.count("instances")!=1||x.buffers.at("instances").bytes!=std::vector<unsigned char>(128,0x5a)){std::cerr<<"FAIL post-submit descriptor churn lost snapshot\n";return 1;}}
 {Fixture f;if(!f.p.capture_inline(f.cmd,0)){std::cerr<<"FAIL inline setup\n";return 2;}f.write(2,4);f.run();if(!f.p.poll_async(f.q).buffers.empty()){std::cerr<<"FAIL changed before inline submit published\n";return 3;}}
 {Fixture f;if(!f.p.capture_inline(f.cmd,0))return 9;f.run();f.write(2,4);if(f.p.poll_async(f.q).buffers.empty()){std::cerr<<"FAIL inline post-submit churn lost snapshot\n";return 10;}}
 {Fixture f;if(!f.p.capture_inline(f.cmd,0))return 11;f.p.buffer_bound(f.buf,h<VkDeviceMemory>(999),true);f.run();if(!f.p.poll_async(f.q).buffers.empty()){std::cerr<<"FAIL rebound before inline submit published\n";return 12;}}
 {Fixture f;if(!f.p.capture_inline(f.cmd,0))return 13;f.p.buffer_destroyed(f.buf);f.run();if(!f.p.poll_async(f.q).buffers.empty()){std::cerr<<"FAIL retired before inline submit published\n";return 14;}}
 {Fixture f;if(!f.p.capture_inline(f.cmd,0))return 15;VkSubmitInfo s{};s.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;s.commandBufferCount=1;s.pCommandBuffers=&f.cmd;f.p.submit_async(f.q,1,&s,VK_NULL_HANDLE,submit,0);if(!f.p.poll_async(f.q).buffers.empty()){std::cerr<<"FAIL zero epoch published\n";return 16;}}
 {Fixture f;if(!f.p.capture_inline(f.cmd,0))return 17;VkSubmitInfo s{};s.sType=VK_STRUCTURE_TYPE_SUBMIT_INFO;s.commandBufferCount=1;s.pCommandBuffers=&f.cmd;VkProtectedSubmitInfo chain{};chain.sType=VK_STRUCTURE_TYPE_PROTECTED_SUBMIT_INFO;s.pNext=&chain;f.p.submit_async(f.q,1,&s,VK_NULL_HANDLE,submit,7);if(!f.p.poll_async(f.q).buffers.empty()){std::cerr<<"FAIL unsupported submit published\n";return 18;}}
 {Fixture f;f.run();f.p.buffer_bound(f.buf,h<VkDeviceMemory>(999),true);if(!f.p.poll_async(f.q).buffers.empty()){std::cerr<<"FAIL rebound buffer published\n";return 4;}}
 {Fixture f;f.run();f.p.buffer_destroyed(f.buf);if(!f.p.poll_async(f.q).buffers.empty()){std::cerr<<"FAIL retired buffer published\n";return 5;}}
 {Fixture f;fence_ready=false;f.run();if(!f.p.poll_async(f.q).buffers.empty()){std::cerr<<"FAIL incomplete copy published\n";return 6;}fence_ready=true;if(f.p.poll_async(f.q).buffers.empty()){std::cerr<<"FAIL completed copy missing\n";return 7;}}
 std::cout<<"PASS descriptor lifetime, inline boundary, resource lifetime, fence completion\n";return 0;
 }catch(...){std::cerr<<"FAIL unexpected exception\n";return 8;}}
