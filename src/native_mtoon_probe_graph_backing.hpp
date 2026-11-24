#pragma once
#include "native_mtoon_probe_graph.hpp"
#include "native_mtoon_probe_backing.hpp"
namespace evrm {
namespace mtoon_graph_backing_detail {
inline bool buffers(const NativeMToonProbeGraph& graph,const NativeBufferMemory& memory)noexcept{
    if(!graph.has_classifier())return false;const auto& inputs=graph.classifier().buffers;
    for(unsigned i=0;i<inputs.size();++i){const auto& a=inputs[i];if(!memory.valid(a.info.buffer,a.info.offset,a.info.range))return false;
        for(unsigned j=0;j<i;++j){const auto& b=inputs[j];if(!memory.disjoint(a.info.buffer,a.info.offset,a.info.range,b.info.buffer,b.info.offset,b.info.range))return false;}}
    return true;
}
inline bool parameters(const NativeMToonProbeGraph& graph,const NativeBufferBinding& parameter,const NativeBufferMemory& memory)noexcept{
    if(!graph.has_classifier()||!parameter||!memory.valid(parameter.info.buffer,parameter.info.offset,parameter.info.range))return false;
    // The parameter slices may share a read-only arena. None may overlap the
    // mutable state, bounds, list or counter roles, including physical aliases.
    for(unsigned n=1;n<7;++n){const auto& b=graph.classifier().buffers[n];
        if(!memory.disjoint(parameter.info.buffer,parameter.info.offset,parameter.info.range,b.info.buffer,b.info.offset,b.info.range))return false;}
    return true;
}
}
// Capture immediately after the classifier, before subsequent native stages.
// A later capture cannot retroactively certify the classifier's input history.
class NativeMToonProbeBufferBacking {
public:
    explicit operator bool()const noexcept{return bool(graph_);}
    bool for_graph(const std::shared_ptr<const NativeMToonProbeGraph>& graph)const noexcept{return graph_&&graph_==graph;}
    bool current(const NativeBufferMemory& buffers,const NativeImageMemory& images)const noexcept{
        if(!graph_||buffers_!=&buffers||images_!=&images||!mtoon_graph_backing_detail::buffers(*graph_,buffers))return false;
        for(const auto& token:tokens_)if(!images.current(token))return false;
        for(const auto& binding:native_only_)if(!images.current(binding))return false;return true;
    }
private:
    friend NativeMToonProbeBufferBacking native_mtoon_probe_buffer_backing(std::shared_ptr<const NativeMToonProbeGraph>,const NativeBufferMemory&,const NativeImageMemory&)noexcept;
    // Only roles0..2 are read by the full snapshot. Unread counter/list
    // roles3..6 retain metadata/lifetimes without granting read permission.
    std::shared_ptr<const NativeMToonProbeGraph> graph_;std::array<NativeBufferBacking,3> tokens_;
    std::array<NativeScopedBufferBinding,4> native_only_;
    const NativeBufferMemory* buffers_=nullptr;const NativeImageMemory* images_=nullptr;
};
inline NativeMToonProbeBufferBacking native_mtoon_probe_buffer_backing(std::shared_ptr<const NativeMToonProbeGraph> graph,
    const NativeBufferMemory& buffers,const NativeImageMemory& images)noexcept{
    try{if(!graph||!mtoon_graph_backing_detail::buffers(*graph,buffers))return {};
        NativeMToonProbeBufferBacking out;const auto& inputs=graph->classifier().buffers;
        for(unsigned n=0;n<7;++n){const auto& b=inputs[n];const bool accepted=n<3?
            images.exclusive_buffer(b.info.buffer,b.info.offset,b.info.range,out.tokens_[n]):
            images.scoped_buffer_binding(b.info.buffer,b.info.offset,b.info.range,out.native_only_[n-3]);
            if(!accepted)return {};}
        out.graph_=std::move(graph);out.buffers_=&buffers;out.images_=&images;return out;
    }catch(...){return {};}
}
// Host backing facts tied to one retained graph. These do not prove GPU
// execution/initialization, queue ownership or lifetime beyond native retirement.
class NativeMToonProbeGraphBacking {
public:
    explicit operator bool()const noexcept{return graph_&&bool(coefficients_)&&bool(classifier_);}
    bool for_graph(const std::shared_ptr<const NativeMToonProbeGraph>& graph)const noexcept{return graph_&&graph_==graph;}
    // Observe each of the two native Update stages immediately and in order.
    // No retry or final-stage-only capture may refresh an earlier history.
    bool updated(const NativeBufferMemory& buffers,const NativeImageMemory& images)noexcept{
        if(update_failed_)return false;
        try{
            if(!graph_||graph_->update_count()!=observed_updates_+1||observed_updates_>=2||
                !early_current(buffers,images)||!updates_current(images))return fail_update();const auto& p=graph_->update().parameters;
            if(!mtoon_graph_backing_detail::parameters(*graph_,p,buffers)||
               !images.exclusive_buffer(p.info.buffer,p.info.offset,p.info.range,update_parameters_[observed_updates_]))return fail_update();
            ++observed_updates_;if(observed_updates_==1)return true;
            return backing_current(buffers,images)||fail_update();
        }catch(...){return fail_update();}
    }
    bool backing_current(const NativeBufferMemory& buffers,const NativeImageMemory& images)const noexcept{
        return !update_failed_&&observed_updates_==2&&graph_&&graph_->complete()&&early_current(buffers,images)&&updates_current(images)&&
            mtoon_graph_backing_detail::parameters(*graph_,graph_->first_update().parameters,buffers)&&
            mtoon_graph_backing_detail::parameters(*graph_,graph_->update().parameters,buffers);
    }
    bool sources_current(const NativeState& state,const NativeBufferMemory& buffers,const NativeImageMemory& images)const noexcept{
        return graph_&&graph_->current(state)&&backing_current(buffers,images);
    }
private:
    friend NativeMToonProbeGraphBacking native_mtoon_probe_graph_backing(std::shared_ptr<const NativeMToonProbeGraph>,const NativeBufferMemory&,const NativeImageMemory&,const NativeMToonProbeBufferBacking&)noexcept;
    std::shared_ptr<const NativeMToonProbeGraph> graph_;NativeMToonProbeBacking coefficients_;NativeMToonProbeBufferBacking classifier_;
    NativeBufferBacking coefficient_parameters_;std::array<NativeBufferBacking,2> update_parameters_;
    unsigned observed_updates_=0;bool update_failed_=false;
    const NativeBufferMemory* buffers_=nullptr;const NativeImageMemory* images_=nullptr;
    bool early_current(const NativeBufferMemory& buffers,const NativeImageMemory& images)const noexcept{
        return graph_&&buffers_==&buffers&&images_==&images&&mtoon_graph_backing_detail::buffers(*graph_,buffers)&&
            classifier_.current(buffers,images)&&native_mtoon_probe_backing_current(coefficients_,buffers,images)&&images.current(coefficient_parameters_)&&
            mtoon_graph_backing_detail::parameters(*graph_,graph_->convergence().metadata().buffers[0],buffers);
    }
    bool fail_update()noexcept{update_failed_=true;return false;}
    bool updates_current(const NativeImageMemory& images)const noexcept{
        for(unsigned n=0;n<observed_updates_;++n)if(!images.current(update_parameters_[n]))return false;return true;
    }
};
inline NativeMToonProbeGraphBacking native_mtoon_probe_graph_backing(std::shared_ptr<const NativeMToonProbeGraph> graph,
    const NativeBufferMemory& buffers,const NativeImageMemory& images,const NativeMToonProbeBufferBacking& classifier)noexcept{
    try{
        if(!graph||!graph->has_sources()||!classifier.for_graph(graph)||!classifier.current(buffers,images))return {};
        const auto coefficients=native_mtoon_probe_backing(graph->convergence(),buffers,images);if(!coefficients)return {};
        const auto& p=graph->convergence().metadata().buffers[0];NativeMToonProbeGraphBacking result;
        if(!mtoon_graph_backing_detail::parameters(*graph,p,buffers)||!images.exclusive_buffer(p.info.buffer,p.info.offset,p.info.range,result.coefficient_parameters_))return {};
        result.graph_=std::move(graph);result.coefficients_=coefficients;result.classifier_=classifier;result.buffers_=&buffers;result.images_=&images;return result;
    }catch(...){return {};}
}
} // namespace evrm
