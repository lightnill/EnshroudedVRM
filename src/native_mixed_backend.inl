// Included inside NativeBackend::Impl; every entry runs under its source mutex.
    void mixed_notice(const char* reason,const char* key) noexcept {try{notice(reason,key);}catch(...){}}
    void initialize_mixed() {
        if(mixed_initialized)return;
        if(mixed_failed)throw std::runtime_error("Mixed native initialization previously declined");
        mixed_failed=true;
        if(!replacement||!image_memory||!limits.minUniformBufferOffsetAlignment||
           native_oit_constants_offset%limits.minUniformBufferOffsetAlignment||
           native_oit_forward_offset%limits.minUniformBufferOffsetAlignment||
           limits.maxStorageBufferRange<native_oit_prepared_bytes||limits.maxUniformBufferRange<2320||
           limits.maxPerStageDescriptorSampledImages<3||limits.maxDescriptorSetSampledImages<3||
           limits.maxPerStageDescriptorSamplers<2||limits.maxDescriptorSetSamplers<2)
            throw std::runtime_error("Mixed native limits unsupported");
        const auto format=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(get_instance(instance,"vkGetPhysicalDeviceFormatProperties"));
        const auto image_format=reinterpret_cast<PFN_vkGetPhysicalDeviceImageFormatProperties>(get_instance(instance,"vkGetPhysicalDeviceImageFormatProperties"));
        if(!format||!image_format)throw std::runtime_error("Mixed native format queries unavailable");
        snapshot_caps.limits=limits;snapshot_caps.memory=context.memory;
        const std::array<VkFormat,4> formats={VK_FORMAT_E5B9G9R9_UFLOAT_PACK32,VK_FORMAT_R16_UNORM,VK_FORMAT_R32G32B32A32_SFLOAT,VK_FORMAT_R32_SFLOAT};
        for(unsigned i=0;i<4;++i){VkFormatProperties facts{};format(physical,formats[i],&facts);snapshot_caps.formats[i]=facts.optimalTilingFeatures;}
        for(unsigned i=0;i<2;++i)checked(image_format(physical,formats[i+2],VK_IMAGE_TYPE_3D,VK_IMAGE_TILING_OPTIMAL,
            VK_IMAGE_USAGE_SAMPLED_BIT|VK_IMAGE_USAGE_STORAGE_BIT,0,&snapshot_caps.outputs[i]),"Mixed output format query");
        std::ifstream input(shader_directory/"native_volume_snapshot.comp.spv",std::ios::binary|std::ios::ate);
        if(!input)throw std::runtime_error("Missing native volume snapshot shader");
        const auto bytes=input.tellg();if(bytes<20||bytes>1024*1024||bytes%4)throw std::runtime_error("Native volume snapshot shader size");
        snapshot_words.resize(static_cast<std::size_t>(bytes)/4);input.seekg(0);input.read(reinterpret_cast<char*>(snapshot_words.data()),bytes);
        if(!input||snapshot_words[0]!=0x07230203u)throw std::runtime_error("Native volume snapshot shader bytes");
        std::array<VkDescriptorSetLayoutBinding,11> bindings{};
        for(unsigned i=0;i<11;++i){auto& b=bindings[i];b.binding=i;b.descriptorCount=1;
            b.descriptorType=(i==0||i==3||i==6||i==7)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                (i==1||i==8||i==9)?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:(i==2||i==10)?VK_DESCRIPTOR_TYPE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b.stageFlags=(i==4||i==5)?VK_SHADER_STAGE_VERTEX_BIT:(i==0||i==3)?VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_FRAGMENT_BIT;
        }
        VkDescriptorSetLayoutCreateInfo dsl{};dsl.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;dsl.bindingCount=bindings.size();dsl.pBindings=bindings.data();
        checked(F(vkCreateDescriptorSetLayout)(context.device,&dsl,nullptr,&oit_set),"Mixed native OIT set layout");
        VkPipelineLayoutCreateInfo layout{};layout.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.setLayoutCount=1;layout.pSetLayouts=&oit_set;
        checked(F(vkCreatePipelineLayout)(context.device,&layout,nullptr,&oit_layout),"Mixed native OIT layout");
        oit_shader=shader("avatar_native.pull.oit.spv");
        oit_pipelines=std::make_unique<NativeOitDrawPipelines>(context.device,context.get,oit_layout,shaders[0],oit_shader,independent_blend);
        VkSamplerCreateInfo sampler{};sampler.sType=VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;sampler.minFilter=sampler.magFilter=VK_FILTER_LINEAR;
        sampler.mipmapMode=VK_SAMPLER_MIPMAP_MODE_NEAREST;sampler.addressModeU=sampler.addressModeV=sampler.addressModeW=VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
        checked(F(vkCreateSampler)(context.device,&sampler,nullptr,&volume_sampler),"Mixed native volume sampler");
        mixed_initialized=true;mixed_failed=false;
    }
    static bool full_volume(const NativeSampledImageView& v) noexcept {
        const auto& r=v.subresource;
        return v.image&&v.image_generation&&v.view&&v.view_generation&&v.image_type==VK_IMAGE_TYPE_3D&&
            v.view_type==VK_IMAGE_VIEW_TYPE_3D&&v.samples==VK_SAMPLE_COUNT_1_BIT&&
            v.extent.width&&v.extent.height&&v.extent.depth&&r.aspectMask==VK_IMAGE_ASPECT_COLOR_BIT&&
            v.mip_levels==1&&v.array_layers==1&&!r.baseMipLevel&&r.levelCount==1&&!r.baseArrayLayer&&r.layerCount==1;
    }
    static bool same_image(const NativeOitView& a,const NativeOitView& b) noexcept {
        return a.current&&b.current&&a.view==b.view&&a.image==b.image&&
            a.view_generation==b.view_generation&&a.image_generation==b.image_generation;
    }
    bool volume_metadata_current(const Bundle& b) const {
        if(!image_memory)return false;
        for(unsigned i=0;i<2;++i){const auto& storage=b.volume_storage[i];
            const auto now=metadata.observed_storage_image_binding(b.volume_bound,2,3+i);
            if(!metadata.live(storage)||!now||now.info.imageView!=storage.info.imageView||
               now.view.image_generation!=storage.view.image_generation||!image_memory->current(b.volume_allocations[i]))return false;
        }
        return true;
    }
    bool mixed_volume_current(const Bundle& b) const {
        return b.snapshot_recorded&&b.volume_snapshot&&volume_metadata_current(b);
    }
    bool mixed_late_current(const Bundle& b) const {
        if(b.late_witnesses.empty()||b.late_witnesses.size()>16)return false;
        for(const auto& w:b.late_witnesses){
            if(!source_current(w.constants)||!source_current(w.forward)||
               !metadata.observed_buffer_binding(w.bound,0,11)||!metadata.observed_buffer_binding(w.bound,3,11))return false;
            for(unsigned i=0;i<2;++i){const auto now=metadata.observed_sampled_image_binding(w.bound,3,25+i);
                if(!metadata.live(w.images[i])||!now||now.info.imageView!=w.images[i].info.imageView||
                   now.view.image_generation!=b.volume_storage[i].view.image_generation||now.view.image!=b.volume_storage[i].view.image)return false;
            }
        }
        return true;
    }
    bool mixed_current(const Bundle& b) const {
        if(!mixed_volume_current(b)||!mixed_late_current(b)||!b.oit_raster.matches(b.first_pass.extent))return false;
        if(!same_image(b.oit_depth,metadata.attachment_view(b.oit_depth.view)))return false;
        for(const auto& c:b.oit_colors)if(!same_image(c,metadata.attachment_view(c.view)))return false;
        return true;
    }
    void mixed_dispatch(Recording& r,VkCommandBuffer command,bool direct,unsigned x,unsigned y,unsigned z) {
        if(!r.bundle||!r.bundle->material_plan.mixed)return;
        auto& b=*r.bundle;
        const auto bound=metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_COMPUTE);
        if(!bound.valid){b.invalid=true;b.snapshot_decline="compute-observation";mixed_notice("mixed compute observation unavailable","mixed-compute-unavailable");return;}
        if(native_volume_producer(bound.pipeline.compute_bytes,bound.pipeline.compute_digest)!=NativeVolumeProducer::populate)return;
        b.snapshot_dispatch={x,y,z};
        if(b.snapshot_attempted){b.invalid=true;b.snapshot_decline="duplicate-populate";mixed_notice("mixed duplicate Populate declined","mixed-duplicate-populate");return;}
        b.snapshot_attempted=true;
        const auto decline=[&](const char* stage,std::uint32_t binding=UINT32_MAX,VkImage image=VK_NULL_HANDLE){
            b.invalid=true;b.snapshot_decline=stage;b.snapshot_decline_binding=binding;b.snapshot_decline_image=image;
            b.snapshot_decline_bound=bound;mixed_notice("mixed Populate snapshot prerequisites declined","mixed-snapshot-declined");};
        if(!direct||!x||!y||!z||!r.eligible||r.tainted||b.invalid||context.uncertain||r.pass_active){decline("recording");return;}
        const auto restore=compatible(bound);if(!restore){decline("compute-restore");return;}
        std::array<NativeVolumeSnapshotSource,2> sources{};
        for(unsigned i=0;i<2;++i){auto storage=metadata.observed_storage_image_binding(bound,2,3+i);
            if(!storage){decline("storage-observation",3+i);return;}
            if(storage.declaration_flags){decline("storage-declaration",3+i,storage.view.image);return;}
            if(storage.info.imageLayout!=VK_IMAGE_LAYOUT_GENERAL){decline("storage-layout",3+i,storage.view.image);return;}
            if(!full_volume(storage.view)){decline("storage-view-shape",3+i,storage.view.image);return;}
            if(!metadata.live(storage)){decline("storage-lifetime",3+i,storage.view.image);return;}
            if(!(storage.view.usage&VK_IMAGE_USAGE_STORAGE_BIT)){decline("storage-usage",3+i,storage.view.image);return;}
            if(!image_memory||!image_memory->ordinary_binding(storage.view.image,b.volume_allocations[i])){decline("storage-allocation",3+i,storage.view.image);return;}
            const auto& v=storage.view;auto& s=sources[i];s.image=v.image;s.generation=v.image_generation;s.type=v.image_type;
            s.format=v.image_format;s.flags=v.image_flags;s.usage=v.image_usage;s.extent=v.extent;s.samples=v.samples;s.mip_levels=v.mip_levels;s.array_layers=v.array_layers;
            s.format_count=v.image_format_count;s.formats=v.image_formats;b.volume_storage[i]=storage;
            if(v.format!=(i?VK_FORMAT_R16_UNORM:VK_FORMAT_R32_UINT)){decline("storage-format",3+i,storage.view.image);return;}
        }
        const auto& extent=sources[0].extent;
        b.snapshot_extent=extent;
        if(x!=(extent.width+3)/4||y!=(extent.height+3)/4||z!=(extent.depth+3)/4){decline("dispatch-coverage");return;}
        // Constructor validates formats, equal extents, capability/size budgets
        // and allocates every private resource before any private command.
        auto snapshot=std::make_unique<NativeVolumeSnapshot>(context.device,context.get,sources,snapshot_caps,snapshot_words.data(),snapshot_words.size());
        b.volume_bound=bound;b.volume_restore=restore;b.volume_snapshot=std::move(snapshot);
        if(!volume_metadata_current(b)){decline("retained-metadata");return;}
        // Retain before recording; a failed recording cannot destroy resources
        // still referenced by this original primary.
        if(!b.volume_snapshot->record(command,bound.pipeline.pipeline,restore->handle,bound.sets.size(),bound.sets.data(),x,y,z)){decline("snapshot-record");return;}
        b.snapshot_recorded=true;mixed_notice("mixed immediate full-Populate owned snapshot recorded","mixed-snapshot-ready");
    }
    void mixed_rendering(Recording& r,const VkRenderingInfo& info) {
        if(r.pass.supported&&r.pass.kind==NativePipelineKind::gbuffer){r.mixed_gbuffer_depth=metadata.attachment_view(r.pass.depth);r.mixed_gbuffer_extent=r.pass.extent;}
        if(!r.bundle||!r.bundle->material_plan.mixed)return;
        auto& b=*r.bundle;std::array<NativeOitView,5> colors{};NativeOitView depth;
        if(info.colorAttachmentCount==5&&info.pColorAttachments)for(unsigned i=0;i<5;++i)colors[i]=metadata.attachment_view(info.pColorAttachments[i].imageView);
        if(info.pDepthAttachment)depth=metadata.attachment_view(info.pDepthAttachment->imageView);
        const bool matched=native_oit_final_pass(info,colors,depth,r.mixed_gbuffer_depth,r.mixed_gbuffer_extent);
        b.oit_window.begin(matched);
        if(matched){b.oit_colors=colors;b.oit_depth=depth;}
        if(b.oit_window.failed)b.invalid=true;
    }
    bool late_arena(const Bundle& b,const NativeBufferBinding& source,VkDeviceSize range)const {
        return source&&source.declaration_flags==0&&source.info.buffer==b.arena.info.buffer&&source.generation==b.arena.generation&&
            source.info.range==range&&source.info.offset<=UINT32_MAX&&!(source.info.offset&15)&&
            source.info.offset<=b.arena.size&&range<=b.arena.size-source.info.offset&&
            !(source.info.offset%limits.minUniformBufferOffsetAlignment)&&source_current(source);
    }
    void mixed_late(Recording& r,VkCommandBuffer command) {
        if(!r.bundle||!r.bundle->material_plan.mixed||!r.bundle->oit_window.active)return;
        auto& b=*r.bundle;if(b.invalid||r.tainted)return;
        const auto bound=metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS);
        const auto kind=bound.valid?native_oit_late_shader(bound.pipeline):NativeOitShader::unsupported;
        if(kind!=NativeOitShader::lit_diffuse&&kind!=NativeOitShader::lit_shaded)return;
        Bundle::LateWitness w;w.bound=bound;w.constants=metadata.observed_buffer_binding(bound,0,11);w.forward=metadata.observed_buffer_binding(bound,3,11);
        if(!bound.pipeline.oit_compatible||!late_arena(b,w.constants,2320)||!late_arena(b,w.forward,96)||!mixed_volume_current(b)){b.invalid=true;return;}
        for(unsigned i=0;i<2;++i){w.images[i]=metadata.observed_sampled_image_binding(bound,3,25+i);const auto& s=w.images[i];const auto& captured=b.volume_storage[i].view;
            if(!s||!metadata.live(s)||!full_volume(s.view)||s.info.imageLayout!=VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||
               (s.declaration_flags&&(s.declaration_flags!=VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT||!partially_bound))||
               s.view.format!=(i?VK_FORMAT_R16_UNORM:VK_FORMAT_E5B9G9R9_UFLOAT_PACK32)||
               s.view.image!=captured.image||s.view.image_generation!=captured.image_generation||
               s.view.extent.width!=captured.extent.width||s.view.extent.height!=captured.extent.height||s.view.extent.depth!=captured.extent.depth){b.invalid=true;return;}
        }
        if(!b.late_witnesses.empty()){
            const auto& first=b.late_witnesses.front();
            if(!same_binding(first.constants,w.constants)){b.invalid=true;return;}
            for(const auto& old:b.late_witnesses)if(same_binding(old.constants,w.constants)&&same_binding(old.forward,w.forward)&&
                old.images[0].set==w.images[0].set&&old.images[0].set_version==w.images[0].set_version&&old.bound.pipeline.pipeline==w.bound.pipeline.pipeline){b.oit_window.observe_lit(true);return;}
        }
        if(b.late_witnesses.size()>=16){b.invalid=true;return;}
        if(b.late_witnesses.empty()){b.data.blend[1]=static_cast<std::uint32_t>(w.constants.info.offset);b.data.blend[2]=static_cast<std::uint32_t>(w.forward.info.offset);}
        b.late_witnesses.push_back(std::move(w));b.oit_window.observe_lit(true);
    }
    void make_oit_sets(Bundle& b) {
        if(b.oit_pool||!b.volume_snapshot||!b.oit_calls.empty())throw std::runtime_error("Mixed OIT descriptors already constructed");
        const auto count=static_cast<std::uint32_t>(b.assets->asset->materials.size()+1);
        const std::array<VkDescriptorPoolSize,4> sizes={{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,4*count},{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2*count},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,3*count},{VK_DESCRIPTOR_TYPE_SAMPLER,2*count}}};
        VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.maxSets=count;pool.poolSizeCount=sizes.size();pool.pPoolSizes=sizes.data();
        checked(F(vkCreateDescriptorPool)(context.device,&pool,nullptr,&b.oit_pool),"Mixed OIT descriptor pool");
        std::vector<VkDescriptorSetLayout> layouts(count,oit_set);std::vector<VkDescriptorSet> sets(count);
        VkDescriptorSetAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;allocation.descriptorPool=b.oit_pool;allocation.descriptorSetCount=count;allocation.pSetLayouts=layouts.data();
        checked(F(vkAllocateDescriptorSets)(context.device,&allocation,sets.data()),"Mixed OIT descriptor allocation");
        const auto outputs=b.volume_snapshot->outputs();
        for(unsigned i=0;i<count;++i){
            const std::array<VkDescriptorBufferInfo,6> buffers={{{b.frame->handle,0,sizeof(NativePreparedFrame)},{b.materials->handle,b.material_stride*i,sizeof(NativeMaterial)},
                vertex_binding(b),index_binding(b),
                {b.frame->handle,native_oit_constants_offset,2320},{b.frame->handle,native_oit_forward_offset,96}}};
            const std::array<VkDescriptorImageInfo,5> images={{{VK_NULL_HANDLE,b.assets->images[b.assets->image_for_material[i]]->view,VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL},
                {b.assets->samplers[i],VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED},outputs[0],outputs[1],{volume_sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED}}};
            std::array<VkWriteDescriptorSet,11> writes{};
            for(unsigned j=0;j<11;++j){auto& w=writes[j];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=sets[i];w.dstBinding=j;w.descriptorCount=1;
                if(j==1||j==2||j>=8){w.descriptorType=(j==2||j==10)?VK_DESCRIPTOR_TYPE_SAMPLER:VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;w.pImageInfo=&images[j==1?0:j==2?1:j-6];}
                else {w.descriptorType=(j==4||j==5)?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;w.pBufferInfo=&buffers[j==0?0:j==3?1:j-2];}
            }
            F(vkUpdateDescriptorSets)(context.device,writes.size(),writes.data(),0,nullptr);
        }
        for(const auto index:b.material_plan.blend_draws){const auto& draw=b.pose->geometry->draws[index];const auto material=draw.material<0?count-1:static_cast<unsigned>(draw.material);b.oit_calls.push_back({sets.at(material),draw.first_index,draw.index_count});}
    }
    void mixed_end(Recording& r,VkCommandBuffer command) {
        if(!r.bundle||!r.bundle->material_plan.mixed||!r.bundle->oit_window.active)return;
        auto& b=*r.bundle;bool inserted=false;
        const auto bound=metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS);
        const auto kind=bound.valid?native_oit_late_shader(bound.pipeline):NativeOitShader::unsupported;
        const auto restore=bound.valid?compatible(bound):nullptr;
        b.oit_raster=metadata.raster(command);
        if(r.eligible&&!r.tainted&&!r.native_restore&&!b.invalid&&!context.uncertain&&b.oit_window.can_insert()&&
           bound.pipeline.oit_compatible&&(kind==NativeOitShader::lit_diffuse||kind==NativeOitShader::lit_shaded||kind==NativeOitShader::unlit)&&restore&&mixed_current(b)){
            make_oit_sets(b);b.graphics_restore.push_back(restore);
            inserted=oit_pipelines->record(command,b.oit_calls.data(),static_cast<std::uint32_t>(b.oit_calls.size()),bound.pipeline.pipeline,restore->handle,static_cast<std::uint32_t>(bound.sets.size()),bound.sets.data());
        }
        b.oit_window.end(inserted);if(!inserted){b.invalid=true;mixed_notice("mixed final OIT insertion declined","mixed-insertion-declined");}
        else mixed_notice("mixed private BLEND batch recorded under shared common commit","mixed-insertion-ready");
    }
