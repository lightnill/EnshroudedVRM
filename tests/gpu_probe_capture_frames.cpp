#include "../src/gpu_probe_capture_frames.hpp"
#include <iostream>
#include <stdexcept>
void check(bool ok,const char*message){if(!ok)throw std::runtime_error(message);}
int main(){try{
 check(evrm::probe_capture_frames(L"")==std::set<std::uint64_t>{120,180,240},"legacy defaults changed");
 check(evrm::probe_capture_frames(L"0,600,600")==std::set<std::uint64_t>{0,600},"numeric frame parsing changed");
 check(evrm::probe_capture_frames(L"none").empty(),"portable mode still exports GPU captures");
 for(const auto*bad:{L"no",L"1,x",L"1000001",L"-1",L"0;600"}){bool rejected=false;try{evrm::probe_capture_frames(bad);}catch(...){rejected=true;}check(rejected,"invalid capture frame accepted");}
 std::cout<<"Capture frames: legacy defaults, explicit no-export mode and numeric rejection passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
