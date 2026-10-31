#include "../src/voice_profile_io.hpp"
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
namespace {
void need(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
void write(const std::filesystem::path& path,const std::string& value){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<value;need(bool(file),"fixture write failed");}
std::string valid(){std::string result="{\"schemaVersion\":1,\"featureConvention\":\"";result+=evrm::voice_feature_convention;
    result+="\",\"profiles\":{";const char* names[]={"a","i","u","e","o"};
    for(unsigned n=0;n<5;++n){if(n)result+=",";result+="\"";result+=names[n];result+="\":[[0,1,2,3,4,5,6,7,8,9,10,11]]";}
    return result+="}}";}
void fallback(const std::filesystem::path& path){const auto result=evrm::load_voice_profiles(path);
    need(!result.custom&&result.diagnostic&&*result.diagnostic&&evrm::validate_voice_profiles(result.profiles)&&result.profiles.count==45,"invalid optional data did not fall back atomically");}
}
int main(){
    wchar_t temporary[MAX_PATH]{};need(GetTempPathW(MAX_PATH,temporary)!=0,"temporary path unavailable");
    const auto folder=std::filesystem::path(temporary)/(L"evrm-profiles-"+std::to_wstring(GetCurrentProcessId())+L"-"+std::to_wstring(GetTickCount64()));
    std::filesystem::create_directory(folder);const auto path=folder/L"profiles.json";
    auto result=evrm::load_voice_profiles(path);need(!result.custom&&!result.diagnostic&&evrm::validate_voice_profiles(result.profiles),"absent optional file should be normal defaults");
    write(path,valid());result=evrm::load_voice_profiles(path);need(result.custom&&!result.diagnostic&&result.profiles.count==5,"valid optional profile file was ignored");
    auto wrong=valid();wrong.replace(wrong.find("c1c12-v1"),8,"c1c12-v2");write(path,wrong);fallback(path);
    write(path,"not json");fallback(path);write(path,std::string(65537,' '));fallback(path);
    write(path,std::string(1000,'[')+std::string(1000,']'));fallback(path);
    wrong=valid();wrong.replace(wrong.find("[0,1,2"),6,"[1000,1,2");write(path,wrong);fallback(path);
    wrong=valid();wrong.replace(wrong.find("\"o\""),3,"\"x\"");write(path,wrong);fallback(path);
    wrong=valid();wrong.replace(wrong.find("0,1,2"),5,"null,1,2");write(path,wrong);fallback(path);
    fallback(folder);fallback(std::filesystem::path(L"\\\\example.invalid\\voice.json"));
    std::filesystem::remove(path);std::filesystem::remove(folder);
    std::puts("PASS bounded optional profiles, exact convention and atomic owned-default fallback");
}
