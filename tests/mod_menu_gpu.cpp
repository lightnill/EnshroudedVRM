#include "native_skinning_fixture.hpp"
#include "../src/mod_menu_gpu.hpp"
#include <fstream>
namespace {
struct ColorImage {
    Context& c;VkImage image{};VkDeviceMemory memory{};
    ColorImage(Context& ctx):c(ctx){auto ci=info<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);ci.imageType=VK_IMAGE_TYPE_2D;ci.format=VK_FORMAT_R8G8B8A8_UNORM;
        ci.extent={1000,800,1};ci.mipLevels=ci.arrayLayers=1;ci.samples=VK_SAMPLE_COUNT_1_BIT;ci.tiling=VK_IMAGE_TILING_OPTIMAL;ci.usage=VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_TRANSFER_SRC_BIT|VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        checked(c.fn<PFN_vkCreateImage>("vkCreateImage")(c.device,&ci,nullptr,&image),"UI test image");VkMemoryRequirements req{};c.fn<PFN_vkGetImageMemoryRequirements>("vkGetImageMemoryRequirements")(c.device,image,&req);
        unsigned type=0;while(type<c.memory.memoryTypeCount&&!(req.memoryTypeBits&(1u<<type)))++type;require(type<c.memory.memoryTypeCount,"UI image memory");
        auto ai=info<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);ai.allocationSize=req.size;ai.memoryTypeIndex=type;checked(c.fn<PFN_vkAllocateMemory>("vkAllocateMemory")(c.device,&ai,nullptr,&memory),"UI memory");
        checked(c.fn<PFN_vkBindImageMemory>("vkBindImageMemory")(c.device,image,memory,0),"UI bind");
        c.begin();auto barrier=info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);barrier.image=image;barrier.oldLayout=VK_IMAGE_LAYOUT_UNDEFINED;barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.dstAccessMask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};
        c.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(c.command,VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkClearColorValue color{{.15f,.1f,.08f,1}};c.fn<PFN_vkCmdClearColorImage>("vkCmdClearColorImage")(c.command,image,VK_IMAGE_LAYOUT_GENERAL,&color,1,&barrier.subresourceRange);c.submit();
    }
    ~ColorImage(){c.fn<PFN_vkDestroyImage>("vkDestroyImage")(c.device,image,nullptr);c.fn<PFN_vkFreeMemory>("vkFreeMemory")(c.device,memory,nullptr);}
};
}
int wmain(int argc,wchar_t** argv){try{
    (void)&fixture;(void)&observed_get;require(argc==2,"output directory");Context c(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT,true);
    ColorImage one(c),two(c);evrm::ModMenuGpu gpu(c.instance,c.physical,c.device,c.gipa,c.get,nullptr,c.queue,c.family,VK_FORMAT_R8G8B8A8_UNORM,{1000,800},{one.image,two.image},VK_IMAGE_LAYOUT_GENERAL,1);
    evrm::ModSettings settings;evrm::ModMenuStatus status;status.mouth=evrm::ModMouthSupport::Ready;bool expanded=false,close=false;
    HostBuffer readback(c,1000*800*4);evrm::ModInputFrame input;input.open=true;
    evrm::ModMenuPlacement placement;
    for(unsigned mode=0;mode<6;++mode){settings.voice_enabled=mode!=0;expanded=mode==2||mode==3;const unsigned index=mode%2;
        if(mode==3){settings.voice_source=evrm::VoiceInputSource::Microphone;status.microphone=evrm::MicrophoneState::Ready;
            auto devices=std::make_shared<evrm::MicrophoneDeviceList>();devices->available=true;devices->devices.push_back({"fixture","Studio microphone"});status.microphones=std::move(devices);}
        if(mode>=4){settings.voice_source=evrm::VoiceInputSource::Game;auto avatar=std::make_shared<evrm::AvatarMenuSnapshot>();avatar->preferences_loaded=true;avatar->name="Linthiel";avatar->fingerprint=std::string(64,'a');avatar->path="C:\\fixture\\Linthiel.vrm";
            avatar->materials=std::make_shared<const std::vector<std::string>>(std::vector<std::string>{"Face","Arms","Hair","Glasses"});auto profile=std::make_shared<evrm::AvatarProfile>();profile->whole=evrm::AvatarShaderPreset::Matte;avatar->profile=profile;
            status.avatar=avatar;placement.appearance_expanded=mode==5;}
        require(gpu.available(),"UI frame slot completed");const auto result=gpu.draw(index,0,nullptr,settings,status,expanded,close,input,&placement);
        require(result.result==VK_SUCCESS&&result.submitted&&result.completion,"UI submitted");
        c.begin();auto barrier=info<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);barrier.image=index?two.image:one.image;barrier.oldLayout=barrier.newLayout=VK_IMAGE_LAYOUT_GENERAL;
        barrier.srcQueueFamilyIndex=barrier.dstQueueFamilyIndex=VK_QUEUE_FAMILY_IGNORED;barrier.srcAccessMask=VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
        barrier.subresourceRange={VK_IMAGE_ASPECT_COLOR_BIT,0,1,0,1};c.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(c.command,VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,nullptr,0,nullptr,1,&barrier);
        VkBufferImageCopy copy{};copy.imageSubresource={VK_IMAGE_ASPECT_COLOR_BIT,0,0,1};copy.imageExtent={1000,800,1};c.fn<PFN_vkCmdCopyImageToBuffer>("vkCmdCopyImageToBuffer")(c.command,barrier.image,VK_IMAGE_LAYOUT_GENERAL,readback.buffer,1,&copy);
        checked(c.fn<PFN_vkEndCommandBuffer>("vkEndCommandBuffer")(c.command),"UI readback end");checked(c.fn<PFN_vkResetFences>("vkResetFences")(c.device,1,&c.fence),"UI readback fence");
        const VkPipelineStageFlags stage=VK_PIPELINE_STAGE_TRANSFER_BIT;auto submit=info<VkSubmitInfo>(VK_STRUCTURE_TYPE_SUBMIT_INFO);submit.waitSemaphoreCount=1;submit.pWaitSemaphores=&result.completion;submit.pWaitDstStageMask=&stage;submit.commandBufferCount=1;submit.pCommandBuffers=&c.command;
        checked(c.fn<PFN_vkQueueSubmit>("vkQueueSubmit")(c.queue,1,&submit,c.fence),"UI readback submit");checked(c.fn<PFN_vkWaitForFences>("vkWaitForFences")(c.device,1,&c.fence,VK_TRUE,5'000'000'000),"UI readback completion");
        const auto* bytes=static_cast<unsigned char*>(readback.mapped);require(bytes[0]>=36&&bytes[0]<=40,"LOAD preserved outside menu");
        bool changed=false;for(unsigned y=100;y<700;++y)if(bytes[(y*1000+500)*4]!=bytes[0]){changed=true;break;}require(changed,"UI pixels rendered");
        std::ofstream file(std::filesystem::path(argv[1])/(mode==0?L"menu-off.ppm":mode==1?L"menu-on.ppm":mode==2?L"menu-expanded.ppm":mode==3?L"menu-microphone.ppm":mode==4?L"menu-avatar.ppm":L"menu-appearance.ppm"),std::ios::binary);file<<"P6\n1000 800\n255\n";
        for(unsigned p=0;p<1000*800;++p)file.write(reinterpret_cast<const char*>(bytes+p*4),3);
    }
    gpu.presentation_complete();std::cout<<"PASS headless real-GPU menu LOAD/draw/readback, independent images and compact/expanded captures\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
