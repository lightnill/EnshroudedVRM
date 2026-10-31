#include "native_skinning_fixture.hpp"
#include "../src/native_avatar_assets.hpp"
#include "../src/native_avatar_material.hpp"
#include "../src/avatar_appearance.hpp"
#include <fstream>
namespace {
PFN_vkGetDeviceProcAddr asset_real_get{};bool hide_completion=false,fail_completion=false;unsigned destroyed_images=0;
VkResult VKAPI_CALL asset_status(VkDevice device,VkFence fence){if(fail_completion)return VK_ERROR_UNKNOWN;if(hide_completion)return VK_NOT_READY;return reinterpret_cast<PFN_vkGetFenceStatus>(asset_real_get(device,"vkGetFenceStatus"))(device,fence);}
void VKAPI_CALL asset_destroy_image(VkDevice device,VkImage image,const VkAllocationCallbacks* callbacks){++destroyed_images;reinterpret_cast<PFN_vkDestroyImage>(asset_real_get(device,"vkDestroyImage"))(device,image,callbacks);}
PFN_vkVoidFunction VKAPI_CALL asset_get(VkDevice device,const char* name){
    if(!std::strcmp(name,"vkGetFenceStatus"))return reinterpret_cast<PFN_vkVoidFunction>(asset_status);
    if(!std::strcmp(name,"vkDestroyImage"))return reinterpret_cast<PFN_vkVoidFunction>(asset_destroy_image);
    return asset_real_get(device,name);
}
struct Source {std::shared_ptr<const vrm::Avatar> avatar;std::shared_ptr<const evrm::NativeSkinAsset> skin;};
Source source(std::array<std::uint8_t,4> color){auto input=fixture();input.avatar.materials.emplace_back();input.avatar.materials[0].base_texture=0;
    vrm::Texture texture;texture.image=0;input.avatar.textures.push_back(texture);input.avatar.images.push_back({1,1,{color.begin(),color.end()}});
    auto skin=std::make_shared<const evrm::NativeSkinAsset>(evrm::make_native_skin_asset(input.avatar,input.pose));require(bool(*skin),"prepared test skin");
    return {std::make_shared<const vrm::Avatar>(std::move(input.avatar)),std::move(skin)};
}
struct Probe {
    Context& c;VkDescriptorSetLayout set_layout{};VkPipelineLayout layout{};VkPipeline pipeline{};VkShaderModule module{};VkDescriptorPool pool{};VkDescriptorSet set{};
    Probe(Context& context,const std::filesystem::path& folder):c(context){
        const std::array<VkDescriptorSetLayoutBinding,2> bindings{{{0,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
        auto si=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);si.bindingCount=bindings.size();si.pBindings=bindings.data();checked(c.fn<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(c.device,&si,nullptr,&set_layout),"probe set");
        auto li=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);li.setLayoutCount=1;li.pSetLayouts=&set_layout;checked(c.fn<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(c.device,&li,nullptr,&layout),"probe layout");
        std::ifstream file(folder/L"avatar_texture_probe.spv",std::ios::binary|std::ios::ate);require(bool(file),"probe shader missing");const auto size=file.tellg();std::vector<std::uint32_t> words(std::size_t(size)/4);file.seekg(0);file.read(reinterpret_cast<char*>(words.data()),size);
        auto mi=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);mi.codeSize=std::size_t(size);mi.pCode=words.data();checked(c.fn<PFN_vkCreateShaderModule>("vkCreateShaderModule")(c.device,&mi,nullptr,&module),"probe module");
        auto pi=info<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);pi.layout=layout;pi.stage=info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=module;pi.stage.pName="cs_main";
        checked(c.fn<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(c.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline),"probe pipeline");
        const std::array<VkDescriptorPoolSize,2> sizes{{{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}}};auto pool_info=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pool_info.maxSets=1;pool_info.poolSizeCount=sizes.size();pool_info.pPoolSizes=sizes.data();checked(c.fn<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(c.device,&pool_info,nullptr,&pool),"probe pool");
        auto ai=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);ai.descriptorPool=pool;ai.descriptorSetCount=1;ai.pSetLayouts=&set_layout;checked(c.fn<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(c.device,&ai,&set),"probe allocate");
    }
    void sample(const evrm::NativeAvatarGpuAssets& assets,std::array<float,4> wanted){HostBuffer out(c,16);const auto image=assets.material(0);const auto buffer=out.binding();
        std::array<VkWriteDescriptorSet,2> writes{};for(unsigned n=0;n<2;++n){writes[n]=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);writes[n].dstSet=set;writes[n].dstBinding=n;writes[n].descriptorCount=1;}
        writes[0].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[0].pImageInfo=&image;writes[1].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[1].pBufferInfo=&buffer;c.fn<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(c.device,writes.size(),writes.data(),0,nullptr);
        c.begin();c.fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(c.command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);c.fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(c.command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);c.fn<PFN_vkCmdDispatch>("vkCmdDispatch")(c.command,1,1,1);
        auto barrier=info<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;c.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(c.command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);c.submit();
        const auto* color=static_cast<const float*>(out.mapped);for(unsigned n=0;n<4;++n)require(std::abs(color[n]-wanted[n])<1e-5f,"uploaded texture pixels do not match selected asset");
    }
    ~Probe(){if(c.uncertain)return;c.fn<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(c.device,pool,nullptr);c.fn<PFN_vkDestroyPipeline>("vkDestroyPipeline")(c.device,pipeline,nullptr);c.fn<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(c.device,module,nullptr);c.fn<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(c.device,layout,nullptr);c.fn<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(c.device,set_layout,nullptr);}
};
}
#include "avatar_material_gpu.inl"
int wmain(int argc,wchar_t** argv){try{
    (void)&observed_get;require(argc==2,"fixture folder");std::cerr<<"checkpoint context\n";Context c;std::cerr<<"checkpoint context ready\n";asset_real_get=c.get;const auto folder=std::filesystem::path(argv[1]);
    require((GetErrorMode()&SEM_NOGPFAULTERRORBOX)!=0,"test process may leave unattended crash dialogs");
    auto skinning=std::make_shared<evrm::NativeSkinningGpu>(c.device,asset_get,c.memory,c.limits,folder,&c.uncertain);std::cerr<<"checkpoint skin pipelines ready\n";
    evrm::NativeAvatarAssetStager stage(c.device,asset_get,nullptr,c.memory,skinning,&c.uncertain);std::cerr<<"checkpoint stager ready\n";const auto red=source({255,0,0,255}),green=source({0,255,0,255});std::cerr<<"checkpoint sources ready\n";
    std::cerr<<"checkpoint begin red\n";hide_completion=true;require(stage.update(c.queue,c.family,1,red.avatar,red.skin)==evrm::NativeAvatarUploadState::Pending,"upload did not stage");
    require(!stage.ready(1,red.avatar),"uncompleted upload published");const auto destroyed=destroyed_images;stage.cancel();require(destroyed_images==destroyed,"cancel freed unproven pending images");
    require(stage.update(c.queue,c.family,2,green.avatar,green.skin)==evrm::NativeAvatarUploadState::Pending&&!stage.ready(1,red.avatar),"superseded upload activated");
    std::cerr<<"checkpoint cancel/supersede\n";hide_completion=false;const auto end=GetTickCount64()+5000;while(stage.update(c.queue,c.family,2,green.avatar,green.skin)==evrm::NativeAvatarUploadState::Pending&&GetTickCount64()<end)Sleep(1);
    std::cerr<<"checkpoint ready green\n";auto active=stage.take_ready(2,green.avatar);require(active&&active->avatar()==green.avatar&&active->skin(),"latest ready GPU model missing");Probe probe(c,folder);std::cerr<<"checkpoint sample green\n";probe.sample(*active,{0,1,0,1});
    MaterialProbe material(c,folder);material.verify(*active);
    const auto before=destroyed_images;const auto next_end=GetTickCount64()+5000;while(stage.update(c.queue,c.family,3,red.avatar,red.skin)==evrm::NativeAvatarUploadState::Pending&&GetTickCount64()<next_end)Sleep(1);
    std::cerr<<"checkpoint ready red\n";auto next=stage.take_ready(3,red.avatar);require(bool(next),"replacement asset not ready");require(destroyed_images==before,"active old asset released while leased");probe.sample(*active,{0,1,0,1});probe.sample(*next,{1,0,0,1});
    std::weak_ptr<evrm::NativeAvatarGpuAssets> old=active;active.reset();require(old.expired()&&destroyed_images>before,"old GPU asset did not retire after final lease");
    std::cerr<<"checkpoint fault\n";require(stage.update(c.queue,c.family,4,green.avatar,green.skin)==evrm::NativeAvatarUploadState::Pending,"fault upload did not start");fail_completion=true;
    const auto fault_before=destroyed_images;require(stage.update(c.queue,c.family,4,green.avatar,green.skin)==evrm::NativeAvatarUploadState::Failed&&c.uncertain,"unknown completion was treated as safe");require(destroyed_images==fault_before,"uncertain GPU resources were freed");
    std::cerr<<"checkpoint teardown\n";fail_completion=false;checked(c.fn<PFN_vkDeviceWaitIdle>("vkDeviceWaitIdle")(c.device),"test teardown completion proof");c.uncertain=false;stage.device_idle_complete();
    std::cout<<"PASS real-GPU avatar upload pixels, delayed/cancelled/superseded completion, old asset leases and uncertain-completion retention\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
