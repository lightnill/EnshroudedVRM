#include "../src/voice_inputs.hpp"
#include <cstdio>
#include <cstdlib>
void need(bool yes,const char* why){if(!yes){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
struct Routes {bool game=false,microphone=false,mic_available=true;evrm::VoiceInputRouter* router=nullptr;bool change_during_read=false;};
int main(){
    Routes routes;
    evrm::VoiceInputCallbacks callbacks{&routes,
        [](void* p,bool on,evrm::VoiceAnalysisTuning)noexcept{static_cast<Routes*>(p)->game=on;},
        [](void* p,bool on,evrm::VoiceAnalysisTuning,evrm::MicrophoneDeviceId)noexcept{auto& r=*static_cast<Routes*>(p);r.microphone=on&&r.mic_available;return !on||r.mic_available;},
        [](void* p)noexcept{auto& r=*static_cast<Routes*>(p);evrm::MouthSnapshot out;out.activity=r.game?evrm::VoiceActivity::Talking:evrm::VoiceActivity::Off;out.weights[0]=r.game?.3f:0;
            if(r.change_during_read){r.change_during_read=false;evrm::ModSettings next;next.voice_enabled=true;next.voice_source=evrm::VoiceInputSource::Microphone;r.router->request(next);}return out;},
        [](void* p)noexcept{const auto& r=*static_cast<Routes*>(p);evrm::MouthSnapshot out;out.activity=r.microphone?evrm::VoiceActivity::Talking:evrm::VoiceActivity::UnsupportedSource;out.weights[1]=r.microphone?.8f:0;return out;}};
    evrm::VoiceInputRouter router(callbacks);routes.router=&router;evrm::ModSettings settings;settings.voice_enabled=true;
    need(router.request(settings)&&routes.game&&!routes.microphone,"default game source routing");need(router.snapshot().weights[0]==.3f,"game source output");
    settings.voice_source=evrm::VoiceInputSource::Microphone;need(router.request(settings)&&!routes.game&&routes.microphone,"microphone source routing");
    need(router.snapshot().weights[1]==.8f&&router.snapshot().weights[0]==0,"game/microphone output mixed");
    routes.mic_available=false;router.request(settings);need(router.snapshot().activity==evrm::VoiceActivity::UnsupportedSource,"failed microphone reused old output or silently selected game");
    settings.voice_enabled=false;router.request(settings);need(!routes.game&&!routes.microphone&&router.snapshot().activity==evrm::VoiceActivity::Off,"Off did not stop both sources");
    routes.mic_available=true;settings.voice_source=evrm::VoiceInputSource::Game;settings.voice_enabled=true;router.request(settings);
    routes.change_during_read=true;for(float w:router.snapshot().weights)need(w==0,"source switch during render read published stale source weights");
    need(router.snapshot().weights[1]==.8f,"new source failed after read race");
    std::puts("PASS explicit game/mic routing, default/off/failure isolation and source-switch snapshot race; no device opened");
}
