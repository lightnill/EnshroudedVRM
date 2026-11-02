// Reuse the actual GpuProbe's fake-driver fixture, never a graphics device.
#define main original_readback_lifetime_main
#include "gpu_readback_lifetime.cpp"
#undef main
#include "../src/native_preview_trace.hpp"
#include "../src/native_preview_handoff.hpp"
void trace_need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    NativeOwnerImage image;image.base=reinterpret_cast<std::uintptr_t>(GetModuleHandleW(nullptr));image.size=native_owner_profile::image_size;image.timestamp=native_owner_profile::image_timestamp;
    std::copy(std::begin(native_owner_profile::image_sha256),std::end(native_owner_profile::image_sha256),image.sha256.begin());
    {Fixture f;native_preview_trace_source(image,30,1);
        auto& handoff=native_preview_handoff_history();const auto now=GetTickCount64();const auto source=handoff.configure(1,2,3,30);
        handoff.request(source,1,true,now);handoff.enqueue(123,handoff.render(source,true,true,now),now);
        native_preview_handoff_thread().working=handoff.execute(123,now);
        f.p.sets_bound(f.cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,f.pl,0,3,f.sets,0,nullptr);f.p.draw_indexed_indirect(f.cmd,f.buf,0,1,sizeof(VkDrawIndexedIndirectCommand));
        trace_need(native_preview_trace_report(GetTickCount64()).find("stage=1 ")!=std::string::npos,"actual model bindings did not record preview ancestry");
        trace_need(f.p.capture_inline(f.cmd,0),"fake inline capture failed");trace_need(native_preview_trace_report(GetTickCount64()).find("stage=2 ")!=std::string::npos,"actual inline copy did not record preview ancestry");
        const auto links=handoff.report(GetTickCount64());
        trace_need(links.find("preview-handoff.sample stage=5 origin=1 ")!=std::string::npos&&links.find("preview-handoff.sample stage=6 origin=1 ")!=std::string::npos,"actual GPU binding/copy lost active native worker association");
        f.run();trace_need(!f.p.poll_async(f.q).buffers.empty(),"diagnostic altered completed native readback");}
    {Fixture f;native_preview_trace_source(image,30,2);f.run();trace_need(native_preview_trace_report(GetTickCount64()).find("stage=3 ")!=std::string::npos,"actual queued copy did not record preview ancestry");
        trace_need(!f.p.poll_async(f.q).buffers.empty(),"queued diagnostics changed native capture");f.p.buffer_destroyed(f.buf);
        trace_need(!native_preview_handoff_history().source().epoch,"actual buffer destruction retained handoff source");
        trace_need(!native_preview_trace_history().ticket(30,NativePreviewTraceStage::Inline,GetTickCount64()+3000),"actual buffer destruction retained preview trace source");}
    std::puts("PASS actual GPU observation/copy/retirement wiring with fake callbacks");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL preview trace wiring: %s\n",e.what());return 1;}}
