#include "../src/mod_settings.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <thread>
namespace {
void require(bool condition,const char* message){if(!condition)throw std::runtime_error(message);}
void write(const std::filesystem::path& path,const std::string& bytes){std::ofstream f(path,std::ios::binary);f<<bytes;}
}
int wmain(int argc,wchar_t** argv){try{
    require(argc==2,"fixture directory required");const auto root=std::filesystem::path(argv[1]);
    const auto path=root/L"mod-settings.json";evrm::ModSettings defaults;
    require(!defaults.voice_enabled&&defaults.strength==85&&defaults.sensitivity==50&&defaults.smoothing_ms==60&&defaults.hotkey==0x77,"F8 defaults");
    require(defaults.voice_source==evrm::VoiceInputSource::Game&&evrm::microphone_device_id(defaults).empty(),"game/default-microphone defaults");
    require(evrm::read_mod_settings(path).status==evrm::ModSettingsStatus::Missing,"missing defaults");
    write(root/L"settings.json","launcher settings stay private and unchanged");
    evrm::ModSettings chosen{true,73,12,140,0x7b};require(evrm::write_mod_settings(path,chosen),"atomic initial write");
    auto loaded=evrm::read_mod_settings(path);require(loaded.status==evrm::ModSettingsStatus::Loaded&&evrm::pack_mod_settings(loaded.settings)==evrm::pack_mod_settings(chosen),"round trip");
    require(evrm::unpack_mod_settings(evrm::pack_mod_settings(chosen)).hotkey==0x7b,"packed hotkey");
    auto mic=chosen;mic.voice_source=evrm::VoiceInputSource::Microphone;
    mic.microphone_device=std::make_shared<const std::string>("{capture-device-fixture}");
    require(evrm::write_mod_settings(path,mic),"microphone settings save");loaded=evrm::read_mod_settings(path);
    require(loaded.status==evrm::ModSettingsStatus::Loaded&&evrm::equal_mod_settings(loaded.settings,mic),"schema2 device/source round trip");
    require(evrm::unpack_mod_settings(evrm::pack_mod_settings(mic)).voice_source==evrm::VoiceInputSource::Microphone,"packed scalar source");
    for(const auto& id:{std::string(1025,'a'),std::string("a\0b",3),std::string("\xff",1)}){
        auto invalid_mic=mic;invalid_mic.microphone_device=std::make_shared<const std::string>(id);
        require(!evrm::valid_mod_settings(invalid_mic)&&!evrm::write_mod_settings(path,invalid_mic),"invalid endpoint ID accepted");}
    const std::string valid=R"({"schemaVersion":1,"voiceEnabled":false,"mouthStrength":85,"sensitivity":50,"smoothingMs":60,"menuHotkey":45})";
    write(path,valid);loaded=evrm::read_mod_settings(path);
    require(loaded.status==evrm::ModSettingsStatus::Loaded&&loaded.settings.hotkey==0x77&&loaded.settings.strength==85,"legacy Insert migrates to F8 without losing tuning");
    require(loaded.settings.voice_source==evrm::VoiceInputSource::Game&&evrm::microphone_device_id(loaded.settings).empty(),"legacy settings activated direct microphone");
    const std::string schema2=R"({"schemaVersion":2,"voiceEnabled":false,"mouthStrength":85,"sensitivity":50,"smoothingMs":60,"menuHotkey":119,"voiceSource":"microphone","microphoneDeviceId":"fixture"})";
    for(const auto& replacement:{std::pair<const char*,const char*>{"\"microphone\"","\"automatic\""},
        {"\"microphoneDeviceId\":\"fixture\"","\"microphoneDeviceId\":4"},
        {"\"microphoneDeviceId\":\"fixture\"","\"microphoneDeviceId\":\"fixture\",\"microphoneDeviceId\":\"other\""},
        {"\"fixture\"","\"bad\\u0000id\""}}){
        auto bad=schema2;const auto at=bad.find(replacement.first);bad.replace(at,std::char_traits<char>::length(replacement.first),replacement.second);write(path,bad);
        loaded=evrm::read_mod_settings(path);require(loaded.status==evrm::ModSettingsStatus::Invalid&&!loaded.settings.voice_enabled&&
            loaded.settings.voice_source==evrm::VoiceInputSource::Game,"malformed schema2 activated a source");}
    auto legacy=chosen;legacy.hotkey=45;require(!evrm::valid_mod_settings(legacy)&&!evrm::write_mod_settings(path,legacy),"Insert cannot be newly assigned");
    for(const auto& bad:{std::string("[]"),std::string("{}"),std::string("null"),valid.substr(0,valid.size()-1),std::string(4097,' '),
        std::string(100,'[')+"0"+std::string(100,']'),std::string(R"({"schemaVersion":1,"schemaVersion":1,"voiceEnabled":false,"mouthStrength":85,"sensitivity":50,"smoothingMs":60,"menuHotkey":45})")}){
        write(path,bad);loaded=evrm::read_mod_settings(path);require(loaded.status==evrm::ModSettingsStatus::Invalid&&!loaded.settings.voice_enabled,"invalid safe defaults");}
    for(const auto& replacement: {std::pair<const char*,const char*>{"85","101"},{"50","-1"},{"60","19"},{"60","141"},{"45","27"},{"45","1"},{"false","1"},{"85","85.0"},{"50","1e999"}}){
        auto bad=valid;const auto at=bad.find(replacement.first);bad.replace(at,std::char_traits<char>::length(replacement.first),replacement.second);write(path,bad);
        require(evrm::read_mod_settings(path).status==evrm::ModSettingsStatus::Invalid,"type/range reject");}
    auto future=valid;future.replace(future.find(":1"),2,":3");write(path,future);
    require(evrm::read_mod_settings(path).status==evrm::ModSettingsStatus::UnsupportedVersion,"future schema");
    require(evrm::write_mod_settings(path,chosen),"replace malformed file only on explicit save");
    std::ifstream launcher(root/L"settings.json");std::string line;std::getline(launcher,line);require(line=="launcher settings stay private and unchanged","launcher unchanged");
    const auto before=evrm::pack_mod_settings(evrm::read_mod_settings(path).settings);
    evrm::ModSettings invalid=chosen;invalid.hotkey=27;require(!evrm::write_mod_settings(path,invalid),"invalid write rejected");
    require(evrm::pack_mod_settings(evrm::read_mod_settings(path).settings)==before,"failed write preserves file");
    std::filesystem::create_directory(root/L"directory.json");require(!evrm::write_mod_settings(root/L"directory.json",chosen),"directory not replaced");
    require(evrm::read_mod_settings(L"\\\\localhost\\share\\mod-settings.json").status==evrm::ModSettingsStatus::Unavailable,"remote refused");
    evrm::ModSettingsMailbox mailbox;const auto initial=mailbox.ticket();
    require(mailbox.accept_loaded(chosen)&&mailbox.snapshot().voice_enabled,"initial file accepted");
    require(!mailbox.accept_loaded(defaults),"load once");
    evrm::ModSettingsMailbox edited;require(edited.update(defaults)&&edited.ticket()!=initial,"equal user edit carries generation");
    require(!edited.accept_loaded(chosen)&&!edited.snapshot().voice_enabled,"late load cannot overwrite choice");
    require(!edited.update(invalid)&&!edited.snapshot().voice_enabled,"invalid update atomic");
    evrm::ModSettingsMailbox coherent;require(coherent.update(mic),"device mailbox update");
    const auto device=mic.microphone_device;
    require(coherent.snapshot().microphone_device==device,"render snapshot copied endpoint storage");
    auto alternative=defaults;alternative.strength=91;
    std::atomic<bool> done{false},mixed{false};
    std::thread writer([&]{for(unsigned n=0;n<2000;++n)coherent.update(n&1?mic:alternative);done=true;});
    do{const auto value=coherent.snapshot();if(!evrm::equal_mod_settings(value,mic)&&!evrm::equal_mod_settings(value,alternative))mixed=true;}while(!done.load());
    writer.join();require(!mixed,"source/device/tuning mailbox snapshot mixed generations");
    std::cout<<"PASS settings defaults, schema, bounds, atomic replacement, launcher preservation and startup generation\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
