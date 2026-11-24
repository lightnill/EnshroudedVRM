#pragma once
#include "native_mtoon_probe_source.hpp"
#include "native_mtoon_probe_frame.hpp"
namespace evrm {
enum class NativeMToonProbeStage {other,classify,converge,update,deferred};
enum class NativeMToonProbeGraphFailure {none,command,sequence,shader,layout,declaration,dispatch,buffer,overlap,resources,exception};
namespace mtoon_graph_detail {
constexpr unsigned hex(char c){return unsigned(c<='9'?c-'0':c-'a'+10);}
constexpr std::array<std::uint8_t,32> digest(const char* value){std::array<std::uint8_t,32> result{};
    for(unsigned i=0;i<32;++i)result[i]=std::uint8_t(hex(value[i*2])*16+hex(value[i*2+1]));return result;}
inline bool compute(const NativePipelineState& p)noexcept{return p.pipeline&&p.layout&&p.generation&&p.set_count&&
    !p.graphics_stage_count&&!p.vertex_bytes&&!p.fragment_bytes&&!p.mesh_bytes&&!p.tess_control_bytes&&!p.tess_evaluation_bytes;}
inline bool identity(const NativeBufferBinding& a,const NativeBufferBinding& b)noexcept{return a&&b&&
    a.info.buffer==b.info.buffer&&a.info.offset==b.info.offset&&a.info.range==b.info.range&&a.generation==b.generation&&a.size==b.size&&a.usage==b.usage;}
// Native stages may use different UBO objects or disjoint arena slices. Do not
// join those by address: the owned GPU consumer must compare spatial values.
// A recycled/inconsistently described shared object or partial overlap declines.
inline bool parameter_objects(const NativeBufferBinding& a,const NativeBufferBinding& b)noexcept{return a&&b&&
    (a.info.buffer!=b.info.buffer||(a.generation==b.generation&&a.size==b.size&&a.usage==b.usage&&
        (a.info.offset==b.info.offset||a.info.offset+a.info.range<=b.info.offset||b.info.offset+b.info.range<=a.info.offset)));}
inline bool layout(const NativeBoundState& b,const NativeDescriptorSetLayoutObservation& l)noexcept{
    return b.valid&&b.pipeline.set_count==1&&b.sets.size()==1&&b.set_generations.size()==1&&b.set_versions.size()==1&&l&&
        l.set==b.sets[0]&&l.set_generation==b.set_generations[0]&&l.set_version==b.set_versions[0];
}
inline bool buffer(const NativeBufferBinding& b,const NativeDescriptorSetLayoutObservation& l,unsigned binding,VkDeviceSize bytes)noexcept{
    return b&&b.set==l.set&&b.set_generation==l.set_generation&&b.set_version==l.set_version&&b.binding==binding&&
        !b.declaration_flags&&b.info.offset%4==0&&b.info.range==bytes&&
        (b.usage&(binding?VK_BUFFER_USAGE_STORAGE_BUFFER_BIT:VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT));
}
inline bool declaration(const NativeDescriptorDeclaration& d,unsigned maximum,unsigned sampled=UINT32_MAX)noexcept{
    const auto type=d.binding==0?VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER:d.binding==sampled?VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE:VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    return d.binding<maximum&&d.count==1&&!d.flags&&d.type==type&&
        (d.stages==VK_SHADER_STAGE_COMPUTE_BIT||(d.binding==0&&d.stages==mtoon_probe_detail::native_parameter_stages));
}
inline bool current_layout(const NativeState& state,const NativeBoundState& bound,const NativeDescriptorSetLayoutObservation& old)noexcept{
    const auto now=state.observed_set_layout(bound,0);return now&&now.layout==old.layout&&now.generation==old.generation&&
        now.set==old.set&&now.set_generation==old.set_generation&&now.set_version==old.set_version;
}
}
inline NativeMToonProbeStage native_mtoon_probe_stage(const NativePipelineState& p)noexcept{
    if(!mtoon_graph_detail::compute(p))return NativeMToonProbeStage::other;
    using namespace mtoon_graph_detail;
    if(p.compute_bytes==8964&&p.compute_digest==digest("2dc8ce3ddb81885ab789f46fe5c6fe86f382a8cbd7b3f4cbef5503a4989da2e9"))return NativeMToonProbeStage::classify;
    if(native_mtoon_probe_producer(p.compute_bytes,p.compute_digest)==NativeMToonProbeProducer::AmbientConverge)return NativeMToonProbeStage::converge;
    if(p.compute_bytes==7628&&p.compute_digest==digest("8af2de7ab78b8fbb82d6fd0b9556559a3089790892426d8516f89252f9351c45"))return NativeMToonProbeStage::update;
    if(p.compute_bytes==74292&&p.compute_digest==digest("50177f15315cb95c82320a0c8fce5a66daec9652786f12601cac48e1355ff537"))return NativeMToonProbeStage::deferred;
    return NativeMToonProbeStage::other;
}
struct NativeMToonProbeClassifyMetadata {
    NativeBoundState bound;NativeDescriptorSetLayoutObservation layout;NativeDispatchObservation dispatch;
    std::array<NativeBufferBinding,7> buffers;
};
struct NativeMToonProbeUpdateMetadata {
    NativeBoundState bound;NativeDescriptorSetLayoutObservation layout;NativeDispatchObservation dispatch;
    NativeBufferBinding parameters,state;
};
// One primary recording's necessary stage/resource graph. This is retained
// metadata, not backing, initialized GI, a source lease or scene readiness.
class NativeMToonProbeGraph {
public:
    explicit NativeMToonProbeGraph(VkCommandBuffer command)noexcept:command_(command),failed_(!command),failure_(command?NativeMToonProbeGraphFailure::none:NativeMToonProbeGraphFailure::command){}
    bool classified(VkCommandBuffer command,const NativeMToonProbeClassifyMetadata& f)noexcept{
        try{
            using namespace mtoon_graph_detail;
            constexpr auto stage=NativeMToonProbeStage::classify;using Failure=NativeMToonProbeGraphFailure;
            if(failed_)return false;
            if(command!=command_)return decline(stage,Failure::command);
            if(classified_)return decline(stage,Failure::sequence);
            if(native_mtoon_probe_stage(f.bound.pipeline)!=stage)return decline(stage,Failure::shader);
            if(!layout(f.bound,f.layout)||f.layout.bindings.size()!=7)return decline(stage,Failure::layout);
            if(!f.dispatch||f.dispatch.kind!=NativeDispatchKind::Direct||f.dispatch.groups!=std::array<std::uint32_t,3>{512,1,1})return decline(stage,Failure::dispatch);
            std::array<bool,7> seen{};for(const auto& d:f.layout.bindings){if(!declaration(d,7)||seen[d.binding])return decline(stage,Failure::declaration);seen[d.binding]=true;}
            constexpr VkDeviceSize fixed[]{480,131072,393216,4,4};
            for(unsigned i=0;i<7;++i){const auto size=i<5?fixed[i]:f.buffers[i].info.range;
                if((i>=5&&(size<4||size>131072||size%4))||!buffer(f.buffers[i],f.layout,i,size))return decline(stage,Failure::buffer);
                for(unsigned j=0;j<i;++j)if(f.buffers[i].info.buffer==f.buffers[j].info.buffer&&
                    f.buffers[i].info.offset<f.buffers[j].info.offset+f.buffers[j].info.range&&f.buffers[j].info.offset<f.buffers[i].info.offset+size)return decline(stage,Failure::overlap);
            }
            classifier_=f;serial_=f.dispatch.serial;classified_=true;return true;
        }catch(...){return decline(NativeMToonProbeStage::classify,NativeMToonProbeGraphFailure::exception);}
    }
    bool converged(VkCommandBuffer command,const NativeMToonProbeCandidate& candidate)noexcept{
        try{
            using namespace mtoon_graph_detail;
            constexpr auto stage=NativeMToonProbeStage::converge;using Failure=NativeMToonProbeGraphFailure;
            if(failed_)return false;
            if(command!=command_)return decline(stage,Failure::command);
            if(!classified_||updates_)return decline(stage,Failure::sequence);
            if(!candidate)return decline(stage,Failure::resources);
            const auto& f=candidate.metadata();const auto root=f.root.value;
            if(f.dispatch.serial<=serial_||(roots_&(1u<<root)))return decline(stage,Failure::sequence);
            constexpr unsigned matching[]{0,1,5,6,4,3};
            if(!parameter_objects(f.buffers[0],classifier_.buffers[0]))return decline(stage,Failure::resources);
            for(unsigned i=1;i<6;++i)if(!identity(f.buffers[i],classifier_.buffers[matching[i]]))return decline(stage,Failure::resources);
            if(roots_){const auto& prior=convergence_.metadata();
                if(f.bound.pipeline.pipeline!=prior.bound.pipeline.pipeline||f.bound.pipeline.generation!=prior.bound.pipeline.generation||
                   f.layout.layout!=prior.layout.layout||f.layout.generation!=prior.layout.generation||f.root.layout_generation!=prior.root.layout_generation)return decline(stage,Failure::resources);
                for(unsigned i=0;i<6;++i)if(!mtoon_probe_detail::same(f.buffers[i],prior.buffers[i]))return decline(stage,Failure::resources);
                for(unsigned i=0;i<3;++i)if(!mtoon_probe_detail::same(f.coefficients[i].image,prior.coefficients[i].image))return decline(stage,Failure::resources);
            }else convergence_=candidate;
            roots_|=1u<<root;serial_=f.dispatch.serial;return true;
        }catch(...){return decline(NativeMToonProbeStage::converge,NativeMToonProbeGraphFailure::exception);}
    }
    bool updated(VkCommandBuffer command,const NativeMToonProbeUpdateMetadata& f)noexcept{
        try{
            using namespace mtoon_graph_detail;
            constexpr auto stage=NativeMToonProbeStage::update;using Failure=NativeMToonProbeGraphFailure;
            if(failed_)return false;
            if(command!=command_)return decline(stage,Failure::command);
            if(!classified_||roots_!=3u||updates_>=2||f.dispatch.serial<=serial_)return decline(stage,Failure::sequence);
            if(native_mtoon_probe_stage(f.bound.pipeline)!=stage)return decline(stage,Failure::shader);
            if(!layout(f.bound,f.layout)||f.layout.bindings.size()!=13)return decline(stage,Failure::layout);
            if(!mtoon_probe_detail::domain(f.dispatch))return decline(stage,Failure::dispatch);
            std::array<bool,13> seen{};for(const auto& d:f.layout.bindings){auto checked=d;
                // These three scalar storage slots are unused by the pinned
                // shader. Native layouts may mark only those partially bound.
                if(d.binding==8||d.binding==11||d.binding==12)checked.flags&=~VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT;
                if(!declaration(checked,13,10)||seen[d.binding])return decline(stage,Failure::declaration);seen[d.binding]=true;}
            if(!buffer(f.parameters,f.layout,0,480)||!buffer(f.state,f.layout,1,131072)||
                !parameter_objects(f.parameters,classifier_.buffers[0])||!parameter_objects(f.parameters,convergence_.metadata().buffers[0])||
                !identity(f.state,classifier_.buffers[1]))return decline(stage,Failure::resources);
            if(updates_&&!parameter_objects(f.parameters,first_update_.parameters))return decline(stage,Failure::resources);
            if(updates_)update_=f;else first_update_=f;serial_=f.dispatch.serial;++updates_;return true;
        }catch(...){return decline(NativeMToonProbeStage::update,NativeMToonProbeGraphFailure::exception);}
    }
    bool has_sources()const noexcept{return !failed_&&classified_&&roots_!=0u;}
    bool has_classifier()const noexcept{return !failed_&&classified_;}
    bool complete()const noexcept{return !failed_&&classified_&&roots_==3u&&updates_==2;}
    VkCommandBuffer command()const noexcept{return command_;}
    bool failed()const noexcept{return failed_;}
    NativeMToonProbeStage failed_stage()const noexcept{return failed_stage_;}
    NativeMToonProbeGraphFailure failure()const noexcept{return failure_;}
    const NativeMToonProbeClassifyMetadata& classifier()const noexcept{return classifier_;}
    const NativeMToonProbeCandidate& convergence()const noexcept{return convergence_;}
    unsigned update_count()const noexcept{return updates_;}
    const NativeMToonProbeUpdateMetadata& first_update()const noexcept{return first_update_;}
    const NativeMToonProbeUpdateMetadata& update()const noexcept{return updates_==1?first_update_:update_;}
    NativeMToonProbeFrameSource copy_source(VkDescriptorBufferInfo permit)const noexcept{
        if(!complete())return {};const auto& coefficient=convergence_.metadata().layout;
        return {coefficient.layout,coefficient.set,classifier_.layout.layout,classifier_.layout.set,permit,update_.parameters.info,first_update_.parameters.info};
    }
    bool current(const NativeState& state)const noexcept{
        try{
            using namespace mtoon_graph_detail;if(!complete())return false;const auto& c=convergence_.metadata();
            if(!current_layout(state,classifier_.bound,classifier_.layout)||!current_layout(state,c.bound,c.layout)||
                !current_layout(state,first_update_.bound,first_update_.layout)||!current_layout(state,update_.bound,update_.layout))return false;
            for(const auto& b:classifier_.buffers)if(!state.live(b))return false;
            for(const auto& b:c.buffers)if(!state.live(b))return false;
            for(const auto& image:c.coefficients)if(!state.scene_image_descriptor_current(image))return false;
            return state.live(first_update_.parameters)&&state.live(first_update_.state)&&state.live(update_.parameters)&&state.live(update_.state);
        }catch(...){return false;}
    }
private:
    bool decline(NativeMToonProbeStage stage,NativeMToonProbeGraphFailure why)noexcept{
        if(!failed_){failed_stage_=stage;failure_=why;}failed_=true;return false;
    }
    VkCommandBuffer command_{};std::uint64_t serial_=0;unsigned roots_=0,updates_=0;
    bool classified_=false,failed_=false;
    NativeMToonProbeGraphFailure failure_=NativeMToonProbeGraphFailure::none;
    NativeMToonProbeStage failed_stage_=NativeMToonProbeStage::other;
    NativeMToonProbeClassifyMetadata classifier_;NativeMToonProbeCandidate convergence_;NativeMToonProbeUpdateMetadata first_update_,update_;
};
// Caller holds the source guard in the immediate post-forward callback.
inline NativeMToonProbeClassifyMetadata native_mtoon_observe_probe_classify(const NativeState& state,VkCommandBuffer command)noexcept{
    try{NativeMToonProbeClassifyMetadata f;f.bound=state.observed_bound(command,VK_PIPELINE_BIND_POINT_COMPUTE);
        if(native_mtoon_probe_stage(f.bound.pipeline)!=NativeMToonProbeStage::classify)return {};
        f.layout=state.observed_set_layout(f.bound,0);f.dispatch=state.observed_dispatch(command);
        for(unsigned i=0;i<7;++i)f.buffers[i]=state.observed_buffer_binding(f.bound,0,i);return f;
    }catch(...){return {};}
}
inline NativeMToonProbeUpdateMetadata native_mtoon_observe_probe_update(const NativeState& state,VkCommandBuffer command)noexcept{
    try{NativeMToonProbeUpdateMetadata f;f.bound=state.observed_bound(command,VK_PIPELINE_BIND_POINT_COMPUTE);
        if(native_mtoon_probe_stage(f.bound.pipeline)!=NativeMToonProbeStage::update)return {};
        f.layout=state.observed_set_layout(f.bound,0);f.dispatch=state.observed_dispatch(command);
        f.parameters=state.observed_buffer_binding(f.bound,0,0);f.state=state.observed_buffer_binding(f.bound,0,1);return f;
    }catch(...){return {};}
}
} // namespace evrm
