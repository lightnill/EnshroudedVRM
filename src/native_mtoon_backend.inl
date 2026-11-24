
    // Original enabled feature observation is copied, never caller pointers or
    // an invented enabling request. These source-free jobs carry no frame use.
    DeviceFeaturesObservation mtoon_enabled;std::uint32_t mtoon_api_version=0;
    NativeMToonRasterProgramStatus mtoon_raster_menu_state=NativeMToonRasterProgramStatus::unsupported;
    bool mtoon_raster_shader_attempted=false;
    std::unique_ptr<NativeMToonRasterProgramCache> mtoon_raster_programs;
    bool mtoon_outlines_requested()noexcept{
        if(!latest||context.uncertain)return false;unsigned examined=0;
        const auto selected=[&](const std::shared_ptr<const NativeAvatarPublication>& p){
            if(!p||++examined>64)return false;const auto* target=surface_target(*p);
            if(!target||!*target||!(*target)->mtoon()||!native_mtoon_request_matches((*target)->mtoon_request(),p->asset,p->appearance.get()))return false;
            for(const auto& material:(*target)->mtoon_request()->plan().materials)if(material.status==MToonResourceStatus::prepared_cpu&&material.outline)return true;return false;
        };
        const auto view=[&](const std::shared_ptr<const NativeAvatarPublication>& p){if(!p)return false;if(selected(p))return true;for(const auto& actor:p->additional_subjects){if(examined>=64)break;if(selected(actor))return true;}return false;};
        if(view(latest))return true;for(const auto& other:latest->additional_views){if(examined>=64)break;if(view(other))return true;}return false;
    }
    NativeMToonRasterPreparedProgram prepare_mtoon_raster_program()noexcept{
        if(context.uncertain||!mtoon_scene_requested()||mtoon_api_version<VK_API_VERSION_1_2||
            !native_rendering_environment_supported(mtoon_api_version,mtoon_enabled)||!native_mtoon_fragment_identity_supported(mtoon_enabled))return {};
        try{
            if(!mtoon_raster_programs){
                if(mtoon_raster_shader_attempted)return {};mtoon_raster_shader_attempted=true;
                const auto load=[&](const std::string& name,bool required){std::vector<std::uint32_t> words;std::ifstream in(shader_directory/name,std::ios::binary|std::ios::ate);
                    if(!in){if(required)throw std::runtime_error("Native MToon raster shader missing");return words;}const auto bytes=in.tellg();
                    if(bytes<20||bytes>1024*1024||bytes%4!=0)throw std::runtime_error("Native MToon raster shader size");words.resize(std::size_t(bytes)/4);in.seekg(0);in.read(reinterpret_cast<char*>(words.data()),bytes);if(!in||words[0]!=0x07230203)throw std::runtime_error("Native MToon raster shader data");return words;};
                NativeMToonRasterProgramShaders code;constexpr const char* families[]{"mtoon","mtoon-fallback","mtoon-ordinary","mtoon-enhanced"};constexpr const char* stages[]{"vert","visibility","gbuffer"};
                for(unsigned family=0;family<4;++family)for(unsigned stage=0;stage<3;++stage)code.main[family][stage]=load(std::string("avatar_native.")+families[family]+"."+stages[stage]+".spv",true);
                code.forward[0]=load("avatar_native.mtoon-forward.vert.spv",false);code.forward[1]=load("avatar_native.mtoon-forward.capture.spv",false);
                constexpr const char* shadow_stages[]{"vert","frag"};for(unsigned stage=0;stage<2;++stage)code.shadow[stage]=load(std::string("avatar_native.mtoon-shadow.")+shadow_stages[stage]+".spv",true);
                for(unsigned stage=0;stage<3;++stage)code.outline[stage]=load(std::string("avatar_native.mtoon-outline.")+stages[stage]+".spv",false);
                constexpr const char* work[]{"init","bounds","arguments","finish"};for(unsigned stage=0;stage<4;++stage)code.work[stage]=load(std::string("native_mtoon_work.")+work[stage]+".comp.spv",true);
                NativeMToonRasterProgramCapabilities caps;caps.api_version=mtoon_api_version;caps.enabled=mtoon_enabled;caps.limits=limits;
                mtoon_raster_programs=std::make_unique<NativeMToonRasterProgramCache>(context.device,context.get,caps,std::move(code),&context.uncertain);
            }
            auto prepared=mtoon_raster_programs->request(mtoon_outlines_requested(),true);mtoon_raster_menu_state=prepared.status;return prepared;
        }catch(...){return {};}
    }
// Included inside NativeBackend::Impl, under its native source mutex.
    bool mtoon_frame_shader_failed=false;
    // Same proven rich frame data as Enhanced: camera-relative offset and
    // exposure/emissive fields. Preparing it does not select raster/shading,
    // create Enhanced graphics or grant scene/source/common-commit authority.
    bool initialize_mtoon_frame_prepare(bool frontend=false)noexcept {
        if(!prepare_layout||mtoon_frame_shader_failed||surface_prepare_failed||context.uncertain)return false;
        try{
            if(frontend){if(surface_menu_failed)return false;
                if(!surface_shaders[4])surface_shaders[4]=shader("native_menu_prepare.surface.comp.spv");
                return poll_prepare(NativePrepareWorker::Key::SurfaceMenu,surface_shaders[4],surface_menu_prepare,surface_menu_failed);}
            if(!surface_shaders[3])surface_shaders[3]=shader("native_prepare.surface.comp.spv");
            return poll_prepare(NativePrepareWorker::Key::SurfaceWorld,surface_shaders[3],surface_prepare,surface_prepare_failed);
        }catch(...){mtoon_frame_shader_failed=true;return false;}
    }
    NativeMToonShadeProgramStatus mtoon_shade_menu_state=NativeMToonShadeProgramStatus::unsupported;
    bool mtoon_shade_shader_attempted=false;
    std::unique_ptr<NativeMToonShadeProgramCache> mtoon_shade_programs;
    NativeMToonShadePreparedProgram prepare_mtoon_shade_program()noexcept {
        if(!partially_bound||context.uncertain||!mtoon_scene_requested())return {};
        try{
            if(!mtoon_shade_programs){
                if(mtoon_shade_shader_attempted)return {};mtoon_shade_shader_attempted=true;
                std::ifstream input(shader_directory/"native_mtoon_shade.comp.spv",std::ios::binary|std::ios::ate);
                if(!input)return {};const auto size=input.tellg();if(size<20||size>1024*1024||size%4)return {};
                std::vector<std::uint32_t> words(static_cast<std::size_t>(size)/4);input.seekg(0);input.read(reinterpret_cast<char*>(words.data()),size);
                if(!input||words[0]!=0x07230203)return {};
                NativeMToonShadeCapabilities caps;caps.limits=limits;caps.descriptor_binding_partially_bound_enabled=partially_bound;
                mtoon_shade_programs=std::make_unique<NativeMToonShadeProgramCache>(context.device,context.get,caps,std::move(words),&context.uncertain);
            }
            auto prepared=mtoon_shade_programs->request();mtoon_shade_menu_state=prepared.status;return prepared;
        }catch(...){return {NativeMToonShadeProgramStatus::failed,{}};}
    }
    NativeMToonShadeProgramStatus mtoon_forward_shade_menu_state=NativeMToonShadeProgramStatus::unsupported;
    bool mtoon_forward_shade_shader_attempted=false;
    std::unique_ptr<NativeMToonShadeProgramCache> mtoon_forward_shade_programs;
    NativeMToonShadePreparedProgram prepare_mtoon_forward_shade_program()noexcept {
        if(!partially_bound||context.uncertain||!mtoon_scene_requested())return {};
        try{
            if(!mtoon_forward_shade_programs){
                if(mtoon_forward_shade_shader_attempted)return {};mtoon_forward_shade_shader_attempted=true;
                std::ifstream input(shader_directory/"native_mtoon_forward_shade.comp.spv",std::ios::binary|std::ios::ate);
                if(!input)return {};const auto size=input.tellg();if(size<20||size>1024*1024||size%4)return {};
                std::vector<std::uint32_t> words(static_cast<std::size_t>(size)/4);input.seekg(0);input.read(reinterpret_cast<char*>(words.data()),size);
                if(!input||words[0]!=0x07230203)return {};
                NativeMToonShadeCapabilities caps;caps.limits=limits;caps.forward=true;caps.descriptor_binding_partially_bound_enabled=partially_bound;
                mtoon_forward_shade_programs=std::make_unique<NativeMToonShadeProgramCache>(context.device,context.get,caps,std::move(words),&context.uncertain);
            }
            auto prepared=mtoon_forward_shade_programs->request();mtoon_forward_shade_menu_state=prepared.status;return prepared;
        }catch(...){return {NativeMToonShadeProgramStatus::failed,{}};}
    }
    NativeMToonShadeProgramStatus mtoon_forward_finish_menu_state=NativeMToonShadeProgramStatus::unsupported;
    bool mtoon_forward_finish_shader_attempted=false;
    std::unique_ptr<NativeMToonShadeProgramCache> mtoon_forward_finish_programs;
    NativeMToonShadePreparedProgram prepare_mtoon_forward_finish_program()noexcept {
        if(!partially_bound||context.uncertain||!mtoon_scene_requested())return {};
        try{
            if(!mtoon_forward_finish_programs){
                if(mtoon_forward_finish_shader_attempted)return {};mtoon_forward_finish_shader_attempted=true;
                std::ifstream input(shader_directory/"native_mtoon_forward_finish.comp.spv",std::ios::binary|std::ios::ate);
                if(!input)return {};const auto size=input.tellg();if(size<20||size>1024*1024||size%4)return {};
                std::vector<std::uint32_t> words(static_cast<std::size_t>(size)/4);input.seekg(0);input.read(reinterpret_cast<char*>(words.data()),size);
                if(!input||words[0]!=0x07230203)return {};
                NativeMToonShadeCapabilities caps;caps.limits=limits;caps.forward=true;caps.descriptor_binding_partially_bound_enabled=partially_bound;
                mtoon_forward_finish_programs=std::make_unique<NativeMToonShadeProgramCache>(context.device,context.get,caps,std::move(words),&context.uncertain);
            }
            auto prepared=mtoon_forward_finish_programs->request();mtoon_forward_finish_menu_state=prepared.status;return prepared;
        }catch(...){return {NativeMToonShadeProgramStatus::failed,{}};}
    }
    bool mtoon_observe_requested=false,mtoon_probe_shader_attempted=false;
    std::unique_ptr<NativeMToonProbeProgramCache> mtoon_probe_programs;
    NativeMToonProbeCopyCapabilities mtoon_probe_caps;std::shared_ptr<NativeMToonProbeFrameReuse> mtoon_probe_reuse;
    std::uint64_t mtoon_probe_retry_after=0;
    void begin_mtoon_raster(Recording& r,VkCommandBuffer command,const VkRenderingInfo& info)noexcept {
        if(!mtoon_observe_requested||!r.eligible||r.tainted||!r.pass.supported||r.pass.kind!=NativePipelineKind::gbuffer)return;
        r.mtoon_scene={};r.mtoon_scene_backing={};r.mtoon_raster={};if(++r.mtoon_raster_passes!=1)return;
        try{
            if(info.sType!=VK_STRUCTURE_TYPE_RENDERING_INFO||info.pNext||info.flags||info.viewMask||info.layerCount!=1||
               info.colorAttachmentCount!=6||!info.pColorAttachments||!info.pDepthAttachment||info.pStencilAttachment||info.renderArea.offset.x||info.renderArea.offset.y)return;
            NativeMToonRasterView view;view.command=command;view.extent=info.renderArea.extent;
            for(unsigned n=0;n<4;++n)view.attachments[n]=metadata.scene_image_view(info.pColorAttachments[n].imageView);
            view.attachments[4]=metadata.scene_image_view(info.pDepthAttachment->imageView);
            for(const auto& a:view.attachments)if(!a)return;
            r.mtoon_raster=std::move(view);
        }catch(...){} // Optional observations never change original rendering.
    }
    void end_mtoon_raster(Recording& r)noexcept {
        if(r.mtoon_raster_passes!=1||!r.mtoon_raster.command||!r.pass.supported||r.pass.kind!=NativePipelineKind::gbuffer)return;
        bool preview=false;each_subject(r,[&](const Recording& subject){if(subject.bundle&&subject.bundle->menu_preview)preview=true;});
        r.mtoon_raster.preview=preview;
        r.mtoon_raster.frontend=r.bundle&&r.bundle->menu_preview&&r.bundle->menu_owner.kind==NativeMenuOwnerKind::frontend&&r.bundle->subjects.empty();
        r.mtoon_raster.completed=!r.tainted;
    }
    void observe_mtoon_scene(Recording& r,VkCommandBuffer command,std::uint32_t observed_family=UINT32_MAX)noexcept {
        if(!mtoon_observe_requested||!r.eligible||r.tainted||r.pass_active||context.uncertain)return;
        if(native_mtoon_probe_stage(metadata.observed_pipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE))!=NativeMToonProbeStage::deferred)return;
        r.mtoon_scene={};r.mtoon_scene_backing={};r.mtoon_scope_family=UINT32_MAX;r.mtoon_original_use_serial=0;if(++r.mtoon_lighting_passes!=1||!mtoon_scene_requested())return;
        r.mtoon_scope_family=observed_family;
        r.mtoon_scene=native_mtoon_observe_scene(metadata,command,r.mtoon_raster,r.mtoon_probe_graph);
        r.mtoon_backing_diagnostic={};if(r.mtoon_scene&&image_memory)r.mtoon_scene_backing=native_mtoon_scene_backing(r.mtoon_scene,memory,*image_memory,&r.mtoon_backing_diagnostic);
    }
    bool prepare_mtoon_probe_programs()noexcept {
        if(mtoon_probe_programs)return true;if(mtoon_probe_shader_attempted)return false;
        mtoon_probe_shader_attempted=true;
        try {
            const auto format=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(get_instance(instance,"vkGetPhysicalDeviceFormatProperties"));
            if(!format)return false;VkFormatProperties facts{};format(physical,VK_FORMAT_R16G16B16A16_SFLOAT,&facts);
            mtoon_probe_caps.limits=limits;mtoon_probe_caps.memory=context.memory;mtoon_probe_caps.coefficient_format_features=facts.optimalTilingFeatures;
            std::ifstream input(shader_directory/"native_mtoon_probe_frame.comp.spv",std::ios::binary|std::ios::ate);
            if(!input)return false;const auto size=input.tellg();if(size<20||size>1024*1024||size%4)return false;
            std::vector<std::uint32_t> words(static_cast<std::size_t>(size)/4);input.seekg(0);input.read(reinterpret_cast<char*>(words.data()),size);
            if(!input||words[0]!=0x07230203)return false;
            mtoon_probe_programs=std::make_unique<NativeMToonProbeProgramCache>(context.device,context.get,mtoon_probe_caps,std::move(words));return true;
        }catch(...){return false;}
    }
    bool mtoon_scene_requested()noexcept {
        if(!latest||context.uncertain)return false;
        unsigned examined=0;
        const auto selected=[&](const std::shared_ptr<const NativeAvatarPublication>& p){
            if(!p||++examined>64)return false;
            const auto* target=surface_target(*p);
            return target&&*target&&(*target)->mtoon()&&
                native_mtoon_request_matches((*target)->mtoon_request(),p->asset,p->appearance.get());
        };
        const auto view=[&](const std::shared_ptr<const NativeAvatarPublication>& p){
            if(!p)return false;if(selected(p))return true;
            for(const auto& subject:p->additional_subjects){if(examined>=64)break;if(selected(subject))return true;}return false;
        };
        if(view(latest))return true;
        for(const auto& other:latest->additional_views){if(examined>=64)break;if(view(other))return true;}
        return false;
    }
    void observe_mtoon_probe(Recording& recording,VkCommandBuffer command)noexcept {
        if(!recording.eligible||recording.tainted||context.uncertain)return;
        if(!mtoon_observe_requested){recording.mtoon_probe_graph.reset();return;}
        if(recording.mtoon_probe_graph&&recording.mtoon_probe_graph->failed())return;
        try {
            const auto pipeline=metadata.observed_pipeline(command,VK_PIPELINE_BIND_POINT_COMPUTE);
            const auto stage=native_mtoon_probe_stage(pipeline);
            if(stage!=NativeMToonProbeStage::classify&&stage!=NativeMToonProbeStage::converge&&stage!=NativeMToonProbeStage::update)return;
            if(!mtoon_scene_requested()){recording.mtoon_probe_graph.reset();return;}
            if(!recording.mtoon_probe_graph)recording.mtoon_probe_graph=std::make_shared<NativeMToonProbeGraph>(command);
            auto& graph=*recording.mtoon_probe_graph;
            if(stage==NativeMToonProbeStage::classify){
                if(graph.classified(command,native_mtoon_observe_probe_classify(metadata,command))&&image_memory)
                    recording.mtoon_probe_buffers=native_mtoon_probe_buffer_backing(recording.mtoon_probe_graph,memory,*image_memory);
            }
            else if(stage==NativeMToonProbeStage::converge){
                const auto candidate=native_mtoon_observe_probe_candidate(metadata,command);
                if(graph.converged(command,candidate)&&!recording.mtoon_probe_backing_attempted){
                    recording.mtoon_probe_backing_attempted=true;
                    if(image_memory&&native_mtoon_probe_candidate_current(candidate,metadata,command))recording.mtoon_probe_backing=native_mtoon_probe_graph_backing(recording.mtoon_probe_graph,memory,*image_memory,recording.mtoon_probe_buffers);
                    if(!recording.mtoon_probe_use)recording.mtoon_probe_use=std::make_unique<NativeMToonProbeUse>();recording.mtoon_probe_use->producer();
                }
            }
            else if(graph.updated(command,native_mtoon_observe_probe_update(metadata,command))&&image_memory&&
                recording.mtoon_probe_backing.updated(memory,*image_memory))record_mtoon_probe_snapshot(recording,command);
        }catch(...) {recording.mtoon_probe_graph.reset();}
    }
    void mtoon_probe_barriers(Recording& r,std::uint32_t buffers,const VkBufferMemoryBarrier* b,std::uint32_t images,const VkImageMemoryBarrier* i)noexcept {
        if(!mtoon_observe_requested||!r.eligible||r.tainted||r.mtoon_probe_snapshot||!image_memory)return;
        try{if(!r.mtoon_probe_use)r.mtoon_probe_use=std::make_unique<NativeMToonProbeUse>();r.mtoon_probe_use->legacy(*image_memory,buffers,b,images,i);}catch(...){if(r.mtoon_probe_use)r.mtoon_probe_use->unobserved();}
    }
    void mtoon_probe_barriers2(Recording& r,const VkDependencyInfo* info)noexcept {
        if(!mtoon_observe_requested||!r.eligible||r.tainted||r.mtoon_probe_snapshot||!image_memory)return;
        try{if(!r.mtoon_probe_use)r.mtoon_probe_use=std::make_unique<NativeMToonProbeUse>();r.mtoon_probe_use->sync2(*image_memory,info);}catch(...){if(r.mtoon_probe_use)r.mtoon_probe_use->unobserved();}
    }
    void record_mtoon_probe_snapshot(Recording& r,VkCommandBuffer command)noexcept {
        if(r.mtoon_probe_attempted||!r.mtoon_probe_graph||!r.mtoon_probe_graph->complete())return;
        r.mtoon_probe_attempted=true;
        try {
            if(!image_memory||!r.mtoon_probe_graph->current(metadata)||GetTickCount64()<mtoon_probe_retry_after)return;
            const auto& backing=r.mtoon_probe_backing;
            if(!backing||!backing.sources_current(metadata,memory,*image_memory))return;
            if(!prepare_mtoon_probe_programs())return;
            const auto& graph=*r.mtoon_probe_graph;
            NativeMToonProbeProgramShape shape{graph.convergence().metadata().layout.bindings,graph.classifier().layout.bindings};
            const auto prepared=mtoon_probe_programs->request(shape);
            if(prepared.status!=NativeMToonProbeProgramStatus::ready||!prepared.program||!prepared.program->matches(shape))return;
            unsigned retained=0;each_recording([&](VkCommandBuffer,const Recording& item){if(item.mtoon_probe_snapshot)++retained;});
            // At most eight original recordings, each with <=4MiB actual memory.
            if(retained>=8)return;
            std::array<VkImage,3> sources{};for(unsigned i=0;i<3;++i)sources[i]=graph.convergence().metadata().coefficients[i].image.image;
            if(!r.mtoon_probe_use||!r.mtoon_probe_use->general(sources,*image_memory))return;
            const auto restore=compatible(graph.update().bound);if(!restore)return;
            auto permit=lease_control_buffer(context,16,VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT);std::memset(permit->mapped,0,16);
            const auto source=graph.copy_source({permit->handle,0,16});
            if(!mtoon_probe_reuse){mtoon_probe_reuse=std::make_shared<NativeMToonProbeFrameReuse>();mtoon_probe_reuse->retain(private_reuse_enabled);}
            auto snapshot=std::make_shared<NativeMToonProbeFrame>(context.device,context.get,source,mtoon_probe_caps,prepared.program,&context.uncertain,mtoon_probe_reuse);
            const auto& original=graph.update().bound;
            if(!snapshot->record(command,original.pipeline.pipeline,restore->handle,static_cast<std::uint32_t>(original.sets.size()),original.sets.data()))return;
            r.mtoon_probe_permit=std::move(permit);r.mtoon_probe_snapshot=std::move(snapshot);r.mtoon_probe_restore=restore;r.mtoon_probe_layout_qualified=true;
        }catch(...) {mtoon_probe_retry_after=GetTickCount64()+1000;}
    }
    void arm_mtoon_probe(Recording& r,bool shape,VkQueue submitted_queue)noexcept {
        NativeSubmitStageScope arm(submit_timing,NativeSubmitStage::MToonProbeArm);
        if(!r.mtoon_probe_snapshot||!r.mtoon_probe_permit||r.mtoon_probe_armed)return;
        const auto sources_current=[&]{NativeSubmitStageScope current(submit_timing,NativeSubmitStage::MToonProbeCurrent);
            return r.mtoon_probe_backing.sources_current(metadata,memory,*image_memory);};
        const bool permit=r.mtoon_probe_layout_qualified&&shape&&r.eligible&&!r.tainted&&!r.pass_active&&!disabled&&!context.uncertain&&submitted_queue==queue&&
            mtoon_scene_requested()&&image_memory&&r.mtoon_probe_graph&&r.mtoon_probe_backing.for_graph(r.mtoon_probe_graph)&&
            sources_current();
        const std::array<std::uint32_t,4> words{permit?1u:0u,0,0,0};std::memcpy(r.mtoon_probe_permit->mapped,words.data(),16);
        r.mtoon_probe_queue=submitted_queue;r.mtoon_probe_armed=true;r.mtoon_probe_pending=true;
    }

    // Optional owned-buffer samples, never rendering/source admission. Four
    // recordings spaced two seconds apart; no extra GPU wait or allocation.
    unsigned mtoon_gpu_observations_recorded=0;
    std::uint64_t mtoon_gpu_observation_ready_since=0;
    MToonNumericConfig numeric_mtoon_config(std::uint64_t)noexcept{return {};}
    void save_numeric_mtoon_observation(const NativeMToonGpuObservation&)noexcept{}
    bool mtoon_observation_due(std::uint64_t now)const noexcept{
        return mtoon_gpu_observations_recorded<4&&!context.uncertain&&mtoon_gpu_observation_ready_since&&
            now>=mtoon_gpu_observation_ready_since&&now-mtoon_gpu_observation_ready_since>=2000;
    }
    void record_mtoon_observation(Recording& r,VkCommandBuffer command)noexcept{
        if(!r.mtoon_lighting_recorded||!r.mtoon_frame||r.mtoon_observation_recorded||context.uncertain)return;
        const auto now=GetTickCount64();if(!mtoon_gpu_observation_ready_since)mtoon_gpu_observation_ready_since=now;
        if(!mtoon_observation_due(now))return;
        if(r.mtoon_frame->record_observation(command)){r.mtoon_observation_recorded=true;++mtoon_gpu_observations_recorded;mtoon_gpu_observation_ready_since=now;}
    }
    void harvest_mtoon_observation(Recording& r,const char* completion)noexcept{
        if(!r.mtoon_observation_recorded||r.mtoon_observation_harvested||!r.mtoon_frame||!r.bundle||!r.mtoon_active||context.uncertain)return;
        bool submitted=true;each_bundle(r.bundle,[&](const Bundle& b){submitted=submitted&&b.submitted&&!b.awaiting_submit_result&&!b.failed_submit&&(!b.invalid||!b.data.permit[0])&&b.submitted_queue==queue;});
        if(!submitted)return;r.mtoon_observation_harvested=true;
        // Called only by the existing valid-original nonpending retirement or
        // successful device-idle path. Coherence/CPU approval is not completion.
        const auto d=r.mtoon_frame->observation_after_original_completion();
        save_numeric_mtoon_observation(d);
        const auto aa=r.mtoon_composite?r.mtoon_composite->observation_after_original_completion():NativeMToonCompositeObservation{};
        const bool aa_valid=aa.available&&aa.generation==d.generation;
        try{
            static constexpr const char* names[]{"owned-state","reconstruction","source-masks","scene-extent","depth-extent","position","water","scene-samples","view","radiance","points","finish","emission","packet"};
            std::string declines;for(unsigned n=0;n<d.shader_declines.size();++n)if(d.shader_declines[n]){if(!declines.empty())declines+=",";declines+=std::string(names[n])+":"+std::to_string(d.shader_declines[n]);}
            if(declines.empty())declines="none";
            static constexpr const char* water_names[]{"admission","dimensions","mapping","alpha-position","unprojection","distance","active-unqualified","modulation"};
            std::string water;for(unsigned n=0;n<d.water_declines.size();++n)if(d.water_declines[n]){if(!water.empty())water+=",";water+=std::string(water_names[n])+":"+std::to_string(d.water_declines[n]);}if(water.empty())water="none";
            static constexpr const char* point_names[]{"admission","cluster","decode","response","shadow-admission","shadow","accumulate","iteration"};
            std::string points;for(unsigned n=0;n<d.point_declines.size();++n)if(d.point_declines[n]){if(!points.empty())points+=",";points+=std::string(point_names[n])+":"+std::to_string(d.point_declines[n]);}if(points.empty())points="none";
            log_event("native-mtoon.gpu generation="+std::to_string(d.generation)+" available="+std::to_string(d.available)+
            " header="+std::to_string(d.header_valid)+" work="+std::to_string(d.work_valid)+" extent="+std::to_string(d.width)+","+std::to_string(d.height)+
            " draws="+std::to_string(d.work_draws)+" groups="+std::to_string(d.group_work)+" declined="+std::to_string(d.declined)+
            " selected-work="+std::to_string(d.selected_work)+" outline-work="+std::to_string(d.outline_work)+
            " sampled-identities="+std::to_string(d.identity_samples)+" selected-samples="+std::to_string(d.selected_samples)+" outline-samples="+std::to_string(d.outline_samples)+
            " matched-packets="+std::to_string(d.matching_packets)+" matched-outlines="+std::to_string(d.matching_outlines)+" malformed-packets="+std::to_string(d.malformed_packets)+
            " outline-coverage-samples="+std::to_string(d.outline_coverage_samples)+" outline-coverage-units="+std::to_string(d.outline_coverage_units)+
            " aa-available="+std::to_string(aa_valid)+" aa-generation="+std::to_string(aa.generation)+" aa-sampled="+std::to_string(aa.sampled)+" aa-eligible="+std::to_string(aa.eligible)+" aa-mixed="+std::to_string(aa.mixed)+" aa-outline-mixed="+std::to_string(aa.outline_mixed)+
            " forward-fragments="+std::to_string(d.forward_fragments)+" forward-colours="+std::to_string(d.forward_colours)+" forward-overflow="+std::to_string(d.forward_overflow)+
            " forward-samples="+std::to_string(d.forward_samples)+" forward-matches="+std::to_string(d.forward_matches)+" forward-declines="+std::to_string(d.forward_declines[1])+","+std::to_string(d.forward_declines[5])+","+std::to_string(d.forward_declines[6])+","+std::to_string(d.forward_declines[7])+","+std::to_string(d.forward_declines[9])+","+std::to_string(d.forward_declines[10])+
            " shader-declines={"+declines+"} water-reasons={"+water+"} water-facts="+std::to_string(d.water_facts)+" point-reasons={"+points+"} point-facts="+std::to_string(d.point_facts)+" completion="+completion);
            const auto tuple=[](const auto& values){std::string text;for(const auto value:values){if(!text.empty())text+=",";text+=std::to_string(value);}return text;};
            log_event("native-mtoon.forward-observation generation="+std::to_string(d.generation)+
                " present="+std::to_string(d.forward_present)+" capture-header="+std::to_string(d.forward_capture_header_valid)+" packet-header="+std::to_string(d.forward_packet_header_valid)+
                " active="+std::to_string(d.forward_active)+" capacity="+std::to_string(d.forward_capacity)+" prepared-mask="+std::to_string(d.forward_prepared_mask)+" enabled-mask="+std::to_string(d.forward_enabled_mask)+
                " draws="+std::to_string(d.forward_capture_draws)+","+std::to_string(d.forward_enabled_draws)+" shades="+std::to_string(d.forward_shade_controls)+","+std::to_string(d.forward_enabled_shades)+
                " raw-capture="+tuple(d.forward_capture_header)+" raw-packet="+tuple(d.forward_packet_header)+" finish-facts="+tuple(r.mtoon_forward_finish_facts)+" completion="+completion);
        }catch(...){}
    }

    // Bounded CPU-only diagnostics. These observe existing owners/flags and
    // never prepare a driver job, read GPU results or change any permit.
    std::uint64_t mtoon_diagnostic_last=0,mtoon_diagnostic_reports=0;
    std::string mtoon_frame_error,mtoon_composition_error;
    std::string mtoon_backing_diagnostic_snapshot(const Recording& r)const {
        if(!r.mtoon_scene||!image_memory)return "{}";
        const auto& why=r.mtoon_backing_diagnostic;const auto& scene=r.mtoon_scene.metadata();
        if((why.code==2||why.code==7)&&why.role<scene.views.size()){
            const auto d=image_memory->diagnostic(scene.views[why.role].identity.image);unsigned role_images=0,role_buffers=0;
            for(unsigned n=0;n<scene.views.size();++n)for(const auto image:d.conflicting_images)if(image&&image==scene.views[n].identity.image)role_images|=1u<<n;
            for(unsigned n=0;n<scene.buffers.size();++n)for(const auto buffer:d.conflicting_buffers)if(buffer&&buffer==scene.buffers[n].info.buffer)role_buffers|=1u<<n;
            return "{observed="+std::to_string(d.observed)+" disabled="+std::to_string(d.disabled)+","+std::to_string(unsigned(d.disable_reason))+
                " image="+std::to_string(d.image_known)+","+std::to_string(d.query_safe)+","+std::to_string(d.creation_flags)+","+std::to_string(unsigned(d.creation_extension))+
                " binding="+std::to_string(d.bind_attempted)+","+std::to_string(d.footprint_known)+","+std::to_string(d.offset)+","+std::to_string(d.bytes)+
                " requirements="+std::to_string(d.requirements_observed)+","+std::to_string(d.requirement_bytes)+","+std::to_string(d.requirement_alignment)+","+std::to_string(d.requirement_memory_types)+
                " allocation="+std::to_string(d.allocation_known)+","+std::to_string(d.allocation_ordinary)+","+std::to_string(d.allocation_uncertain)+","+std::to_string(d.allocation_bytes)+","+std::to_string(d.memory_type)+","+std::to_string(d.memory_properties)+
                " allocation-chain="+std::to_string(unsigned(d.allocation_extension))+","+std::to_string(unsigned(d.allocation_last_extension))+","+std::to_string(d.allocation_flags)+","+std::to_string(d.allocation_device_mask)+
                " generation-match="+std::to_string(d.bound_memory_generation&&d.bound_memory_generation==d.current_memory_generation)+
                " conflicts="+std::to_string(d.image_conflicts)+","+std::to_string(d.buffer_conflicts)+","+std::to_string(d.unknown_image_conflicts)+","+std::to_string(d.unknown_buffer_conflicts)+
                " role-aliases="+std::to_string(role_images)+","+std::to_string(role_buffers)+" granularity="+std::to_string(d.granularity)+"}";
        }
        return "{}";
    }
    std::string mtoon_diagnostic_snapshot(const Recording& r,bool shape,VkQueue submitted)const {
        unsigned authored=0,unqualified=0,casters=0,shadow_views=0;
        each_bundle(r.bundle,[&](const Bundle& b){authored+=unsigned(b.mtoon_caster_draws.size());
            unqualified+=!b.mtoon_caster_draws.empty()&&!b.mtoon_caster_qualified;casters+=b.mtoon_caster_views;shadow_views+=unsigned(b.shadow_views.size());});
        const auto& graph=r.mtoon_probe_graph;
        return "native-mtoon.state shape="+std::to_string(shape)+" queue="+std::to_string(submitted==queue)+
            " eligible="+std::to_string(r.eligible)+" tainted="+std::to_string(r.tainted)+
            " frame="+std::to_string(bool(r.mtoon_frame))+" active="+std::to_string(r.mtoon_active)+
            " programs="+std::to_string(unsigned(mtoon_raster_menu_state))+","+std::to_string(unsigned(mtoon_shade_menu_state))+","+std::to_string(unsigned(mtoon_composite_menu_state))+
            " forward-programs="+std::to_string(unsigned(mtoon_forward_shade_menu_state))+","+std::to_string(unsigned(mtoon_forward_finish_menu_state))+
            " forward-prepared="+std::to_string(r.mtoon_forward_prepared)+" forward-finished="+std::to_string(r.mtoon_forward_finished)+" forward-volume-at-deferred="+std::to_string(r.mtoon_forward_volume_at_deferred)+
            " rich-frame="+std::to_string(bool(surface_prepare))+" raster="+std::to_string(r.mtoon_raster_passes)+
            " raster-complete="+std::to_string(r.mtoon_raster.completed)+" visibility="+std::to_string(r.mtoon_visibility_recorded)+" gbuffer="+std::to_string(r.mtoon_gbuffer_recorded)+
            " frontend="+std::to_string(r.mtoon_raster.frontend)+" frontend-cube="+std::to_string(r.mtoon_raster.frontend&&r.mtoon_scene&&metadata.cube_initialized(r.mtoon_scene.metadata().views[3].identity,queue))+
            " cube-proof={"+(r.mtoon_raster.frontend&&r.mtoon_scene?metadata.cube_diagnostic(r.mtoon_scene.metadata().views[3].identity,queue):std::string("not-frontend"))+"}"+
            " probe="+std::to_string(bool(graph))+" probe-complete="+std::to_string(graph&&graph->complete())+
            " probe-failure="+std::to_string(graph?unsigned(graph->failed_stage()):0u)+","+std::to_string(graph?unsigned(graph->failure()):0u)+
            " updates="+std::to_string(graph?graph->update_count():0u)+" snapshot="+std::to_string(bool(r.mtoon_probe_snapshot))+
            " probe-layout="+std::to_string(r.mtoon_probe_layout_qualified)+" probe-armed="+std::to_string(r.mtoon_probe_armed)+
            " scene="+std::to_string(bool(r.mtoon_scene))+" scene-view="+std::to_string(bool(r.mtoon_scene_view))+
            " family="+std::to_string(r.mtoon_scope_family)+","+std::to_string(family)+
            " lighting="+std::to_string(r.mtoon_lighting_passes)+" attempted="+std::to_string(r.mtoon_lighting_attempted)+" recorded="+std::to_string(r.mtoon_lighting_recorded)+
            " authored-casters="+std::to_string(authored)+" caster-unqualified="+std::to_string(unqualified)+" caster-views="+std::to_string(casters)+","+std::to_string(shadow_views)+
            " slots="+std::to_string(mtoon_frame_pool?mtoon_frame_pool->retained_slots():0)+" frame-bytes="+std::to_string(mtoon_frame_pool?mtoon_frame_pool->allocated_bytes():0)+
            " checkpoint="+std::to_string(r.mtoon_composition_checkpoint)+" backing-reject="+std::to_string(r.mtoon_backing_diagnostic.code)+","+std::to_string(r.mtoon_backing_diagnostic.role)+
            " source-masks="+std::to_string(r.mtoon_scene_view.buffer_mask())+","+std::to_string(r.mtoon_scene_view.image_mask())+","+std::to_string(r.mtoon_scene_view.sampler_mask())+","+std::to_string(r.mtoon_scene_view.water_mask())+
            " backed-water="+std::to_string(r.mtoon_scene_backing.backed_water_inputs())+" backed-images="+std::to_string(r.mtoon_scene_backing.backed_images())+" fallback-facts="+std::to_string(r.mtoon_fallback_facts)+" backing-memory="+mtoon_backing_diagnostic_snapshot(r)+
            " frame-error={"+mtoon_frame_error+"} composition-error={"+mtoon_composition_error+"}";
    }
    bool mtoon_water_metadata_reported=false;
    void report_mtoon_diagnostics(const Recording& r,bool shape,VkQueue submitted)noexcept {
        if(!r.bundle||!mtoon_observe_requested)return;
        const auto now=GetTickCount64();if(mtoon_diagnostic_reports&&now>=mtoon_diagnostic_last&&now-mtoon_diagnostic_last<5000)return;
        if(!mtoon_scene_requested())return;
        try{
            if(r.mtoon_scene&&!mtoon_water_metadata_reported){const auto& f=r.mtoon_scene.metadata();
                for(unsigned n=0;n<4;++n){const auto& v=f.water.views[n];const auto& d=f.water.images[n];
                    log_event("native-mtoon.water role="+std::to_string(n)+" observed="+std::to_string(bool(d))+" metadata="+std::to_string(bool(v))+
                        " available="+std::to_string(bool(r.mtoon_scene.available_water_inputs()&(1u<<n)))+" backed="+std::to_string(bool(r.mtoon_scene_backing.backed_water_inputs()&(1u<<n)))+
                        " format="+std::to_string(unsigned(v.format))+" image-format="+std::to_string(unsigned(v.image_format))+" aspect="+std::to_string(v.range.aspectMask)+
                        " flags="+std::to_string(v.image_flags)+" usage="+std::to_string(v.image_usage)+","+std::to_string(v.view_usage)+" view="+std::to_string(unsigned(v.view_type))+
                        " extent="+std::to_string(v.extent.width)+","+std::to_string(v.extent.height)+","+std::to_string(v.extent.depth)+" mips-layers="+std::to_string(v.range.levelCount)+","+std::to_string(v.range.layerCount)+
                        " layout="+std::to_string(unsigned(d.info.imageLayout)));}
                mtoon_water_metadata_reported=true;
            }
            log_event(mtoon_diagnostic_snapshot(r,shape,submitted));mtoon_diagnostic_last=now;++mtoon_diagnostic_reports;
        }catch(...){}
    }
    // Optional GPU timestamps have no rendering/admission authority.
    bool mtoon_profile_caps_attempted=false;
    NativeMToonProfileConfig mtoon_profile_caps;
    unsigned mtoon_profile_attempts=0;
    std::uint64_t mtoon_profile_last=0;
    NativeMToonProfileConfig sampled_mtoon_profile(const Recording& r)noexcept{
        if(!profiling_requested||context.uncertain||!queue||family==UINT32_MAX||
           r.mtoon_profile_family_conflict||r.mtoon_profile_family!=family||mtoon_profile_attempts>=128)return {};
        if(!mtoon_profile_caps_attempted){
            mtoon_profile_caps_attempted=true;
            try{
                const auto properties=get_instance?reinterpret_cast<PFN_vkGetPhysicalDeviceQueueFamilyProperties>(
                    get_instance(instance,"vkGetPhysicalDeviceQueueFamilyProperties")):nullptr;
                if(properties&&physical){
                    std::uint32_t count=0;properties(physical,&count,nullptr);
                    if(count&&count<=64){
                        std::array<VkQueueFamilyProperties,64> rows{};auto written=count;properties(physical,&written,rows.data());
                        std::uint32_t after=0;properties(physical,&after,nullptr);
                        if(written==count&&after==count&&family<count&&
                           (rows[family].queueFlags&(VK_QUEUE_GRAPHICS_BIT|VK_QUEUE_COMPUTE_BIT))){
                            mtoon_profile_caps={true,family,family,rows[family].timestampValidBits,limits.timestampPeriod};
                            if(!mtoon_profile_caps.valid())mtoon_profile_caps={};
                        }
                    }
                }
                log_event("native-mtoon.profile-support supported="+std::to_string(mtoon_profile_caps.valid())+
                    " family="+std::to_string(family)+" bits="+std::to_string(mtoon_profile_caps.timestamp_valid_bits)+
                    " period-ns="+std::to_string(mtoon_profile_caps.timestamp_period_ns));
            }catch(...){mtoon_profile_caps={};}
        }
        if(!mtoon_profile_caps.valid()||mtoon_profile_caps.selected_family!=family)return {};
        const auto now=GetTickCount64();
        if(mtoon_profile_last&&(now<mtoon_profile_last||now-mtoon_profile_last<500))return {};
        mtoon_profile_last=now;++mtoon_profile_attempts;
        auto caps=mtoon_profile_caps;caps.observed_family=r.mtoon_profile_family;return caps;
    }
    void harvest_mtoon_profile(Recording& r,const char* completion)noexcept{
        if(!profiling_requested||r.mtoon_profile_harvested||!r.mtoon_frame||!r.bundle||!r.mtoon_active||context.uncertain)return;
        bool submitted=true;
        each_bundle(r.bundle,[&](const Bundle& b){submitted=submitted&&b.submitted&&!b.awaiting_submit_result&&
            !b.failed_submit&&(!b.invalid||!b.data.permit[0])&&b.submitted_queue==queue;});
        if(!submitted)return;r.mtoon_profile_harvested=true;
        const auto result=r.mtoon_frame->gpu_profile_after_original_completion();
        if(!result.requested)return;
        try{
            static constexpr const char* stages[]{"work","outline-visibility","outline-gbuffer","forward-capture",
                "opaque-shade","forward-shade","outline-shade","composite","forward-finish"};
            std::string text="native-mtoon.profile generation="+std::to_string(result.generation)+
                " enabled="+std::to_string(result.enabled)+" reset="+std::to_string(result.reset_recorded)+
                " read="+std::to_string(result.read_attempted)+" age-valid="+std::to_string(result.age_valid)+
                " available="+std::to_string(result.available)+" written-mask="+std::to_string(result.written_mask)+
                " valid-mask="+std::to_string(result.valid_mask)+" result="+std::to_string(result.query_result)+
                " family="+std::to_string(result.config.selected_family)+" bits="+std::to_string(result.config.timestamp_valid_bits)+
                " period-ns="+std::to_string(result.config.timestamp_period_ns)+" unit=ms";
            for(unsigned n=0;n<native_mtoon_profile_stages;++n)
                text+=" "+std::string(stages[n])+"="+((result.valid_mask&(1u<<n))?std::to_string(result.milliseconds[n]):"na");
            log_event(text+" completion="+completion);
        }catch(...){}
    }
    std::unique_ptr<NativeMToonFramePool> mtoon_frame_pool;
    const std::chrono::steady_clock::time_point mtoon_clock_start=std::chrono::steady_clock::now();
    bool prepare_mtoon_frame(Recording& r,VkCommandBuffer command)noexcept{
        if(r.mtoon_frame)return true;
        if(!r.eligible||r.tainted||!r.bundle||!r.pass.supported||r.pass.kind!=NativePipelineKind::visibility||context.uncertain)return false;
        try{
            bool requested=false,world=true;
            const bool frontend=r.bundle->menu_preview&&r.bundle->menu_owner.kind==NativeMenuOwnerKind::frontend&&r.bundle->subjects.empty();
            each_bundle(r.bundle,[&](const Bundle& b){world=world&&!b.menu_preview&&!b.invalid&&b.pose&&b.assets;
                requested=requested||(b.pose&&b.assets&&b.assets->mtoon()&&native_mtoon_request_matches(b.assets->mtoon_request(),b.pose->asset,b.pose->appearance.get()));});
            if((!world&&!frontend)||!requested||r.bundle->invalid)return false;const auto prepared=prepare_mtoon_raster_program();
            if(prepared.status!=NativeMToonRasterProgramStatus::ready||!prepared.program||!initialize_mtoon_frame_prepare(frontend))return false;
            std::vector<NativeMToonFrameDraw> draws;std::vector<std::pair<std::shared_ptr<SubmittedHistory>,std::vector<unsigned>>> emissions;
            struct Caster {std::shared_ptr<Bundle> bundle;std::vector<unsigned> authored;std::vector<NativeDrawCall> ordinary;std::array<std::uint64_t,64> materials{},outlines{};bool qualified=false;};
            std::vector<Caster> casters;unsigned forward_actor=0;
            const float seconds=std::chrono::duration<float>(std::chrono::steady_clock::now()-mtoon_clock_start).count();
            const auto add=[&](const std::shared_ptr<Bundle>& holder){auto& b=*holder;
                if(!b.pose->geometry||!b.frame||!b.materials||!b.material_stride||b.first_pass.extent.width!=r.pass.extent.width||b.first_pass.extent.height!=r.pass.extent.height)throw std::runtime_error("Native MToon view geometry/frame mismatch");
                const auto& geometry=*b.pose->geometry;auto order=b.material_plan.opaque_draws;
                order.insert(order.end(),b.material_plan.blend_draws.begin(),b.material_plan.blend_draws.end());
                b.mtoon_forward_actor=++forward_actor;b.mtoon_forward_material_bits.fill(0);
                Caster caster;caster.bundle=holder;caster.qualified=b.shadow_calls.size()==geometry.draws.size()&&b.shadow_views.empty()&&!b.shadow_probe_draws;
                std::vector<bool> authored_caster(geometry.draws.size(),false);
                const bool matching=b.assets->mtoon()&&native_mtoon_request_matches(b.assets->mtoon_request(),b.pose->asset,b.pose->appearance.get());
                const auto request=matching?b.assets->mtoon_request():std::shared_ptr<const NativeMToonRequest>{};
                const auto raster=make_native_mtoon_raster_plan(request,geometry,order,r.pass.extent.width,r.pass.extent.height,seconds);
                if(raster.status==NativeMToonRasterStatus::invalid)throw std::runtime_error("Native MToon draw/UV provenance mismatch");
                const auto material_count=b.materials->size/b.material_stride;std::vector<unsigned> outlined;
                for(unsigned n=0;n<order.size();++n){const auto index=order[n];if(index>=geometry.draws.size())throw std::runtime_error("Native MToon ordinary draw index");const auto& source=geometry.draws[index];
                    const auto material=source.material<0?material_count-1:unsigned(source.material);if(!material_count||material>=material_count)throw std::runtime_error("Native MToon material index");
                    NativeMToonFrameDraw d;d.lease=holder;d.capture_only=n>=b.material_plan.opaque_draws.size();d.numeric_material=unsigned(material);d.numeric_actor=b.mtoon_forward_actor;d.shadow_actor=b.mtoon_forward_actor;
                    if(raster.status==NativeMToonRasterStatus::prepared){if(n>=raster.draws.size()||raster.draws[n].draw_index!=index)throw std::runtime_error("Native MToon material order");d.uniform=raster.draws[n].uniform;}
                    else{d.uniform.domain={r.pass.extent.width,r.pass.extent.height,0,source.index_count/3};d.uniform.geometry={0,unsigned(geometry.vertices.size()),source.first_index,source.index_count};d.uniform.uv.fill(UINT32_MAX);d.uniform.uv[11]=0;d.uniform.time[0]=seconds;}
                    d.selected=d.uniform.uv[11]==1;
                    d.geometry.frame={b.frame->handle,0,224};d.geometry.material={b.materials->handle,b.material_stride*material,80};d.geometry.vertices=vertex_binding(b);d.geometry.indices=index_binding(b);d.geometry.base=b.assets->material(std::size_t(material));
                    const auto detailed=material<b.surface.features.size()?b.surface.features[std::size_t(material)]:0;
                    if(d.selected){const auto& m=request->plan().materials[std::size_t(material)];const auto& authored=b.pose->asset->materials[std::size_t(material)].mtoon;
                        if(!authored)throw std::runtime_error("Native MToon authored material missing");
                        if(d.capture_only&&prepared.program->has_forward()&&native_mtoon_forward_supported(*authored)){
                            d.forward_actor=b.mtoon_forward_actor;b.mtoon_forward_material_bits[std::size_t(material)/64]|=std::uint64_t(1)<<(std::size_t(material)%64);d.forward_surface=mtoon_surface_vertex_binding(b);}
                        const bool menu_cutout=frontend&&b.menu_cutout&&!d.capture_only&&
                            b.pose->asset->materials[std::size_t(material)].alpha==vrm::AlphaMode::Blend&&
                            (authored->render_mode==vrm::MToonRenderMode::Transparent||authored->render_mode==vrm::MToonRenderMode::TransparentZWrite);
                        if(d.capture_only||(!menu_cutout&&authored->render_mode!=vrm::MToonRenderMode::Opaque&&authored->render_mode!=vrm::MToonRenderMode::Cutout))d.selected=false;
                        if(menu_cutout)d.menu_cutout_material=&m.uniform;
                        d.needs_surface=(m.textures&(1u<<2u))!=0;d.geometry.surface=mtoon_surface_vertex_binding(b);
                        if(d.needs_surface&&!d.geometry.surface.buffer){d.selected=false;d.forward_actor=0;d.needs_surface=false;d.menu_cutout_material=nullptr;}
                        if(d.selected||d.forward_actor){d.cull=VkCullModeFlags(authored->cull_mode);d.material.uniform=b.assets->mtoon()->uniform(std::size_t(material));
                            for(unsigned slot=0;slot<11;++slot)d.material.textures[slot]=b.assets->mtoon()->texture(std::size_t(material),static_cast<vrm::MToonTextureSlot>(slot));
                            d.history.raw_uv=b.assets->mtoon()->uvs();d.outline=d.selected&&m.outline&&!menu_cutout;d.outline_cull=VkCullModeFlags(mtoon_outline_cull(*authored));
                            d.legacy_required=authored->version==vrm::MToonVersion::Vrm0&&authored->outline_mode==vrm::MToonOutlineMode::World;
                            if(d.outline&&d.legacy_required){if(!b.outline_rig)d.outline=false;else{d.control.facts[0]=2;d.history.legacy=b.outline_rig->sources();d.history.batches=b.outline_rig->batches();}}
                            const auto previous=b.previous;
                            if(d.outline&&previous&&previous->mtoon_rendered&&previous->mtoon_request==request&&previous->geometry&&previous->frame&&
                                std::find(previous->mtoon_outline_materials.begin(),previous->mtoon_outline_materials.end(),unsigned(material))!=previous->mtoon_outline_materials.end()){
                                d.control.facts[1]=1;d.control.time[0]=previous->mtoon_seconds;d.history.frame={previous->frame->handle,0,224};d.history.vertices=previous->geometry->binding();
                                if(d.legacy_required){if(previous->outline_rig){d.control.facts[2]=2;d.history.previous_legacy=previous->outline_rig->sources();d.history.previous_batches=previous->outline_rig->batches();}else d.control.facts[1]=0;}
                            }
                            if(d.outline){outlined.push_back(unsigned(material));caster.outlines[std::size_t(material)/64]|=std::uint64_t(1)<<(std::size_t(material)%64);}
                        }
                    }
                    if(!d.selected){d.outline=false;d.enhanced=detailed!=0;d.needs_surface=(detailed&9u)!=0;
                        if(d.enhanced){d.geometry.surface=surface_vertex_binding(b);d.geometry.enhanced_uniform={b.surface_materials?b.surface_materials->handle:VK_NULL_HANDLE,b.surface_stride*material,112};for(unsigned slot=0;slot<3;++slot)d.geometry.enhanced_textures[slot]=b.assets->surface_material(std::size_t(material),static_cast<NativeSurfaceTexture>(slot));}}
                    if(draws.size()>=4096)throw std::runtime_error("Native MToon view draw budget");
                    if(d.selected){if(authored_caster[index])throw std::runtime_error("Native MToon duplicate caster draw");authored_caster[index]=true;caster.authored.push_back(unsigned(draws.size()));caster.materials[std::size_t(material)/64]|=std::uint64_t(1)<<(std::size_t(material)%64);}
                    draws.push_back(std::move(d));
                }
                if(caster.qualified)for(unsigned n=0;n<b.shadow_calls.size();++n)if(!authored_caster[n])caster.ordinary.push_back(b.shadow_calls[n]);
                casters.push_back(std::move(caster));
                if(b.emission){emissions.emplace_back(b.emission,std::move(outlined));b.emission->mtoon_seconds=seconds;b.emission->mtoon_request=request;}
            };
            add(r.bundle);for(const auto& b:r.bundle->subjects)add(b);
            if(!mtoon_frame_pool)mtoon_frame_pool=std::make_unique<NativeMToonFramePool>(context.device,context.get,NativeMToonWorkCapabilities{limits,context.memory},&context.uncertain);
            const auto numeric=numeric_mtoon_config(GetTickCount64());
            const bool collect_observations=numeric.enabled||(mtoon_gpu_observations_recorded<4&&mtoon_observation_due(GetTickCount64()));
            auto frame=mtoon_frame_pool->prepare(command,r.pass.extent,draws,prepared.program,prepared.program->work_program(),collect_observations,sampled_mtoon_profile(r),numeric);
            for(auto& emission:emissions)emission.first->mtoon_outline_materials=std::move(emission.second);
            for(auto& caster:casters){caster.bundle->mtoon_caster_draws=std::move(caster.authored);caster.bundle->mtoon_generic_casters=std::move(caster.ordinary);caster.bundle->mtoon_caster_qualified=caster.qualified;caster.bundle->mtoon_caster_views=0;caster.bundle->mtoon_material_bits=caster.materials;caster.bundle->mtoon_outline_bits=caster.outlines;}
            each_bundle(r.bundle,[&](Bundle& b){b.mtoon_forward_capture=frame->forward_capture();b.mtoon_forward_packets=frame->forward_packets();});
            r.mtoon_frame=std::move(frame);mtoon_frame_error.clear();return true;
        }catch(const std::exception& error){try{mtoon_frame_error=error.what();}catch(...){}return false;}
        catch(...){try{mtoon_frame_error="unknown preparation exception";}catch(...){}return false;}
    }
    void finish_mtoon_gbuffer(Recording& r,VkCommandBuffer command){
        if(!r.mtoon_frame||!r.pass_active||r.pass.kind!=NativePipelineKind::gbuffer||r.mtoon_gbuffer_recorded)return;
        const auto decline=[&]{each_bundle(r.bundle,[](Bundle& b){b.invalid=true;});};
        if(r.tainted||!r.pass.supported||!r.mtoon_visibility_recorded||!metadata.raster(command).matches(r.pass.extent)){decline();return;}
        bool ready=false;each_bundle(r.bundle,[&](Bundle& b){if(!b.pass_drawn||!b.gbuf_camera||b.first_pass.depth!=r.pass.depth||b.first_pass.extent.width!=r.pass.extent.width||b.first_pass.extent.height!=r.pass.extent.height)b.invalid=true;ready=ready||!b.invalid;});
        if(!ready){decline();return;}const auto bound=metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_GRAPHICS);const auto restore=compatible(bound);
        if(!bound.valid||!restore){decline();return;}r.mtoon_final_restore=restore;
        if(!r.mtoon_frame->record_raster(command,true,bound.pipeline.pipeline,restore->handle,unsigned(bound.sets.size()),bound.sets.data())){decline();return;}
        r.mtoon_gbuffer_recorded=true;each_bundle(r.bundle,[](Bundle& b){++b.gbuffer_draws;});
    }

    NativeMToonCompositeProgramStatus mtoon_composite_menu_state=NativeMToonCompositeProgramStatus::unsupported;
    bool mtoon_composite_shader_attempted=false;
    NativeMToonCompositeCapabilities mtoon_composite_caps;
    std::unique_ptr<NativeMToonCompositeProgramCache> mtoon_composite_programs;
    NativeMToonCompositePreparedProgram prepare_mtoon_composite_program()noexcept{
        if(context.uncertain||!mtoon_scene_requested()||!native_mtoon_composite_features_supported(mtoon_enabled))return {};
        try{
            if(!mtoon_composite_programs){
                if(mtoon_composite_shader_attempted)return {};mtoon_composite_shader_attempted=true;
                if(!get_instance)return {};const auto formats=reinterpret_cast<PFN_vkGetPhysicalDeviceFormatProperties>(get_instance(instance,"vkGetPhysicalDeviceFormatProperties"));if(!formats)return {};
                mtoon_composite_caps.limits=limits;mtoon_composite_caps.memory=context.memory;mtoon_composite_caps.storage_image_extended_formats=true;
                constexpr VkFormat required[]{VK_FORMAT_R16G16B16A16_SFLOAT,VK_FORMAT_R16_SFLOAT,VK_FORMAT_R8_UNORM};
                for(unsigned n=0;n<3;++n){VkFormatProperties facts{};formats(physical,required[n],&facts);mtoon_composite_caps.format_features[n]=facts.optimalTilingFeatures;}
                std::ifstream input(shader_directory/"native_mtoon_composite.comp.spv",std::ios::binary|std::ios::ate);
                if(!input)return {};const auto bytes=input.tellg();if(bytes<20||bytes>1024*1024||bytes%4!=0)return {};
                std::vector<std::uint32_t> words(std::size_t(bytes)/4);input.seekg(0);input.read(reinterpret_cast<char*>(words.data()),bytes);if(!input||words[0]!=0x07230203)return {};
                mtoon_composite_programs=std::make_unique<NativeMToonCompositeProgramCache>(context.device,context.get,mtoon_composite_caps,std::move(words),&context.uncertain);
            }
            auto prepared=mtoon_composite_programs->request();mtoon_composite_menu_state=prepared.status;return prepared;
        }catch(...){return {};}
    }

    // This callback runs immediately after the exact original Deferred
    // dispatch. Borrow its read roles and3 destinations in the same primary,
    // family and COMPUTE stage; do not move this work to a private queue/submit.
    // Matching shader branches retain original validity/initialization domains;
    // masks, typed defaults and descriptor metadata are not readiness proofs.
    void record_mtoon_composition(Recording& r,VkCommandBuffer command)noexcept{
        if(r.mtoon_lighting_attempted||!r.mtoon_frame||!r.mtoon_scene)return;
        r.mtoon_lighting_attempted=true;r.mtoon_composition_checkpoint=1;
        if(!r.eligible||r.tainted||r.pass_active||r.mtoon_raster_passes!=1||r.mtoon_lighting_passes!=1||
            !r.mtoon_visibility_recorded||!r.mtoon_gbuffer_recorded||r.mtoon_scope_family!=family||context.uncertain)return;
        r.mtoon_composition_checkpoint=2;if(!image_memory||!r.mtoon_scene_backing.for_scene(r.mtoon_scene))return;
        r.mtoon_composition_checkpoint=3;if(!r.mtoon_scene_backing.current(metadata,memory,*image_memory))return;
        r.mtoon_composition_checkpoint=4;
        const bool frontend=r.mtoon_scene.metadata().raster.frontend;
        if(frontend&&!metadata.cube_initialized(r.mtoon_scene.metadata().views[3].identity,queue))return;
        if(!frontend&&(!r.mtoon_probe_snapshot||!r.mtoon_probe_layout_qualified||!r.mtoon_probe_backing.for_graph(r.mtoon_probe_graph)))return;
        r.mtoon_composition_checkpoint=5;
        const auto& native=r.mtoon_scene.metadata();const auto dispatch=metadata.observed_dispatch(command);
        const auto bound=metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_COMPUTE);
        if(command!=native.command||!dispatch||dispatch.serial!=native.dispatch.serial||
            !bound.valid||bound.pipeline.pipeline!=native.bound.pipeline.pipeline||bound.pipeline.generation!=native.bound.pipeline.generation||
            bound.sets!=native.bound.sets||bound.set_generations!=native.bound.set_generations||bound.set_versions!=native.bound.set_versions)return;
        try{
            r.mtoon_composition_checkpoint=6;const auto shade=prepare_mtoon_shade_program();const auto composite=prepare_mtoon_composite_program();
            if(shade.status!=NativeMToonShadeProgramStatus::ready||!shade.program||composite.status!=NativeMToonCompositeProgramStatus::ready||!composite.program)return;
            std::shared_ptr<const NativeMToonResources> leaves;
            each_bundle(r.bundle,[&](const Bundle& b){if(!leaves&&b.assets&&b.pose&&b.assets->mtoon()&&native_mtoon_request_matches(b.assets->mtoon_request(),b.pose->asset,b.pose->appearance.get()))leaves=b.assets->mtoon();});
            r.mtoon_composition_checkpoint=7;const auto& draws=r.mtoon_frame->shade_draws();if(!leaves||draws.empty())return;
            NativeMToonSceneFallbacks owned;owned.uniform=draws.front().inputs.material;owned.storage=draws.front().inputs.owned[3];owned.storage.range=4;
            owned.images={draws.front().inputs.material_maps[0],leaves->fallback(NativeMToonFallbackImage::volume,queue,family),
                leaves->fallback(NativeMToonFallbackImage::cube,queue,family),leaves->fallback(NativeMToonFallbackImage::array,queue,family)};
            // Share only the pinned native consumer's initialized read domains.
            // Frontend IBL additionally requires a generation-bound full-cube
            // upload and visibility receipt on this queue. World IBL and full
            // water reads remain unqualified; owned placeholders keep their
            // masks withdrawn and those pixels retain native Deferred colour.
            r.mtoon_fallback_facts=(owned.uniform.buffer?1u:0u)|((owned.uniform.range>=640&&owned.uniform.range!=VK_WHOLE_SIZE)?2u:0u)|(owned.uniform.offset%4==0?4u:0u);
            for(unsigned n=0;n<4;++n){const auto& image=owned.images[n];const unsigned bits=(image.imageView?1u:0u)|(image.sampler?2u:0u)|((image.imageLayout==VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL||image.imageLayout==VK_IMAGE_LAYOUT_GENERAL)?4u:0u);r.mtoon_fallback_facts|=bits<<(4+n*3);}
            r.mtoon_composition_checkpoint=8;auto view=native_mtoon_scene_view(r.mtoon_scene,r.mtoon_scene_backing,r.mtoon_frame->generation(),family,owned,frontend?0x3fffu:0x3ff7u,6u);
            if(!view)return;r.mtoon_composition_checkpoint=9;if(!view.current(metadata,memory,*image_memory))return;
            r.mtoon_composition_checkpoint=10;const auto restore=compatible(bound);if(!restore)return;
            r.mtoon_scene_view=std::move(view);r.mtoon_compute_restore=restore;
            // Always restore on partial optional allocation/recording failure.
            // Every owner is retained before its first driver recording call.
            const auto restore_native=[&]{function<PFN_vkCmdBindPipeline>("vkCmdBindPipeline")(command,VK_PIPELINE_BIND_POINT_COMPUTE,bound.pipeline.pipeline);
                function<PFN_vkCmdBindDescriptorSets>("vkCmdBindDescriptorSets")(command,VK_PIPELINE_BIND_POINT_COMPUTE,restore->handle,0,unsigned(bound.sets.size()),bound.sets.data(),0,nullptr);};
            struct Restore {decltype(restore_native)& fn;~Restore(){fn();}} restore_scope{restore_native};
            r.mtoon_composition_checkpoint=11;
            if(profiling_requested)(void)r.mtoon_frame->begin_gpu_stage(command,NativeMToonProfileStage::work);
            if(!r.mtoon_frame->record_work(command))return;
            if(profiling_requested)(void)r.mtoon_frame->end_gpu_stage(command,NativeMToonProfileStage::work);
            NativeMToonShadeCapabilities caps;caps.limits=limits;caps.descriptor_binding_partially_bound_enabled=partially_bound;
            const auto forward_program=prepare_mtoon_forward_shade_program();bool forward_ready=forward_program.status==NativeMToonShadeProgramStatus::ready&&bool(forward_program.program);
            r.mtoon_composition_checkpoint=12;r.mtoon_shades.reserve(draws.size());
            const auto raw_groups=r.mtoon_frame->shade_group_counts();std::array<unsigned,3> profile_groups{};
            unsigned profile_draw_index=0;
            for(const auto& draw:draws){const auto ordinal=profile_draw_index++;
                const unsigned profile_group=ordinal<raw_groups[0]?0:ordinal<raw_groups[0]+raw_groups[1]?1:2;
                auto in=draw.inputs;in.owned[13]=frontend?owned.storage:r.mtoon_probe_snapshot->output();r.mtoon_scene_view.shade_inputs(in);
                if(draw.forward_actor){if(!forward_ready)continue;bool found=false;
                    each_bundle(r.bundle,[&](const Bundle& b){if(b.mtoon_forward_actor==draw.forward_actor&&b.frame){
                        in.forward_constants={b.frame->handle,native_oit_constants_offset,2320};in.forward_visibility=owned.images[1];in.forward_sampler=owned.images[1].sampler;
                        r.mtoon_forward_volume_at_deferred=r.mtoon_forward_volume_at_deferred||(b.volume_snapshot&&b.snapshot_recorded);found=true;}});
                    if(!found){forward_ready=false;continue;}auto forward_caps=caps;forward_caps.forward=true;
                    r.mtoon_shades.push_back(std::make_unique<NativeMToonShadePass>(context.device,context.get,forward_caps,
                        NativeMToonShadeParameters{native.raster.extent.width,native.raster.extent.height,r.mtoon_frame->generation(),draw.work_index,262144},in,&context.uncertain,forward_program.program));
                }else r.mtoon_shades.push_back(std::make_unique<NativeMToonShadePass>(context.device,context.get,caps,
                    NativeMToonShadeParameters{native.raster.extent.width,native.raster.extent.height,r.mtoon_frame->generation(),draw.work_index},in,&context.uncertain,shade.program));
                ++profile_groups[profile_group];}

            r.mtoon_composition_checkpoint=13;r.mtoon_composite=std::make_unique<NativeMToonCompositePass>(context.device,context.get,mtoon_composite_caps,
                NativeMToonCompositeParameters{native.raster.extent.width,native.raster.extent.height,r.mtoon_frame->generation(),r.mtoon_frame->has_outlines()?1u:0u},
                r.mtoon_frame->packets(),r.mtoon_frame->identities(),r.mtoon_frame->composite_permit(),r.mtoon_scene_view.composite_images(),
                NativeMToonCompositeShader{},&context.uncertain,composite.program);
            r.mtoon_composition_checkpoint=14;unsigned profile_pass_index=0;
            constexpr NativeMToonProfileStage shade_stages[]{NativeMToonProfileStage::opaque_shade,NativeMToonProfileStage::forward_shade,NativeMToonProfileStage::outline_shade};
            // Counts only bracket diagnostics. Every original pass still records
            // once in its original vector order, regardless of timing state.
            for(const auto& pass:r.mtoon_shades){
                if(profiling_requested){unsigned offset=0;for(unsigned n=0;n<3;++n){if(profile_groups[n]&&profile_pass_index==offset)(void)r.mtoon_frame->begin_gpu_stage(command,shade_stages[n]);offset+=profile_groups[n];}}
                if(!pass->record(command,bound.pipeline.pipeline,restore->handle,unsigned(bound.sets.size()),bound.sets.data()))return;
                ++profile_pass_index;
                if(profiling_requested){unsigned offset=0;for(unsigned n=0;n<3;++n){offset+=profile_groups[n];if(profile_groups[n]&&profile_pass_index==offset)(void)r.mtoon_frame->end_gpu_stage(command,shade_stages[n]);}}
            }
            r.mtoon_composition_checkpoint=15;
            if(profiling_requested)(void)r.mtoon_frame->begin_gpu_stage(command,NativeMToonProfileStage::composite);
            if(!r.mtoon_composite->record(command,bound.pipeline.pipeline,restore->handle,unsigned(bound.sets.size()),bound.sets.data()))return;
            if(profiling_requested)(void)r.mtoon_frame->end_gpu_stage(command,NativeMToonProfileStage::composite);
            // Active water needs original quad/subgroup mapping not yet proved.
            // Its GPU branch leaves native colour untouched; inactive pixels
            // still use exact native water classification without invented1s.
            r.mtoon_composition_checkpoint=16;r.mtoon_lighting_recorded=r.mtoon_frame->source_masks(r.mtoon_scene_view.buffer_mask(),r.mtoon_scene_view.image_mask(),
                r.mtoon_scene_view.sampler_mask(),r.mtoon_scene_view.water_mask(),false);
            if(r.mtoon_lighting_recorded){r.mtoon_original_use_serial=native.dispatch.serial;r.mtoon_composition_checkpoint=17;
                r.mtoon_forward_prepared=forward_ready&&bool(r.mtoon_frame->forward_capture().buffer);}

        }catch(const std::exception& error){try{mtoon_composition_error=error.what();}catch(...){}}
        catch(...){try{mtoon_composition_error="unknown composition exception";}catch(...){}}
        // Recorded optional owners survive until original retirement.
    }
    // All borrowed Deferred reads ended in record_mtoon_composition. This
    // later step reads OWNED sources only, after Populate has copied the
    // transparent visibility volume. It never extends native source scope.
    void record_mtoon_forward_finish(Recording& r,VkCommandBuffer command)noexcept{
        if(!r.mtoon_frame||!r.mtoon_lighting_recorded||r.pass_active||!r.eligible||r.tainted||context.uncertain)return;
        if(!r.mtoon_frame->forward_capture().buffer){record_mtoon_observation(r,command);return;}
        if(!r.mtoon_forward_prepared||r.mtoon_forward_finished)return;
        // Deferred can precede Populate. A ready program/capture allocation is
        // not a completed actor volume; wait for a later original dispatch.
        const auto forward_actors=r.mtoon_frame->forward_actor_mask();
        bool waiting=false,source_ready=false;
        each_bundle(r.bundle,[&](const Bundle& b){
            if(b.invalid||!b.mtoon_forward_actor||b.mtoon_forward_actor>32||
               !(forward_actors&(1u<<(b.mtoon_forward_actor-1)))||
               (r.mtoon_forward_finished_actors&(1u<<(b.mtoon_forward_actor-1))))return;
            waiting=true;source_ready=source_ready||(b.volume_snapshot&&b.snapshot_recorded);
        });
        if(!source_ready){
            if(!waiting){r.mtoon_forward_finished=true;record_mtoon_observation(r,command);}
            return;
        }
        try{
            const auto prepared=prepare_mtoon_forward_finish_program();if(prepared.status!=NativeMToonShadeProgramStatus::ready||!prepared.program)return;
            const auto bound=metadata.observed_bound(command,VK_PIPELINE_BIND_POINT_COMPUTE);const auto restore=compatible(bound);if(!bound.valid||!restore)return;
            NativeMToonShadeCapabilities caps;caps.limits=limits;caps.forward=true;caps.descriptor_binding_partially_bound_enabled=partially_bound;
            if(profiling_requested)(void)r.mtoon_frame->begin_gpu_stage(command,NativeMToonProfileStage::forward_finish);
            bool pending=false;
            each_bundle(r.bundle,[&](Bundle& b){
                auto& facts=r.mtoon_forward_finish_facts;++facts[0];
                if(b.invalid){++facts[1];return;}if(!b.mtoon_forward_actor||b.mtoon_forward_actor>32){++facts[4];return;}
                const auto actor_bit=1u<<(b.mtoon_forward_actor-1);
                if(!(forward_actors&actor_bit))return;
                if(r.mtoon_forward_finished_actors&actor_bit)return;
                if(!b.volume_snapshot){++facts[2];pending=true;return;}if(!b.snapshot_recorded){++facts[3];pending=true;return;}
                const auto first=r.mtoon_forward_finishes.size();bool ready=true;
                try{for(const auto& draw:r.mtoon_frame->shade_draws())if(draw.forward_actor==b.mtoon_forward_actor){++facts[5];auto in=draw.inputs;
                    in.owned[13]=r.mtoon_probe_snapshot->output();r.mtoon_scene_view.shade_inputs(in);
                    in.forward_constants={b.frame->handle,native_oit_constants_offset,2320};in.forward_visibility=b.volume_snapshot->outputs()[1];in.forward_sampler=volume_sampler;
                    r.mtoon_forward_finishes.push_back(std::make_unique<NativeMToonShadePass>(context.device,context.get,caps,
                        NativeMToonShadeParameters{r.mtoon_raster.extent.width,r.mtoon_raster.extent.height,r.mtoon_frame->generation(),draw.work_index,262144},in,&context.uncertain,prepared.program));
                    ++facts[6];
                }}catch(...){++facts[8];ready=false;}
                // Even a failed/partially recorded optional finish retains all
                // owners until retirement, but enables no capture or lookup.
                for(auto n=first;ready&&n<r.mtoon_forward_finishes.size();++n){ready=r.mtoon_forward_finishes[n]->record(command,bound.pipeline.pipeline,restore->handle,unsigned(bound.sets.size()),bound.sets.data());facts[7]+=ready;}
                ready=ready&&r.mtoon_forward_finishes.size()>first;
                r.mtoon_forward_finished_actors|=actor_bit;
                facts[9]+=ready;facts[10]+=r.mtoon_active;
                r.mtoon_frame->forward_ready(b.mtoon_forward_actor,ready);
                if(ready)for(unsigned n=0;n<64;++n)b.mtoon_material_bits[n]|=b.mtoon_forward_material_bits[n];
            });
            if(profiling_requested)(void)r.mtoon_frame->end_gpu_stage(command,NativeMToonProfileStage::forward_finish);
            r.mtoon_forward_finished=!pending;
            if(r.mtoon_forward_finished)record_mtoon_observation(r,command);
        }catch(const std::exception& error){try{mtoon_composition_error=error.what();}catch(...){}}
        catch(...){try{mtoon_composition_error="unknown forward finish exception";}catch(...){}}
    }
    bool mtoon_shadow_ready(const Recording& r)const noexcept{
        if(!r.mtoon_frame)return false;
        if(r.mtoon_raster.frontend&&(!r.bundle||!r.bundle->menu_preview||r.bundle->menu_owner.kind!=NativeMenuOwnerKind::frontend||
            !r.bundle->subjects.empty()))return false;
        // Frontend sun shadows use the same original caster recording and
        // submit-time source/depth checks as world shadows. A missing point
        // shadow dictionary does not imply that sun shadow passes are absent.
        if(!r.mtoon_frame->has_shadows())return false;bool ready=true;
        each_bundle(r.bundle,[&](const Bundle& b){if(b.data.permit[0]&&!b.invalid&&!b.mtoon_caster_draws.empty())ready=ready&&b.mtoon_caster_qualified&&b.mtoon_caster_views==b.shadow_views.size();});return ready;
    }
    bool mtoon_original_use_ready(const Recording& r)const noexcept{
        // Receipt exists only after the immediate post-forward original
        // primary/family scope, exact current resources/restore state and full
        // Work->shade->composite recording succeeded. The same original valid
        // use supplies these read domains/layouts/ownership until its ordinary
        // retirement. Submit separately rechecks queue, waits and lifetimes.
        // Frontend's extra IBL directions need their separate full-cube proof;
        // world IBL and active-water sampling remain unqualified.
        if(!r.mtoon_original_use_serial||!r.mtoon_lighting_recorded||!r.mtoon_compute_restore||!r.mtoon_scene||!r.mtoon_scene_view||
            r.mtoon_scope_family!=family||r.mtoon_scene_view.family()!=family||r.mtoon_lighting_passes!=1||r.mtoon_raster_passes!=1||
            (r.mtoon_scene_view.image_mask()&~(r.mtoon_raster.frontend?0x3fffu:0x3ff7u))||(r.mtoon_scene_view.sampler_mask()&(r.mtoon_raster.frontend?96u:104u))||(r.mtoon_scene_view.water_mask()&~6u))return false;
        const auto& source=r.mtoon_scene.metadata();return source.command&&source.command==r.mtoon_raster.command&&source.raster.completed&&(source.raster.preview==source.raster.frontend)&&source.raster.frontend==r.mtoon_raster.frontend&&
            (!source.raster.frontend||metadata.cube_initialized(source.views[3].identity,queue))&&
            source.dispatch.kind==NativeDispatchKind::Direct&&source.dispatch.serial==r.mtoon_original_use_serial&&source.probes==r.mtoon_probe_graph&&
            native_mtoon_probe_stage(source.bound.pipeline)==NativeMToonProbeStage::deferred;
    }
    void arm_mtoon_composition(Recording& r,bool shape,VkQueue submitted_queue)noexcept{
        NativeSubmitStageScope arm(submit_timing,NativeSubmitStage::MToonCompositionArm);
        if(!r.mtoon_frame||r.mtoon_armed)return;r.mtoon_armed=true;
        const auto scene_current=[&]{NativeSubmitStageScope current(submit_timing,NativeSubmitStage::MToonSceneCurrent);
            return r.mtoon_scene_view.current(metadata,memory,*image_memory);};
        const auto shadow_current_use=[&]{NativeSubmitStageScope current(submit_timing,NativeSubmitStage::MToonShadowUse);
            return mtoon_shadow_ready(r)&&mtoon_original_use_ready(r);};
        bool permit=shape&&r.eligible&&!r.tainted&&!r.pass_active&&r.ownership_closed&&!disabled&&!context.uncertain&&submitted_queue==queue&&
            r.mtoon_scope_family==family&&r.mtoon_lighting_recorded&&r.mtoon_lighting_passes==1&&r.mtoon_raster_passes==1&&
            r.mtoon_scene_view&&r.mtoon_scene_view.generation()==r.mtoon_frame->generation()&&image_memory&&
            scene_current()&&(r.mtoon_raster.frontend?(!r.mtoon_probe_graph&&!r.mtoon_probe_snapshot):
                (r.mtoon_probe_armed&&r.mtoon_probe_permit&&*static_cast<const std::uint32_t*>(r.mtoon_probe_permit->mapped)==1u))&&shadow_current_use();
        {NativeSubmitStageScope casters(submit_timing,NativeSubmitStage::MToonCasterRequests);
            bool survivor=false;each_bundle(r.bundle,[&](const Bundle& b){if(!b.data.permit[0])return;survivor=true;permit=permit&&b.submitted&&b.awaiting_submit_result&&b.submitted_queue==submitted_queue&&!b.failed_submit&&!b.invalid;
                if(!b.mtoon_caster_draws.empty()){const auto latest=current_player_publication(b);permit=permit&&latest&&b.assets&&native_mtoon_request_matches(b.assets->mtoon_request(),latest->asset,latest->appearance.get());}});permit=permit&&survivor;}
        {NativeSubmitStageScope activation(submit_timing,NativeSubmitStage::MToonActivation);r.mtoon_active=r.mtoon_frame->set_active(permit);}
    }
