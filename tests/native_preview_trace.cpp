#include "../src/native_preview_trace.hpp"
#include <cstdio>
#include <stdexcept>
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    evrm::NativePreviewTraceHistory trace;
    need(!trace.ticket(0x551,evrm::NativePreviewTraceStage::Bindings,100),"unconfigured ancestry sampled a buffer");
    trace.configure(0x140000000ULL,44298240,0x551,0x12345678);
    need(!trace.ticket(0x552,evrm::NativePreviewTraceStage::Bindings,100),"unrelated buffer used preview sample budget");
    const auto first=trace.ticket(0x551,evrm::NativePreviewTraceStage::Bindings,100);need(bool(first),"preview source did not admit diagnostic sample");
    need(!trace.ticket(0x551,evrm::NativePreviewTraceStage::Bindings,101),"preview tracing was not rate limited");
    evrm::NativePreviewTraceSample value;value.path_mask=3;value.stack_depth=32;value.game_depth=2;value.groups[0]=27460;value.groups[1]=6492;value.elapsed_us=12;
    need(trace.record(first,value),"diagnostic sample not retained");const auto report=trace.report(125);
    need(report=="preview-ownership.sample stage=1 path-mask=3 stack-depth=32 game-depth=2 group-0=27460 group-1=6492 group-2=0 group-3=0 group-4=0 group-5=0 elapsed-us=12 age-ms=25\n","diagnostic report contains unexpected fields or lost its code groups");
    auto stale=trace.ticket(0x551,evrm::NativePreviewTraceStage::Inline,125);trace.configure(0x140000000ULL,44298240,0x552,0x12345679);
    need(!trace.record(stale,value),"retired buffer/context accepted a delayed sample");
    auto invalid=trace.ticket(0x552,evrm::NativePreviewTraceStage::Inline,125);value.groups[0]=0xffffffffu;
    need(!trace.record(invalid,value),"raw address/invalid code group entered diagnostics");
    value.groups[0]=27460;need(trace.report(120126).empty(),"expired trace history exported");
    for(unsigned i=0;i<80;++i){const auto ticket=trace.ticket(0x552,evrm::NativePreviewTraceStage::Bindings,2000u*i+2000);need(bool(ticket)&&trace.record(ticket,value),"bounded ring stopped retaining samples");}
    const auto ring=trace.report(160000);std::size_t rows=0;for(char c:ring)rows+=c=='\n';need(rows<=61&&rows>0,"trace history did not apply its age/ring bounds");
    trace.invalidate(0x551);need(bool(trace.ticket(0x552,evrm::NativePreviewTraceStage::Bindings,162000)),"unrelated retirement cleared current preview source");
    trace.invalidate(0x552);need(!trace.ticket(0x552,evrm::NativePreviewTraceStage::Inline,164000),"retired preview source remained active");
    need(evrm::native_preview_trace_path(27460)==1&&evrm::native_preview_trace_path(6492)==2&&evrm::native_preview_trace_path(42961)==4&&evrm::native_preview_trace_path(42048)==8&&evrm::native_preview_trace_path(1)==0,"static supported-game call paths were misclassified");
    std::puts("PASS preview ancestry: source filtering, rate/context/retirement, privacy and bounded history");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL preview trace: %s\n",e.what());return 1;}}
