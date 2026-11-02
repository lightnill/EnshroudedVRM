#include "../src/debug_report_policy.hpp"
#include "../src/debug_report_archive.hpp"
#include "../src/debug_report_log.hpp"
#include "../src/runtime_log.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
int main(int argc,char** argv){try{
    need(argc==2,"fixture folder required");const std::filesystem::path folder=argv[1];
    const auto actual_graphics=evrm::debug_report_graphics_json(R"({"graphics":{"windowPosition":{"x":123,"y":456},"windowSize":{"x":1920,"y":1080},"render":{"fog":"Ultra","voxelMeshQuality":"High","preset":"Custom","password":"SECRET_CANARY"}},"server":"SECRET_CANARY"})");
    need(actual_graphics.find("\"fog\":\"ultra\"")!=std::string::npos&&actual_graphics.find("\"voxelMeshQuality\":\"high\"")!=std::string::npos&&actual_graphics.find("1920")!=std::string::npos,"actual game render settings were lost");
    need(actual_graphics.find("SECRET_CANARY")==std::string::npos&&actual_graphics.find("windowPosition")==std::string::npos,"actual settings leaked unknown/private fields");
    evrm::DebugReportData input;input.mod_version="0.3.0-test";input.game_sha256=std::string(64,'2');
    input.utc="2026-11-01T22:30:00Z";input.request_ms=10000;input.collected_ms=10032;
    input.system.cpu_model="Intel(R) Core(TM) i7";input.system.logical_processors=16;input.system.ram_total=32ull<<30;input.system.ram_available=16ull<<30;
    input.system.gpu.model="NVIDIA GeForce RTX 3090 Ti";input.system.gpu.available=true;input.system.gpu.vendor=0x10de;input.system.gpu.device=0x2203;
    input.graphics_source_json=R"({"graphics":{"fogQuality":3,"shadowQuality":2,"password":"SECRET_CANARY","path":"C:\\Users\\Alice","server":"203.0.113.77"},"steam":"76561198012345678","mail":"alice@example.com"})";
    input.settings.available=input.settings.avatar_available=input.settings.appearance_available=true;input.settings.strength=85;input.settings.sensitivity=50;input.settings.smoothing_ms=60;
    input.model.available=true;input.model.vertices=1234;input.model.materials=8;
    input.scene.location.available=true;input.scene.location.local_world_player=true;input.scene.location.sample_ms=10020;
    input.scene.location.xyz={3507.75,1047.0,1981.75};
    const std::string raw="native-submit-stages samples=300 mean-ms=2.5 worst-ms=24.2 source=SECRET_CANARY endpoint=203.0.113.77";
    input.diagnostics=evrm::debug_report_safe_event(raw);
    need(input.diagnostics.find("24.2")!=std::string::npos&&input.diagnostics.find("SECRET")==std::string::npos,"safe timing fields lost or private free text retained");
    need(evrm::debug_report_safe_event("avatar.error C:\\Users\\Alice SECRET_CANARY 76561198012345678").empty(),"unknown/free-form log line was copied");
    need(evrm::debug_report_safe_event("native-submit-stages mean-ms=SECRET_CANARY").find("SECRET")==std::string::npos,"invalid numeric value leaked");
    const auto stages=evrm::debug_report_safe_event("native-submit-stages samples=300 mean-ms=2.5 worst-ms=24.2 capture={mean:1.25,worst:23.1} owner-verify={mean:0.25,worst:0.9} endpoint=SECRET_CANARY");
    need(stages.find("capture={mean:1.25,worst:23.1}")!=std::string::npos&&stages.find("owner-verify={mean:0.25,worst:0.9}")!=std::string::npos,"per-stage submission timings were discarded");
    need(evrm::debug_report_safe_event(stages)==stages&&stages.find("SECRET")==std::string::npos,"canonical stage event changed or leaked free text");
    const auto fallback=evrm::debug_report_safe_event("avatar-fallback.pose-gap episode=2 phase=view-pose reason=SECRET_CANARY reason-mask=32");
    need(fallback.find("phase=view-pose")!=std::string::npos&&fallback.find("reason-mask=32")!=std::string::npos&&fallback.find("SECRET")==std::string::npos,"fixed fallback categories were lost or private reason leaked");
    evrm::debug_report_log_event(raw,10000);evrm::debug_report_log_event("chat SECRET_CANARY",10001);
    need(evrm::debug_report_recent_log(10005).find("24.2")!=std::string::npos,"recent safe event unavailable");
    need(evrm::debug_report_recent_log(130006).find("24.2")==std::string::npos,"old safe event exceeded120second window");
    const auto files=evrm::make_debug_report_contents(input);
    std::vector<std::uint8_t> zip;
    const auto sink=[&](const std::uint8_t* bytes,std::size_t count){zip.insert(zip.end(),bytes,bytes+count);return true;};
    need(evrm::write_debug_report_zip(files,sink),"valid fixed-member report failed");
    std::ofstream output(folder/"report.zip",std::ios::binary);output.write(reinterpret_cast<const char*>(zip.data()),zip.size());output.close();
    auto invalid=files;invalid[0].name="../private.txt";zip.clear();
    need(!evrm::write_debug_report_zip(invalid,sink)&&zip.empty(),"unsafe archive path reached sink");
    invalid=files;invalid.push_back(files[0]);need(!evrm::write_debug_report_zip(invalid,sink),"duplicate archive name accepted");
    auto stale=input;stale.scene.location.sample_ms=9000;
    for(const auto& file:evrm::make_debug_report_contents(stale))if(file.name=="scene.json"){
        const std::string text(file.bytes.begin(),file.bytes.end());need(text.find("3507")==std::string::npos,"stale coordinates exported as current");}
    auto preview=input;preview.scene.location.local_world_player=false;
    for(const auto& file:evrm::make_debug_report_contents(preview))if(file.name=="scene.json"){
        const std::string text(file.bytes.begin(),file.bytes.end());need(text.find("3507")==std::string::npos,"menu/NPC coordinates exported");}
    evrm::DebugReportImage image;image.width=2;image.height=2;image.rgba={255,0,0,255,0,255,0,255,0,0,255,255,255,255,255,255};
    const auto png=evrm::encode_debug_report_png(image);const auto decoded=evrm::decode_debug_report_png(png);
    need(decoded.width==2&&decoded.height==2&&decoded.rgba==image.rgba,"PNG preview does not match encoded pixels");
    std::ofstream picture(folder/"sample.png",std::ios::binary);picture.write(reinterpret_cast<const char*>(png.data()),png.size());picture.close();
    auto broken=png;broken[45]^=1;need(evrm::decode_debug_report_png(broken).rgba.empty(),"corrupt image was previewed");
    image.width=0;need(evrm::encode_debug_report_png(image).empty(),"zero-size image accepted");
    for(std::size_t size=0;size<png.size();++size){const std::vector<std::uint8_t> truncated(png.begin(),png.begin()+size);need(evrm::decode_debug_report_png(truncated).rgba.empty(),"truncated PNG was previewed");}
    invalid=files;invalid[0].bytes.resize(evrm::debug_report_archive_limit+1);zip.clear();
    need(!evrm::write_debug_report_zip(invalid,sink)&&zip.empty(),"oversize archive reached sink");
    need(!evrm::write_debug_report_zip(files,[](const std::uint8_t*,std::size_t){return false;}),"sink failure was reported as a complete ZIP");
    auto sensitive=input;sensitive.system.cpu_model="C:\\Users\\Alice SECRET_CANARY";sensitive.system.gpu.model="NVIDIA alice@example.com";
    sensitive.mod_version="C:\\Users\\Alice";sensitive.scene.location.xyz[0]=std::numeric_limits<double>::quiet_NaN();
    for(const auto& file:evrm::make_debug_report_contents(sensitive)){
        const std::string value(file.bytes.begin(),file.bytes.end());need(value.find("SECRET")==std::string::npos&&value.find("Alice")==std::string::npos&&value.find("alice@example")==std::string::npos,"invalid metadata strings leaked");}
    const auto unknown=evrm::make_debug_report_contents(evrm::DebugReportData{});
    for(const auto& file:unknown)if(file.name=="system.json"){
        const std::string value(file.bytes.begin(),file.bytes.end());need(value.find("\"total\": 0")==std::string::npos,"unknown RAM was fabricated as zero");}
    for(const auto& file:unknown)if(file.name=="settings.json"){
        const std::string value(file.bytes.begin(),file.bytes.end());need(value.find("voice_enabled")==std::string::npos&&value.find("replacement_enabled")==std::string::npos,"unobserved settings were fabricated as defaults");}
    const auto disk_log=(folder/"bounded-runtime.log").wstring();SetEnvironmentVariableW(L"ENSHROUDED_VRM_LOG",disk_log.c_str());
    for(unsigned n=1;n<=10005;++n)evrm::log_event("tool-slots.applied count="+std::to_string(n));
    need(evrm::debug_report_recent_log(GetTickCount64()).find("count=10005")!=std::string::npos,"disk log limit discarded recent report evidence");
    std::cout<<"PASS safe fields/canaries, qualified in-game coordinates, bounded recent events, fixed ZIP members/CRC and exact PNG preview\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL report policy: "<<error.what()<<'\n';return 1;}}
