#include "../src/debug_report_log.hpp"
#include <cstdio>
#include <stdexcept>
void need(bool ok,const char* why){if(!ok)throw std::runtime_error(why);}
int main(){try{
    const auto safe=evrm::debug_report_safe_event("preview-ownership.sample stage=1 path-mask=3 stack-depth=32 game-depth=2 group-0=27460 group-1=6492 group-2=0 elapsed-us=12 age-ms=25 entity=76561198012345678 pointer=140734799806464 path=C:\\Users\\PRIVATE_CANARY server=203.0.113.10");
    need(safe=="preview-ownership.sample stage=1 path-mask=3 stack-depth=32 game-depth=2 group-0=27460 group-1=6492 group-2=0 elapsed-us=12 age-ms=25","safe GPU ancestry was lost or extra private data exported");
    need(evrm::debug_report_safe_event(safe)==safe,"canonical ancestry changed during archive validation");
    for(const auto* bad:{"preview-ownership.sample stage=4 path-mask=1","preview-ownership.sample stage=1 path-mask=16","preview-ownership.sample stage=1 path-mask=1 group-0=76561198012345678","preview-ownership.sample stage=1 path-mask=1 group-0=72886","preview-ownership.sample stage=1 path-mask=1 group-0=1.5","preview-ownership.sample stage=1 path-mask=1 stack-depth=65"})
        need(evrm::debug_report_safe_event(bad).empty(),"out-of-contract ancestry field was exported");
    std::puts("PASS preview trace: fixed bounded fields and private canaries");return 0;
}catch(const std::exception& e){std::fprintf(stderr,"FAIL preview trace export: %s\n",e.what());return 1;}}
