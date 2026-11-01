// Exercise the actual shared submission-admission expression with an otherwise
// valid recorded frame. A disabled permit keeps original body/shadow draws.
#include "../src/fallback_diagnostics.hpp"
#include <windows.h>
#include <cstdint>
#include <iostream>
#include <stdexcept>
struct Buffer {struct {std::uint64_t buffer=42,offset=0;}info;unsigned generation=2;explicit operator bool()const{return info.buffer!=0;}};
struct Pose {unsigned asset=7;std::uint64_t published_milliseconds=GetTickCount64();};
struct Bundle {
    bool invalid=false,recorded=true,ended=true;
    unsigned visibility_passes=1,gbuffer_passes=1,visibility_draws=1,gbuffer_draws=1;
    Buffer gbuf_camera,owner,arena;struct {unsigned permit[4]={0,0,0,1};}data;
    Pose* pose=nullptr;
};
bool admitted(bool replacement_enabled,bool eligible,bool same_model=true){
    (void)replacement_enabled; // The retained pre-feature negative control lacks this input.
    const bool shape=true;struct {bool eligible=true,tainted=false;}recording;recording.eligible=eligible;
    Pose submitted,current;Bundle b;b.pose=&submitted;if(!same_model)current.asset=8;
    const auto* latest=&current;evrm::FallbackDecision decision;using Gate=evrm::FallbackGate;
    // Generated directly from production native_backend.cpp, never restated.
#include "native_replacement_admission.inc"
    return ready;
}
int main(){try{
    if(!admitted(true,true))throw std::runtime_error("valid replacement frame rejected");
    if(admitted(false,true))throw std::runtime_error("disabled replacement admitted an already-recorded frame");
    if(admitted(true,false)||admitted(false,false))throw std::runtime_error("disabled recording admitted after re-enable");
    if(admitted(true,true,false))throw std::runtime_error("toggle bypassed existing model identity guard");
    std::cout<<"PASS actual common submit permit: disabled queued frame, disabled recording, re-enable and model identity\n";
    return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
