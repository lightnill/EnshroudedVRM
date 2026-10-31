namespace {
struct MaterialProbe {
    Context& c;VkDescriptorSetLayout set_layout{};VkPipelineLayout layout{};VkShaderModule shader{};VkPipeline pipeline{};VkDescriptorPool pool{};VkDescriptorSet set{};
    HostBuffer frame,material,output;
    MaterialProbe(Context& context,const std::filesystem::path& folder):c(context),frame(c,208,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),material(c,80,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT),output(c,64){
        const std::array<VkDescriptorSetLayoutBinding,5> bindings{{
            {0,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{1,VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},
            {2,VK_DESCRIPTOR_TYPE_SAMPLER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{3,VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr},{4,VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1,VK_SHADER_STAGE_COMPUTE_BIT,nullptr}}};
        auto si=info<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);si.bindingCount=bindings.size();si.pBindings=bindings.data();checked(c.fn<PFN_vkCreateDescriptorSetLayout>("vkCreateDescriptorSetLayout")(c.device,&si,nullptr,&set_layout),"material set");
        auto li=info<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);li.setLayoutCount=1;li.pSetLayouts=&set_layout;checked(c.fn<PFN_vkCreatePipelineLayout>("vkCreatePipelineLayout")(c.device,&li,nullptr,&layout),"material layout");
        std::ifstream file(folder/L"avatar_material_probe.spv",std::ios::binary|std::ios::ate);require(bool(file),"material shader missing");const auto bytes=file.tellg();std::vector<std::uint32_t> words(std::size_t(bytes)/4);file.seekg(0);file.read(reinterpret_cast<char*>(words.data()),bytes);
        auto mi=info<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);mi.codeSize=std::size_t(bytes);mi.pCode=words.data();checked(c.fn<PFN_vkCreateShaderModule>("vkCreateShaderModule")(c.device,&mi,nullptr,&shader),"material module");
        auto pi=info<VkComputePipelineCreateInfo>(VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO);pi.layout=layout;pi.stage=info<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);pi.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pi.stage.module=shader;pi.stage.pName="cs_material";
        checked(c.fn<PFN_vkCreateComputePipelines>("vkCreateComputePipelines")(c.device,VK_NULL_HANDLE,1,&pi,nullptr,&pipeline),"material pipeline");
        const std::array<VkDescriptorPoolSize,4> sizes{{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,2},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,1},{VK_DESCRIPTOR_TYPE_SAMPLER,1},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,1}}};
        auto pool_info=info<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);pool_info.maxSets=1;pool_info.poolSizeCount=sizes.size();pool_info.pPoolSizes=sizes.data();checked(c.fn<PFN_vkCreateDescriptorPool>("vkCreateDescriptorPool")(c.device,&pool_info,nullptr,&pool),"material pool");
        auto ai=info<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);ai.descriptorPool=pool;ai.descriptorSetCount=1;ai.pSetLayouts=&set_layout;checked(c.fn<PFN_vkAllocateDescriptorSets>("vkAllocateDescriptorSets")(c.device,&ai,&set),"material allocate");
        static_cast<std::uint32_t*>(frame.mapped)[48]=1;
    }
    void verify(const evrm::NativeAvatarGpuAssets& assets){
        evrm::AvatarAppearance appearance;appearance.whole=evrm::AvatarShaderPreset::Matte;appearance.materials={evrm::AvatarShaderPreset::GameLighting,evrm::AvatarShaderPreset::Matte};
        for(unsigned mode=0;mode<2;++mode){
            vrm::Material source;source.base_color={.2f,.4f,.6f,.8f};source.alpha=vrm::AlphaMode::Mask;source.alpha_cutoff=.5f;source.double_sided=true;
            const auto constants=evrm::native_avatar_material(source,99,evrm::avatar_material_shader_mode(&appearance,mode),false);std::memcpy(material.mapped,&constants,sizeof constants);
            const std::array<VkDescriptorBufferInfo,3> buffers{frame.binding(),material.binding(),output.binding()};const auto image=assets.material(0);
            std::array<VkWriteDescriptorSet,5> writes{};for(unsigned n=0;n<5;++n){writes[n]=info<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);writes[n].dstSet=set;writes[n].dstBinding=n;writes[n].descriptorCount=1;}
            writes[0].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;writes[0].pBufferInfo=&buffers[0];writes[1].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;writes[1].pImageInfo=&image;
            writes[2].descriptorType=VK_DESCRIPTOR_TYPE_SAMPLER;writes[2].pImageInfo=&image;writes[3].descriptorType=VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;writes[3].pBufferInfo=&buffers[1];writes[4].descriptorType=VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[4].pBufferInfo=&buffers[2];
            c.fn<PFN_vkUpdateDescriptorSets>("vkUpdateDescriptorSets")(c.device,writes.size(),writes.data(),0,nullptr);
            c.begin();c.fn<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(c.command,VK_PIPELINE_BIND_POINT_COMPUTE,pipeline);c.fn<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(c.command,VK_PIPELINE_BIND_POINT_COMPUTE,layout,0,1,&set,0,nullptr);c.fn<PFN_vkCmdDispatch>("vkCmdDispatch")(c.command,1,1,1);
            auto barrier=info<VkMemoryBarrier>(VK_STRUCTURE_TYPE_MEMORY_BARRIER);barrier.srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dstAccessMask=VK_ACCESS_HOST_READ_BIT;
            c.fn<PFN_vkCmdPipelineBarrier>("vkCmdPipelineBarrier")(c.command,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,nullptr,0,nullptr);c.submit();
            const auto* values=static_cast<const float*>(output.mapped);
            require(std::abs(values[0])<1e-5f&&std::abs(values[1]-.4f)<1e-5f&&std::abs(values[2])<1e-5f&&values[3]==1,"preset changed diffuse/occlusion output");
            require(std::abs(values[4]-.5f)<1e-5f&&std::abs(values[5]-.5f)<1e-5f&&std::abs(values[6]-(mode?1.f:.35f))<1e-5f,"native Game/Matte normal output wrong");
            require(values[8]==0&&values[9]==(mode?0.f:89.f)&&values[11]==8&&values[12]==0&&values[13]==0&&values[14]==0,"preset changed unrelated GBuffer outputs");
        }
        std::cout<<"PASS production GBuffer material output function on GPU: mixed Game/Matte, diffuse/occlusion/motion flags preserved (compute probe excludes raster/discard)\n";
    }
    ~MaterialProbe(){if(c.uncertain)return;c.fn<PFN_vkDestroyDescriptorPool>("vkDestroyDescriptorPool")(c.device,pool,nullptr);c.fn<PFN_vkDestroyPipeline>("vkDestroyPipeline")(c.device,pipeline,nullptr);c.fn<PFN_vkDestroyShaderModule>("vkDestroyShaderModule")(c.device,shader,nullptr);c.fn<PFN_vkDestroyPipelineLayout>("vkDestroyPipelineLayout")(c.device,layout,nullptr);c.fn<PFN_vkDestroyDescriptorSetLayout>("vkDestroyDescriptorSetLayout")(c.device,set_layout,nullptr);}
};
}
