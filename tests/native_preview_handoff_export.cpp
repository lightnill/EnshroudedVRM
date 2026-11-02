#include "../src/native_preview_handoff.hpp"
#include "../src/debug_report_log.hpp"
#include <iostream>
#include <stdexcept>
static void require(bool condition,const char* reason){if(!condition)throw std::runtime_error(reason);}
int main(){try{
    evrm::NativePreviewHandoff history;
    const auto source=history.configure(0x100001234,0x200005678,0x300009abc,0x40000def0);
    for(unsigned i=0;i<900;++i)history.request(source,1,true,i);
    const auto label=history.render(source,true,true,901);
    history.enqueue(0x500001234,label,902);
    const auto active=history.execute(0x500001234,903);
    require(history.gpu(source.buffer,active,5,904),"missing diagnostic GPU association");
    const auto text=history.report(1000);
    require(text.find("preview-handoff.sample stage=5 origin=1 request=")!=std::string::npos,"queued handoff missing from report");
    require(text.find("request=900")==std::string::npos,"process correlation sequence exported instead of report-local alias");
    for(const auto* secret:{"4294971956","8589956728","12884941436","17179926256","21474841140","0x"})
        require(text.find(secret)==std::string::npos,"private pointer exported");
    require(history.report(121905).empty(),"expired handoff history exported");
    const std::string canonical="preview-handoff.sample stage=5 origin=1 request=2 flags=127 age-ms=96";
    require(evrm::debug_report_safe_event(canonical+" pointer=140734799806464 player=76561198012345678 path=C:\\Users\\PRIVATE_CANARY")==canonical,"safe handoff lost or private extra fields exported");
    require(evrm::debug_report_safe_event(canonical)==canonical,"canonical handoff failed archive revalidation");
    for(const auto* bad:{"preview-handoff.sample stage=7 origin=1 request=2 flags=127 age-ms=96",
        "preview-handoff.sample stage=5 origin=7 request=2 flags=127 age-ms=96",
        "preview-handoff.sample stage=5 origin=1 request=385 flags=127 age-ms=96",
        "preview-handoff.sample stage=5 origin=1 request=2 flags=256 age-ms=96",
        "preview-handoff.sample stage=5 origin=1 request=2 flags=127 age-ms=120001",
        "preview-handoff.sample stage=5 origin=1 request=2.5 flags=127 age-ms=96"})
        require(evrm::debug_report_safe_event(bad).empty(),"out-of-contract handoff exported");
    std::cout<<"bounded request aliases, history age, strict fields and private pointer/account/path canaries passed\n";
}catch(const std::exception& error){std::cerr<<error.what()<<'\n';return 1;}return 0;}
