#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <exception>
#include "../src/native_avatar_assets.hpp"
#include "../src/native_surface_material.hpp"
#include <map>
#include <vector>
#include <cstring>
#include <stdexcept>
#include <iostream>
#include "native_player_fake_vulkan.hpp"
using namespace evrm;
namespace {
std::map<VkImage,VkFormat> formats;
std::map<VkImageView,VkFormat> views;
std::map<VkSampler,VkSamplerCreateInfo> samplers;
bool fail_image=false;
bool fail_completion=false;
unsigned teardown_faults=0;
unsigned image_attempts=0;
template<class F>void safe_teardown(F action)noexcept{
    try{action();}catch(...){++teardown_faults;std::fputs("FAIL fake Vulkan teardown exception\n",stderr);}
}
void VKAPI_CALL destroy_buffer(VkDevice d,VkBuffer b,const VkAllocationCallbacks* a)noexcept{safe_teardown([&]{fake_player_gpu::destroy_buffer(d,b,a);});}
void VKAPI_CALL free_memory(VkDevice d,VkDeviceMemory m,const VkAllocationCallbacks* a)noexcept{safe_teardown([&]{fake_player_gpu::free(d,m,a);});}
void VKAPI_CALL destroy_image(VkDevice d,VkImage i,const VkAllocationCallbacks* a)noexcept{safe_teardown([&]{fake_player_gpu::destroy_image(d,i,a);});}
template<class T>void VKAPI_CALL destroy(VkDevice d,T object,const VkAllocationCallbacks* a)noexcept{safe_teardown([&]{fake_player_gpu::destroy(d,object,a);});}
VkResult VKAPI_CALL image(VkDevice d,const VkImageCreateInfo* i,const VkAllocationCallbacks* a,VkImage* o){
    ++image_attempts;
    if(fail_image)return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    const auto r=fake_player_gpu::image(d,i,a,o);if(r==VK_SUCCESS)formats[*o]=i->format;return r;
}
VkResult VKAPI_CALL view(VkDevice d,const VkImageViewCreateInfo* i,const VkAllocationCallbacks* a,VkImageView* o){
    const auto r=fake_player_gpu::image_view(d,i,a,o);if(r==VK_SUCCESS){if(formats.at(i->image)!=i->format)throw std::runtime_error("image view colour interpretation mismatch");views[*o]=i->format;}return r;
}
VkResult VKAPI_CALL sampler(VkDevice d,const VkSamplerCreateInfo* i,const VkAllocationCallbacks* a,VkSampler* o){
    const auto r=fake_player_gpu::sampler(d,i,a,o);if(r==VK_SUCCESS)samplers[*o]=*i;return r;
}
VkResult VKAPI_CALL completion(VkDevice d,VkFence f){return fail_completion?VK_ERROR_UNKNOWN:fake_player_gpu::status(d,f);}
PFN_vkVoidFunction VKAPI_CALL asset_get(VkDevice d,const char* name){
    if(!std::strcmp(name,"vkDestroyBuffer"))return reinterpret_cast<PFN_vkVoidFunction>(destroy_buffer);
    if(!std::strcmp(name,"vkFreeMemory"))return reinterpret_cast<PFN_vkVoidFunction>(free_memory);
    if(!std::strcmp(name,"vkDestroyImage"))return reinterpret_cast<PFN_vkVoidFunction>(destroy_image);
#define DESTROY_ENTRY(n,t) if(!std::strcmp(name,#n))return reinterpret_cast<PFN_vkVoidFunction>(destroy<t>)
    DESTROY_ENTRY(vkDestroyImageView,VkImageView);DESTROY_ENTRY(vkDestroySampler,VkSampler);
    DESTROY_ENTRY(vkDestroyCommandPool,VkCommandPool);DESTROY_ENTRY(vkDestroyFence,VkFence);
    DESTROY_ENTRY(vkDestroyDescriptorSetLayout,VkDescriptorSetLayout);DESTROY_ENTRY(vkDestroyPipelineLayout,VkPipelineLayout);
    DESTROY_ENTRY(vkDestroyShaderModule,VkShaderModule);DESTROY_ENTRY(vkDestroyPipeline,VkPipeline);DESTROY_ENTRY(vkDestroyDescriptorPool,VkDescriptorPool);
#undef DESTROY_ENTRY
    if(!std::strcmp(name,"vkCreateImage"))return reinterpret_cast<PFN_vkVoidFunction>(image);
    if(!std::strcmp(name,"vkCreateImageView"))return reinterpret_cast<PFN_vkVoidFunction>(view);
    if(!std::strcmp(name,"vkCreateSampler"))return reinterpret_cast<PFN_vkVoidFunction>(sampler);
    if(!std::strcmp(name,"vkGetFenceStatus"))return reinterpret_cast<PFN_vkVoidFunction>(completion);
    return fake_player_gpu::get(d,name);
}
void need(bool v,const char* why){if(!v)throw std::runtime_error(why);}
std::shared_ptr<vrm::Avatar> model(){
    auto a=std::make_shared<vrm::Avatar>();
    for(unsigned i=0;i<4;++i){a->images.push_back({1,1,{128,128,255,255}});vrm::Texture t;t.image=i;a->textures.push_back(t);}
    vrm::Material m;m.base_texture=0;auto s=std::make_shared<vrm::MaterialSurface>();s->kind=vrm::SurfaceKind::MToon1;
    s->normal.texture=1;s->matcap.texture=2;s->rim.texture=3;m.surface=s;a->materials.push_back(m);return a;
}
VkPhysicalDeviceMemoryProperties memory_properties(){VkPhysicalDeviceMemoryProperties p{};p.memoryTypeCount=1;p.memoryTypes[0].propertyFlags=VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;return p;}
std::shared_ptr<NativeSkinningGpu> gpu(const std::filesystem::path& folder,bool* uncertain){
    VkPhysicalDeviceLimits limits{};limits.maxComputeWorkGroupInvocations=64;limits.maxComputeWorkGroupSize[0]=64;
    limits.maxPerStageDescriptorStorageBuffers=limits.maxDescriptorSetStorageBuffers=16;
    limits.maxPerStageDescriptorUniformBuffers=limits.maxDescriptorSetUniformBuffers=1;limits.maxStorageBufferRange=1024*1024;
    return std::make_shared<NativeSkinningGpu>(fake_player_gpu::handle<VkDevice>(1),asset_get,memory_properties(),limits,folder,uncertain);
}
std::shared_ptr<const NativeSkinAsset> skin_source(){
    auto source=std::make_shared<NativeSkinAsset>();auto layout=std::make_shared<NativeAvatarPose>();layout->indices={0};
    source->layout=layout;source->vertices.resize(1);source->surface_vertices.resize(1);return source;
}
int failure_ownership_case(const std::string& which,const std::filesystem::path& folder){
    bool uncertain=false;auto skinning=gpu(folder,&uncertain);
    NativeAvatarAssetStager stager(fake_player_gpu::handle<VkDevice>(1),asset_get,nullptr,memory_properties(),8192,skinning,&uncertain);
    const auto queue=fake_player_gpu::handle<VkQueue>(2);
    auto old=model();auto old_skin=skin_source();const auto enhanced=make_native_surface_selection(*old);
    const std::weak_ptr<const vrm::Avatar> old_avatar_weak=old;
    const std::weak_ptr<const NativeSkinAsset> old_skin_weak=old_skin;
    fail_image=true;need(stager.update(queue,0,7,old,old_skin,enhanced)==NativeAvatarUploadState::Failed,"lifetime fixture did not fail upload");fail_image=false;
    old.reset();old_skin.reset();
    need(!old_avatar_weak.expired()&&!old_skin_weak.expired(),"current exact failure identity was discarded prematurely");
    need(stager.update(queue,0,7,old_avatar_weak.lock(),old_skin_weak.lock(),enhanced)==NativeAvatarUploadState::Failed,"current failed request busy retried");
    if(which=="failure-life-success"){
        auto next=model();NativeSurfaceSelection basic;basic.features.resize(next->materials.size()+1);
        need(stager.update(queue,0,8,next,{},basic)==NativeAvatarUploadState::Pending,"replacement lifetime upload did not start");
        need(stager.update(queue,0,8,next,{},basic)==NativeAvatarUploadState::Ready,"replacement lifetime upload not ready");
        need(bool(stager.take_ready(8,next,basic)),"replacement lifetime asset was not taken");
    }else if(which=="failure-life-idle"){
        need(stager.update(queue,0,0,{}, {},NativeSurfaceSelection{})==NativeAvatarUploadState::Idle,"empty request did not become idle");
    }else if(which=="failure-life-device-idle")stager.device_idle_complete();
    else if(which=="failure-life-cancel")stager.cancel();
    else throw std::runtime_error("unknown failure lifetime case");
    need(old_avatar_weak.expired(),"obsolete failure latch retained decoded avatar");
    need(old_skin_weak.expired(),"obsolete failure latch retained CPU skin arrays");
    need(!uncertain,"known upload failure quarantined device");
    std::cout<<"PASS actual asset stager "<<which<<'\n';return 0;
}
}
int run_case(int argc,char** argv){
    need(argc==3,"case and fake shader folder required");using namespace fake_player_gpu;const std::string which=argv[1];
    if(which.rfind("failure-life-",0)==0)return failure_ownership_case(which,argv[2]);
    auto a=model();
    if(which=="vector-key")a->materials.push_back(a->materials[0]);
    if(which=="dedup"){
        auto surface=std::make_shared<vrm::MaterialSurface>(*a->materials[0].surface);
        surface->normal.texture=surface->matcap.texture=surface->rim.texture=0;a->materials[0].surface=surface;
        a->textures[0].mag_filter=9728;a->textures[0].min_filter=9986;a->textures[0].wrap_s=33648;a->textures[0].wrap_t=33071;
    }
    AvatarAppearance style;style.whole=AvatarShaderPreset::Fast;const auto basic=make_native_surface_selection(*a,&style);
    style.whole=AvatarShaderPreset::Enhanced;const auto enhanced=make_native_surface_selection(*a,&style);
    bool uncertain=false;std::shared_ptr<NativeSkinningGpu> skinning;std::shared_ptr<const NativeSkinAsset> skin;
    if(which=="skin-identity"||which=="skin-owner"||which=="skin-switch"){skinning=gpu(argv[2],&uncertain);skin=skin_source();}
    NativeAvatarAssetStager stager(handle<VkDevice>(1),asset_get,nullptr,memory_properties(),8192,skinning,&uncertain);auto queue=handle<VkQueue>(2);
    need(stager.update(queue,0,7,a,skin,basic)==NativeAvatarUploadState::Pending,"basic start failed");
    need(stager.update(queue,0,7,a,skin,basic)==NativeAvatarUploadState::Ready,"basic completion failed");
    auto active=stager.take_ready(7,a,basic);need(bool(active)&&images_created==2,"basic allocated extra map textures");
    const auto base=active->material(0).imageView;
    if(which=="dimensions"){
        auto oversized=model();oversized->images[0]={8193,1,std::vector<std::uint8_t>(32772,255)};
        const auto before=image_attempts;
        need(stager.update(queue,0,8,oversized,{},basic)==NativeAvatarUploadState::Failed,"device image dimensions were not checked before upload");
        need(image_attempts==before,"oversized selected image reached vkCreateImage");
        need(stager.error().find("device limit")!=std::string::npos&&active->material(0).imageView==base&&!uncertain,
             "dimension rejection was not truthful or damaged the active avatar");
        auto optional=model();optional->images[1]={8193,1,std::vector<std::uint8_t>(32772,255)};
        need(stager.update(queue,0,9,optional,{},basic)==NativeAvatarUploadState::Pending,
             "unused optional image was incorrectly checked against the upload limit");
        need(stager.update(queue,0,9,optional,{},basic)==NativeAvatarUploadState::Ready,
             "Fast with an unused oversized map failed to finish");
        auto fast=stager.take_ready(9,optional,basic);need(bool(fast),"Fast fallback asset unavailable");
        const auto optional_before=image_attempts;
        need(stager.update(queue,0,9,optional,{},enhanced,fast)==NativeAvatarUploadState::Failed&&image_attempts==optional_before,
             "oversized enabled optional image reached vkCreateImage");
        bool invalid_cap=false;try{NativeAvatarAssetStager invalid(handle<VkDevice>(1),asset_get,nullptr,memory_properties(),0,{},&uncertain);}
        catch(const std::invalid_argument&){invalid_cap=true;}
        need(invalid_cap,"zero device image dimension cap silently bypassed validation");
    }else if(which=="selection"){
        pending=true;need(stager.update(queue,0,7,a,{},enhanced,active)==NativeAvatarUploadState::Pending,"same-generation enhancement ignored");
        need(active->material(0).imageView==base&&!active->surface_selection().any(),"pending enhancement modified active assets");
        need(!stager.ready(7,a,enhanced),"pending map bytes exposed");pending=false;
        need(stager.update(queue,0,7,a,{},enhanced,active)==NativeAvatarUploadState::Ready,"enhancement completion failed");
        auto rich=stager.take_ready(7,a,enhanced);need(bool(rich)&&rich->material(0).imageView==base,"base image was duplicated on enhancement");
        need(images_created==5,"optional maps duplicated or missing");
        need(views.at(rich->surface_material(0,NativeSurfaceTexture::Normal).imageView)==VK_FORMAT_R8G8B8A8_UNORM,"normal texture gamma decoded");
        need(views.at(rich->surface_material(0,NativeSurfaceTexture::Rim).imageView)==VK_FORMAT_R8G8B8A8_SRGB,"rim mask colour decode lost");
        auto recorded=rich;need(stager.update(queue,0,7,a,{},basic,rich)==NativeAvatarUploadState::Pending,"Fast transition failed");
        need(stager.update(queue,0,7,a,{},basic,rich)==NativeAvatarUploadState::Ready,"Fast transition not ready");
        auto fast=stager.take_ready(7,a,basic);need(bool(fast)&&!fast->surface_selection().any()&&images_created==5,"Fast uploaded or kept optional maps");
        rich.reset();need(images_destroyed==0,"recorded enhanced image lease retired early");recorded.reset();
        need(images_destroyed==3,"unused map textures stayed resident after final lease");
    }else if(which=="cancel"){
        pending=true;need(stager.update(queue,0,7,a,{},enhanced,active)==NativeAvatarUploadState::Pending,"enhancement start failed");
        need(stager.update(queue,0,7,a,{},basic,active)==NativeAvatarUploadState::Pending,"in-flight cancellation destroyed pending work");
        need(images_destroyed==0,"pending resources freed before fence");pending=false;
        need(stager.update(queue,0,7,a,{},basic,active)==NativeAvatarUploadState::Pending,"changed selection did not stage after cancellation");
        need(stager.update(queue,0,7,a,{},basic,active)==NativeAvatarUploadState::Ready,"changed selection not ready");
        need(bool(stager.take_ready(7,a,basic))&&active->material(0).imageView==base,"cancel corrupted active VRM");
    }else if(which=="failure"){
        fail_image=true;need(stager.update(queue,0,7,a,{},enhanced,active)==NativeAvatarUploadState::Failed,"upload failure not surfaced");fail_image=false;
        need(stager.update(queue,0,7,a,{},enhanced,active)==NativeAvatarUploadState::Failed,"failed exact request busy retried");
        auto cap=enhanced;cap.features[0]=surface_matcap|surface_mask;cap.combined=cap.features[0];
        need(stager.update(queue,0,7,a,{},cap,active)==NativeAvatarUploadState::Pending,"changed request inherited old failure");
        need(stager.update(queue,0,7,a,{},cap,active)==NativeAvatarUploadState::Ready&&bool(stager.take_ready(7,a,cap)),"changed request never recovered");
        need(active->material(0).imageView==base&&!uncertain,"material failure poisoned active VRM/device");
    }else if(which=="dedup"){
        need(stager.update(queue,0,7,a,{},enhanced,active)==NativeAvatarUploadState::Pending,"dedup request failed");
        need(stager.update(queue,0,7,a,{},enhanced,active)==NativeAvatarUploadState::Ready,"dedup request not ready");
        auto rich=stager.take_ready(7,a,enhanced);need(bool(rich)&&images_created==3,"compatible roles failed to share one colour image");
        const auto normal=rich->surface_material(0,NativeSurfaceTexture::Normal),cap=rich->surface_material(0,NativeSurfaceTexture::Matcap),rim=rich->surface_material(0,NativeSurfaceTexture::Rim);
        need(normal.imageView!=base&&cap.imageView==base&&rim.imageView==base,"normal and colour interpretations improperly aliased");
        for(const auto binding:{normal,cap,rim}){
            const auto& spec=samplers.at(binding.sampler);
            need(spec.magFilter==VK_FILTER_NEAREST&&spec.minFilter==VK_FILTER_NEAREST&&spec.addressModeU==VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT&&spec.addressModeV==VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE,"role sampler ignored authored filter/wrap");
        }
        need(!rich->surface_vertices().buffer,"CPU fallback unexpectedly allocated static GPU surface stream");
        need(rich->surface_material(99,NativeSurfaceTexture::Normal).imageView==rich->material(99).imageView,"fallback surface binding is not owned white image");
    }else if(which=="malformed"){
        auto bad=enhanced;bad.features.pop_back();
        need(stager.update(queue,0,7,a,{},bad,active)==NativeAvatarUploadState::Failed&&images_created==2,"wrong selection length allocated resources");
        bad=enhanced;bad.features.back()=surface_normal;
        need(stager.update(queue,0,7,a,{},bad,active)==NativeAvatarUploadState::Failed&&images_created==2,"nonzero fallback selection accepted");
        bad=enhanced;bad.combined=0;
        need(stager.update(queue,0,7,a,{},bad,active)==NativeAvatarUploadState::Failed&&images_created==2,"inconsistent combined feature key accepted");
        bad=enhanced;bad.features[0]|=16;bad.combined|=16;
        need(stager.update(queue,0,7,a,{},bad,active)==NativeAvatarUploadState::Failed&&images_created==2,"unsupported feature key accepted");
        need(active->material(0).imageView==base&&!uncertain,"malformed request damaged active asset");
    }else if(which=="generation"){
        pending=true;need(stager.update(queue,0,7,a,{},enhanced,active)==NativeAvatarUploadState::Pending,"generation request start failed");
        pending=false;need(stager.update(queue,0,8,a,{},enhanced,active)==NativeAvatarUploadState::Ready,"same key generation refresh cancelled useful upload");
        need(!stager.ready(7,a,enhanced)&&!stager.ready(8,a,basic),"ready ignored exact selection/generation");
        need(bool(stager.take_ready(8,a,enhanced))&&images_created==5,"generation-only refresh reuploaded maps");
    }else if(which=="skin-identity"||which=="skin-owner"){
        const auto old_indices=active->skin()->indices().buffer;
        if(which=="skin-identity")skin=skin_source();else skinning=gpu(argv[2],&uncertain);
        NativeAvatarAssetStager replacement(handle<VkDevice>(1),asset_get,nullptr,memory_properties(),8192,skinning,&uncertain);
        need(replacement.update(queue,0,7,a,skin,enhanced,active)==NativeAvatarUploadState::Pending,"incompatible skin identity incorrectly reused instead of rebuilt");
        need(replacement.update(queue,0,7,a,skin,enhanced,active)==NativeAvatarUploadState::Ready,"fresh skin identity did not complete");
        auto rich=replacement.take_ready(7,a,enhanced);need(rich&&rich->material(0).imageView==base,"skin identity change unnecessarily duplicated compatible base image");
        need(rich->skin()->indices().buffer!=old_indices&&rich->surface_vertices().range==32,"skin identity change failed to build independent skin leaves");
    }else if(which=="skin-switch"){
        const auto old_indices=active->skin()->indices().buffer;
        pending=true;need(stager.update(queue,0,7,a,skin,enhanced,active)==NativeAvatarUploadState::Pending,"surface stream switch failed");
        need(!stager.ready(7,a,enhanced)&&!active->surface_vertices().buffer,"pending surface stream leaked into basic active view");
        pending=false;need(stager.update(queue,0,7,a,skin,enhanced,active)==NativeAvatarUploadState::Ready,"surface upload fence did not complete");
        auto rich=stager.take_ready(7,a,enhanced);need(rich&&rich->skin()->indices().buffer==old_indices&&rich->surface_vertices().range==32,"enhancement duplicated base skin buffers or omitted side stream");
        const auto surface_buffer=rich->surface_vertices().buffer;auto recorded=rich;
        need(stager.update(queue,0,7,a,skin,basic,rich)==NativeAvatarUploadState::Pending,"basic skin view failed");
        need(stager.update(queue,0,7,a,skin,basic,rich)==NativeAvatarUploadState::Ready,"basic skin view not ready");
        auto fast=stager.take_ready(7,a,basic);need(fast&&fast->skin()->indices().buffer==old_indices&&!fast->surface_vertices().buffer,"Fast retained optional stream or duplicated ordinary skin");
        rich.reset();need(sizes.count(surface_buffer)==1,"recorded optional stream retired before last lease");
        recorded.reset();need(sizes.count(surface_buffer)==0,"Fast retained predecessor optional stream");
    }else if(which=="vector-key"){
        NativeSurfaceSelection first{{surface_normal|surface_matcap|surface_mask,0,0},surface_normal|surface_matcap|surface_mask};
        NativeSurfaceSelection second{{0,surface_normal|surface_matcap|surface_mask,0},surface_normal|surface_matcap|surface_mask};
        pending=true;need(stager.update(queue,0,7,a,{},first,active)==NativeAvatarUploadState::Pending,"vector key upload start failed");
        need(stager.update(queue,0,7,a,{},second,active)==NativeAvatarUploadState::Pending&&!stager.ready(7,a,second),"same combined mask ignored changed per-material key");
        pending=false;need(stager.update(queue,0,7,a,{},second,active)==NativeAvatarUploadState::Pending,"per-material change did not replace cancelled upload");
        need(stager.update(queue,0,7,a,{},second,active)==NativeAvatarUploadState::Ready,"per-material replacement not ready");
        auto rich=stager.take_ready(7,a,second);need(bool(rich),"per-material ready key lost");
        need(rich->surface_material(0,NativeSurfaceTexture::Normal).imageView==rich->material(99).imageView&&rich->surface_material(1,NativeSurfaceTexture::Normal).imageView!=rich->material(99).imageView,"optional map roles did not follow per-material selection");
    }else if(which=="completion"){
        need(stager.update(queue,0,7,a,{},enhanced,active)==NativeAvatarUploadState::Pending,"completion request failed");
        fail_completion=true;need(stager.update(queue,0,7,a,{},enhanced,active)==NativeAvatarUploadState::Failed&&uncertain,"unknown completion did not quarantine pending job");
        need(images_destroyed==0&&active->material(0).imageView==base,"unknown completion freed leased resources");
        fail_completion=false;uncertain=false;stager.device_idle_complete();
        need(images_destroyed==3,"device-idle retirement did not release optional leaves");
    }else if(which=="identity"){
        fail_image=true;need(stager.update(queue,0,7,a,{},enhanced,active)==NativeAvatarUploadState::Failed,"identity failure not surfaced");fail_image=false;
        auto other=model();
        need(stager.update(queue,0,7,other,{},enhanced,active)==NativeAvatarUploadState::Pending,"same generation failure poisoned another avatar");
        need(stager.update(queue,0,7,other,{},enhanced,active)==NativeAvatarUploadState::Ready,"other avatar failed to recover");
        auto rich=stager.take_ready(7,other,enhanced);need(rich&&rich->material(0).imageView!=base,"different avatar reused unrelated image leaves");
    }else throw std::runtime_error("unknown case");
    std::cout<<"PASS actual asset stager "<<which<<'\n';return 0;
}
int main(int argc,char** argv){
    SetErrorMode(GetErrorMode()|SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    std::set_terminate([]()noexcept{std::fputs("FAIL unexpected terminate in CPU asset fixture\n",stderr);std::fflush(stderr);std::_Exit(3);});
    try{
        if(argc==2&&!std::strcmp(argv[1],"--controlled-negative"))throw std::runtime_error("controlled negative");
        const auto result=run_case(argc,argv);need(teardown_faults==0,"fake Vulkan teardown faults");return result;
    }catch(const std::exception& e){std::cerr<<e.what()<<'\n';return 1;}
    catch(...){std::fputs("FAIL unknown CPU asset fixture exception\n",stderr);return 2;}
}
