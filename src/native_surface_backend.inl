// Included inside NativeBackend::Impl. All resource changes use its mutex.
    static const AvatarAppearance& fast_surface_appearance(){static const auto value=[](){AvatarAppearance a;a.whole=AvatarShaderPreset::Fast;return a;}();return value;}
    struct SurfaceUpload {
        std::string subject;
        std::uint64_t assignment=0,ticket=0;
        std::shared_ptr<const vrm::Avatar> avatar;
        std::shared_ptr<const NativeSkinAsset> skin;
        NativeSurfaceSelection key;
    } surface_upload;
    struct SurfaceFailure {
        std::uint64_t assignment=0;
        std::weak_ptr<const vrm::Avatar> avatar;
        std::weak_ptr<const NativeSkinAsset> skin;
        NativeSurfaceSelection key;
    };
    std::map<std::string,SurfaceFailure> surface_failures;
    std::uint64_t surface_ticket=0;
    std::shared_ptr<NativeAvatarGpuAssets>* surface_target(const NativeAvatarPublication& p) {
        if(!players_managed)return p.subject.empty()&&assets&&assets->avatar()==p.asset?&assets:nullptr;
        const auto found=player_models.find(p.subject);
        return found!=player_models.end()&&player_current(p)&&found->second.assets&&found->second.assets->avatar()==p.asset?&found->second.assets:nullptr;
    }
    void refresh_surface_assets()noexcept {
        try{
            if(!latest||!queue||context.uncertain)return;
            std::vector<std::shared_ptr<const NativeAvatarPublication>> current;
            const auto add=[&](const std::shared_ptr<const NativeAvatarPublication>& p){
                if(!p||!surface_target(*p)||std::any_of(current.begin(),current.end(),[&](const auto& old){return old->subject==p->subject;}))return;
                if(current.size()<16)current.push_back(p);
            };
            const auto view=[&](const std::shared_ptr<const NativeAvatarPublication>& p){if(!p)return;add(p);for(const auto& s:p->additional_subjects)add(s);};
            view(latest);for(const auto& p:latest->additional_views)view(p);
            const auto withdraw_failure=[&](auto it){
                // The outer weak latch is the retry policy. Withdraw the lower
                // strong failure references too, without cancelling a different
                // actor's submitted optional upload.
                if(surface_stager&&!surface_upload.ticket)surface_stager->cancel();
                return surface_failures.erase(it);
            };
            for(auto it=surface_failures.begin();it!=surface_failures.end();)if(it->second.avatar.expired()||
                std::none_of(current.begin(),current.end(),[&](const auto& p){return p->subject==it->first;}))it=withdraw_failure(it);else ++it;
            // Exactly one pending optional upload. It can never cancel or
            // replace the model-selection upload/activation transaction.
            if(surface_upload.ticket){
                const auto found=std::find_if(current.begin(),current.end(),[&](const auto& p){return p->subject==surface_upload.subject&&
                    p->assignment_generation==surface_upload.assignment&&p->asset==surface_upload.avatar&&
                    (p->skinning?p->skinning->asset:nullptr)==surface_upload.skin&&
                    make_native_surface_selection(*p->asset,p->appearance.get())==surface_upload.key;});
                if(found==current.end()){
                    surface_stager->cancel();
                    if(surface_stager->update(queue,family,0,{})==NativeAvatarUploadState::Pending)return;
                    surface_upload={};
                }else{
                    auto* target=surface_target(**found);if(!target)return;
                    const auto state=surface_stager->update(queue,family,surface_upload.ticket,surface_upload.avatar,surface_upload.skin,surface_upload.key,*target);
                    if(state==NativeAvatarUploadState::Pending)return;
                    if(state==NativeAvatarUploadState::Ready){auto ready=surface_stager->take_ready(surface_upload.ticket,surface_upload.avatar,surface_upload.key);
                        if(ready)*target=std::move(ready);
                    }else if(state==NativeAvatarUploadState::Failed){
                        surface_failures[surface_upload.subject]={surface_upload.assignment,surface_upload.avatar,surface_upload.skin,surface_upload.key};
                        try{report(std::string("Enhanced material upload unavailable; keeping current avatar: ")+surface_stager->error());}catch(...){}
                    }
                    surface_upload={};
                }
            }
            for(const auto& p:current){
                auto* target=surface_target(*p);if(!target||!*target)continue;
                const auto key=make_native_surface_selection(*p->asset,p->appearance.get());
                const auto skin=p->skinning?p->skinning->asset:nullptr;
                auto failed=surface_failures.find(p->subject);
                // Withdrawal must precede the already-ready fast path. A new
                // assignment is an explicit retry even for the same model/key.
                if(failed!=surface_failures.end()&&(failed->second.assignment!=p->assignment_generation||failed->second.avatar.lock()!=p->asset||failed->second.skin.lock()!=skin||!(failed->second.key==key))){
                    withdraw_failure(failed);failed=surface_failures.end();
                }
                if((*target)->surface_selection()==key)continue;
                if(failed!=surface_failures.end())continue;
                bool reused=false;
                for(const auto& other:current){auto* shared=surface_target(*other);
                    if(shared&&*shared&&other->asset==p->asset&&(other->skinning?other->skinning->asset:nullptr)==skin&&(*shared)->surface_selection()==key){*target=*shared;reused=true;break;}}
                if(reused)continue;
                if(surface_ticket==UINT64_MAX)return;
                ensure_asset_stager();
                if(!surface_stager)surface_stager=std::make_unique<NativeAvatarAssetStager>(context.device,context.get,set_loader,context.memory,limits.maxImageDimension2D,skinning_gpu,&context.uncertain);
                surface_upload={p->subject,p->assignment_generation,++surface_ticket,p->asset,skin,key};
                const auto state=surface_stager->update(queue,family,surface_upload.ticket,p->asset,skin,key,*target);
                if(state==NativeAvatarUploadState::Failed){surface_failures[p->subject]={p->assignment_generation,p->asset,skin,key};surface_upload={};}
                return;
            }
        }catch(...){/* Optional effects never revoke the current VRM. */}
    }
    VkDescriptorSetLayout make_surface_set(bool oit) {
        std::vector<VkDescriptorSetLayoutBinding> bindings;
        for(unsigned i=0;i<19;++i){
            if(!oit&&i>=6&&i<=10)continue;
            VkDescriptorSetLayoutBinding b{};b.binding=i;b.descriptorCount=1;
            b.descriptorType=(i==0||i==3||i==6||i==7||i==12)?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:
                (i==1||i==8||i==9||i==13||i==15||i==17)?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:
                (i==2||i==10||i==14||i==16||i==18)?VK_DESCRIPTOR_TYPE_SAMPLER:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            b.stageFlags=(i==4||i==5||i==11)?VK_SHADER_STAGE_VERTEX_BIT:
                (i==0||i==3||i==12)?VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_FRAGMENT_BIT;
            bindings.push_back(b);
        }
        VkDescriptorSetLayoutCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        info.bindingCount=bindings.size();info.pBindings=bindings.data();VkDescriptorSetLayout out{};
        checked(F(vkCreateDescriptorSetLayout)(context.device,&info,nullptr,&out),"Enhanced material set layout");return out;
    }
    VkPipelineLayout make_surface_layout(VkDescriptorSetLayout set) {
        VkPipelineLayoutCreateInfo info{};info.sType=VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;info.setLayoutCount=1;info.pSetLayouts=&set;
        VkPipelineLayout out{};checked(F(vkCreatePipelineLayout)(context.device,&info,nullptr,&out),"Enhanced material pipeline layout");return out;
    }
    bool initialize_surface(bool menu,bool mixed)noexcept {
        try{
            if(surface_failed)return false;
            if(!surface_shaders[3]){
                surface_failed=true;
                if(limits.maxPerStageDescriptorStorageBuffers<3||limits.maxDescriptorSetStorageBuffers<3||
                   limits.maxPerStageDescriptorUniformBuffers<3||limits.maxDescriptorSetUniformBuffers<3||
                   limits.maxPerStageDescriptorSampledImages<4||limits.maxDescriptorSetSampledImages<4||
                   limits.maxPerStageDescriptorSamplers<4||limits.maxDescriptorSetSamplers<4)
                    throw std::runtime_error("Enhanced material descriptor limits unavailable");
                surface_set=make_surface_set(false);surface_layout=make_surface_layout(surface_set);
                surface_shaders[0]=shader("avatar_native.surface.vert.spv");surface_shaders[1]=shader("avatar_native.surface.gbuffer.spv");
                surface_shaders[3]=shader("native_prepare.surface.comp.spv");
                if(!draw_pipelines->install_surface_variant(surface_layout,surface_shaders[0],surface_shaders[1]))
                    throw std::runtime_error("Enhanced material graphics pipeline unavailable");
                surface_failed=false;
            }
            const bool world_ready=poll_prepare(NativePrepareWorker::Key::SurfaceWorld,surface_shaders[3],surface_prepare,surface_failed);
            bool menu_ready=true;
            if(menu){
                if(!surface_shaders[4]&&!surface_menu_failed){
                    surface_menu_failed=true;
                    surface_shaders[4]=shader("native_menu_prepare.surface.comp.spv");surface_menu_failed=false;
                }
                menu_ready=poll_prepare(NativePrepareWorker::Key::SurfaceMenu,surface_shaders[4],surface_menu_prepare,surface_menu_failed);
            }
            if(!world_ready||!menu_ready)return false;
            if(mixed&&(!oit_pipelines||!oit_pipelines->surface_ready())){
                if(surface_oit_failed)return false;
                surface_oit_failed=true;
                if(!oit_pipelines||limits.maxPerStageDescriptorUniformBuffers<5||limits.maxDescriptorSetUniformBuffers<5||
                   limits.maxPerStageDescriptorSampledImages<6||limits.maxDescriptorSetSampledImages<6||
                   limits.maxPerStageDescriptorSamplers<5||limits.maxDescriptorSetSamplers<5)
                    throw std::runtime_error("Enhanced transparent material descriptor limits unavailable");
                surface_oit_set=make_surface_set(true);surface_oit_layout=make_surface_layout(surface_oit_set);
                surface_shaders[2]=shader("avatar_native.surface.oit.spv");
                if(!oit_pipelines->install_surface_variant(surface_oit_layout,surface_shaders[0],surface_shaders[2]))
                    throw std::runtime_error("Enhanced transparent material pipeline unavailable");
                surface_oit_failed=false;
            }
            return true;
        }catch(const std::exception& error){try{report(std::string("Enhanced materials unavailable; using Fast: ")+error.what());}catch(...){}return false;}
        catch(...){return false;}
    }
    VkDescriptorBufferInfo surface_vertex_binding(const Bundle& b)const {
        if(b.surface_vertices)return {b.surface_vertices->handle,0,b.surface_vertices->size};
        if(b.surface.needs_tangents()&&b.skin)return b.skin->geometry()->surface_binding();
        if(b.surface.needs_vertices())return b.assets->surface_vertices();
        // Matcap/factor-only shaders do not read this valid placeholder.
        return vertex_binding(b);
    }
    void make_surface_uniforms(Bundle& b) {
        if(!b.surface.any())return;
        const auto alignment=std::max<VkDeviceSize>(16,limits.minUniformBufferOffsetAlignment);
        b.surface_stride=(sizeof(NativeSurfaceMaterial)+alignment-1)/alignment*alignment;
        b.surface_materials=make_buffer(context,b.surface_stride*b.surface.features.size(),VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);
        for(std::size_t i=0;i<b.pose->asset->materials.size();++i){
            const auto value=native_surface_material(b.pose->asset->materials[i],b.surface.features[i]);
            std::memcpy(static_cast<unsigned char*>(b.surface_materials->mapped)+b.surface_stride*i,&value,sizeof value);
        }
        if(b.surface.needs_vertices()&&!b.pose->skinning){
            const auto& stream=b.pose->geometry->surface_vertices;
            if(stream.size()!=b.pose->geometry->vertices.size())throw std::runtime_error("Enhanced CPU pose stream unavailable");
            b.surface_vertices=make_buffer(context,stream.size()*sizeof(vrm::SurfaceVertex),VK_BUFFER_USAGE_STORAGE_BUFFER_BIT);
            if(b.surface_vertices->size>limits.maxStorageBufferRange)throw std::runtime_error("Enhanced CPU pose stream exceeds device range");
            std::memcpy(b.surface_vertices->mapped,stream.data(),b.surface_vertices->size);
        }
    }
    std::vector<VkDescriptorSet> make_surface_sets(Bundle& b,const std::vector<VkDescriptorSet>& basic,bool oit) {
        std::vector<VkDescriptorSet> out(basic.size());if(!b.surface.any())return out;
        const auto count=static_cast<std::uint32_t>(basic.size());
        if(!count||count!=b.surface.features.size()||!b.surface_materials)throw std::runtime_error("Enhanced material descriptor shape");
        if(!b.surface_pool){
            const std::array<VkDescriptorPoolSize,4> sizes={{{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,8*count},
                {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,6*count},{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE,10*count},{VK_DESCRIPTOR_TYPE_SAMPLER,9*count}}};
            VkDescriptorPoolCreateInfo pool{};pool.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.maxSets=2*count;
            pool.poolSizeCount=sizes.size();pool.pPoolSizes=sizes.data();
            checked(F(vkCreateDescriptorPool)(context.device,&pool,nullptr,&b.surface_pool),"Enhanced material descriptor pool");
        }
        const auto vertex=surface_vertex_binding(b);if(!vertex.buffer||!vertex.range||vertex.range==VK_WHOLE_SIZE)throw std::runtime_error("Enhanced surface binding unavailable");
        for(std::size_t i=0;i<out.size();++i){
            if(!b.surface.features[i])continue;
            const auto layout=oit?surface_oit_set:surface_set;
            VkDescriptorSetAllocateInfo allocation{};allocation.sType=VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
            allocation.descriptorPool=b.surface_pool;allocation.descriptorSetCount=1;allocation.pSetLayouts=&layout;
            checked(F(vkAllocateDescriptorSets)(context.device,&allocation,&out[i]),"Enhanced material descriptor allocation");
            std::array<VkCopyDescriptorSet,11> copies{};const auto copy_count=oit?11u:6u;
            for(unsigned j=0;j<copy_count;++j){auto& copy=copies[j];copy.sType=VK_STRUCTURE_TYPE_COPY_DESCRIPTOR_SET;
                copy.srcSet=basic[i];copy.dstSet=out[i];copy.srcBinding=copy.dstBinding=j;copy.descriptorCount=1;}
            const std::array<VkDescriptorBufferInfo,2> buffers={vertex,{b.surface_materials->handle,b.surface_stride*i,sizeof(NativeSurfaceMaterial)}};
            std::array<VkDescriptorImageInfo,6> images{};
            for(unsigned slot=0;slot<3;++slot){const auto sampled=b.assets->surface_material(i,static_cast<NativeSurfaceTexture>(slot));
                images[slot*2]={VK_NULL_HANDLE,sampled.imageView,sampled.imageLayout};images[slot*2+1]={sampled.sampler,VK_NULL_HANDLE,VK_IMAGE_LAYOUT_UNDEFINED};}
            std::array<VkWriteDescriptorSet,8> writes{};
            for(unsigned j=0;j<writes.size();++j){auto& w=writes[j];w.sType=VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;w.dstSet=out[i];w.dstBinding=11+j;w.descriptorCount=1;
                w.descriptorType=j==0?VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:j==1?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:j%2==0?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_SAMPLER;
                if(j<2)w.pBufferInfo=&buffers[j];else w.pImageInfo=&images[j-2];}
            // Copy before adding the optional bindings; every source is a
            // private immutable set retained by this same command bundle.
            F(vkUpdateDescriptorSets)(context.device,0,nullptr,copy_count,copies.data());
            F(vkUpdateDescriptorSets)(context.device,writes.size(),writes.data(),0,nullptr);
        }
        return out;
    }
    void extend_surface_bounds(Bundle& b)const noexcept {
        if(!b.surface.any()||!b.material_plan.mixed)return;
        for(const auto index:b.material_plan.blend_draws){const auto material=b.pose->geometry->draws[index].material;
            if(material<0||static_cast<std::size_t>(material)>=b.pose->asset->materials.size())continue;
            const auto& source=b.pose->asset->materials[material];const auto bound=native_surface_bounds(source,b.surface.features[material]);
            for(unsigned axis=0;axis<3;++axis){
                b.material_plan.base_max[axis]=std::max(b.material_plan.base_max[axis],std::abs(source.base_color[axis])+bound.lit[axis]);
                const auto base=native_avatar_material(source,0,0,false);
                b.material_plan.emissive_max[axis]=std::max(b.material_plan.emissive_max[axis],base.emissive[axis]+bound.unlit[axis]);
            }
        }
    }

    void make_skin_frame(Bundle& b){
        if(!b.pose->skinning)return;
        const auto frame=[&](bool surface){return skinning_gpu->frame(b.assets->skin(),*b.pose->skinning,{b.frame->handle,0,b.frame->size},b.previous?b.previous->geometry:nullptr,b.pose->mouth_weights,surface);};
        if(b.surface.needs_tangents()){
            try{b.skin=frame(true);return;}catch(...){
                // Construction has recorded no commands. Preserve all ordinary
                // ownership/permit state; the Basic attempt rechecks quarantine.
                b.surface=make_native_surface_selection(*b.pose->asset,&fast_surface_appearance());
                b.surface_materials.reset();b.surface_vertices.reset();
            }
        }
        b.skin=frame(false);
    }
