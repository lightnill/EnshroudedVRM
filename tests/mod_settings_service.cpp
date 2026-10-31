#include "../src/mod_settings_service.hpp"
#include "../src/voice_inputs.hpp"
#include <windows.h>
#include <atomic>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
std::atomic<bool> voice_enabled{false};std::atomic<unsigned> tuning_smoothing{0};
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
template<class F>void until(F fn,const char* message){const auto end=GetTickCount64()+3000;do{if(fn())return;Sleep(5);}while(GetTickCount64()<end);require(false,message);}
}
namespace evrm {void request_voice_inputs(const ModSettings& settings)noexcept{voice_enabled.store(settings.voice_enabled);tuning_smoothing=settings.smoothing_ms;}}
int wmain(int argc,wchar_t** argv){try{
    unsigned polls=0;until([&]{return ++polls==1;},"successful poll was evaluated again");require(polls==1,"successful poll must finish the wait");
    require(argc==2,"fixture directory");const auto root=std::filesystem::path(argv[1]);const auto path=root/L"mod-settings.json";
    evrm::ModSettings from_file{true,94,80,120,0x77};require(evrm::write_mod_settings(path,from_file),"initial file");
    std::ofstream(root/L"settings.json")<<"launcher stays unchanged";
    evrm::ModSettings live{false,53,20,40,0x70};require(evrm::request_mod_settings(live),"early user choice");
    evrm::initialize_mod_settings_service(root);until([]{return evrm::mod_settings_loaded();},"loaded background settings");
    require(evrm::mod_settings_snapshot().strength==53&&!voice_enabled&&tuning_smoothing==40,"late file lost to live choice");
    until([&]{return evrm::read_mod_settings(path).settings.strength==53;},"early edit persisted");
    live.voice_enabled=true;live.voice_source=evrm::VoiceInputSource::Microphone;
    live.microphone_device=std::make_shared<const std::string>("fixture-device-b");
    for(unsigned n=0;n<5;++n){live.strength=60+n;require(evrm::request_mod_settings(live),"live update");Sleep(10);}
    require(voice_enabled&&evrm::mod_settings_snapshot().strength==64,"immediate live publication");
    require(evrm::read_mod_settings(path).settings.strength==53,"write debounced");
    until([&]{const auto saved=evrm::read_mod_settings(path);return saved.status==evrm::ModSettingsStatus::Loaded&&evrm::equal_mod_settings(saved.settings,live);},"last coherent edit persisted");
    // Seeing replacement bytes does not mean MoveFileEx has released its final
    // DELETE handle yet. Acquire the intentional save-blocking fixture lock only
    // after that short ownership transition, rather than racing one open call.
    HANDLE locked=INVALID_HANDLE_VALUE;
    until([&]{if(locked!=INVALID_HANDLE_VALUE)return true;
        locked=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,0,nullptr);
        if(locked==INVALID_HANDLE_VALUE){const auto error=GetLastError();require(error==ERROR_SHARING_VIOLATION||error==ERROR_LOCK_VIOLATION,"unexpected fixture lock error");}
        return locked!=INVALID_HANDLE_VALUE;},"fixture lock");
    live.voice_enabled=false;live.strength=65;require(evrm::request_mod_settings(live),"disable while saving");
    require(!voice_enabled,"disable immediate");until([]{return evrm::mod_settings_save_failed();},"save failure reported");
    require(evrm::read_mod_settings(path).settings.strength==64,"failed replace preserved settings");CloseHandle(locked);
    live.strength=66;evrm::request_mod_settings(live);
    // The atomic file replacement becomes observable before the worker returns
    // from that OS call and publishes its status. Both observations converge.
    until([&]{return evrm::read_mod_settings(path).settings.strength==66&&!evrm::mod_settings_save_failed();},"later save and recovery status converged");
    std::ifstream file(root/L"settings.json");std::string line;std::getline(file,line);require(line=="launcher stays unchanged","launcher preserved");
    std::cout<<"PASS background settings generation, live tuning/disable, debounce and atomic-save recovery\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
