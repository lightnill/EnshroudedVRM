#if __has_include("../src/player_assignments.hpp")
#include "../src/player_assignments.hpp"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
namespace {
void need(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::string bytes(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);return {std::istreambuf_iterator<char>(file),{}};}
void write(const std::filesystem::path& path,const std::string& text){std::ofstream file(path,std::ios::binary|std::ios::trunc);file<<text;}
evrm::PlayerAssignments preferences(){evrm::PlayerAssignments value;for(unsigned n=0;n<32;++n)value.secret[n]=static_cast<std::uint8_t>(n);return value;}
}
int wmain(int argc,wchar_t** argv){try{
    using namespace evrm;need(argc==3,"mode and owned fixture folder required");const std::wstring mode=argv[1];const auto directory=std::filesystem::path(argv[2]);const auto file=directory/L"player-model-assignments.json";
    auto data=preferences();const auto first=player_assignment_token(data.secret,0x0123456789abcdefULL),second=player_assignment_token(data.secret,0xfedcba9876543210ULL);
    if(mode==L"tokens"){
        need(first=="9e1c3453badb47877efd43e7ef5b72b8da3ba73efb1b9c191c019bc35fbdd8ce","HMAC does not match independent domain-separated vector");
        need(first!=second&&first.size()==64,"two accounts share an assignment token");
        auto other=data.secret;other[0]^=0x80;need(player_assignment_token(other,0x0123456789abcdefULL)!=first,"installation secret does not separate identities");
        need(player_assignment_token({},0x0123456789abcdefULL).empty()&&player_assignment_token(data.secret,0).empty(),"invalid identity/secret generated a global token");
        std::array<std::uint8_t,32> random{},again{};need(make_player_assignment_secret(random)&&make_player_assignment_secret(again)&&random!=again,"private secret generation failed");
    }else if(mode==L"persistence"){
        need(read_player_assignments(file).status==PlayerAssignmentsStatus::Missing,"missing settings did not report missing");
        need(data.entries.empty(),"remote player enabled by default");
        need(set_player_assignment(data,{first,"C:/avatars/main.vrm",true}),"first assignment rejected");
        need(set_player_assignment(data,{second,"C:/avatars/remote.vrm",true}),"second assignment rejected");
        need(set_player_assignment(data,{second,"C:/avatars/remote.vrm",false}),"None toggle rejected");
        need(data.entries[0].replacement_enabled&&!data.entries[1].replacement_enabled&&data.entries[1].path=="C:/avatars/remote.vrm","per-player toggle erased another choice or remembered path");
        need(create_player_assignments(file,data),"first atomic save failed");
        auto loaded=read_player_assignments(file);need(loaded.status==PlayerAssignmentsStatus::Loaded&&loaded.preferences.secret==data.secret&&loaded.preferences.entries.size()==2,"restart did not restore private assignments");
        need(loaded.preferences.entries[0].path=="C:/avatars/main.vrm"&&!loaded.preferences.entries[1].replacement_enabled,"restart changed selected models or explicit None");
        const auto before=bytes(file);need(!create_player_assignments(file,preferences())&&bytes(file)==before,"create overwrote existing choices");
        need(bytes(file).find("0123456789abcdef")==std::string::npos&&bytes(file).find("fedcba9876543210")==std::string::npos,"raw account key persisted");
        need(set_player_assignment(loaded.preferences,{second,"C:/avatars/new.vrm",true})&&write_player_assignments(file,loaded.preferences),"updated assignment did not save");
        need(read_player_assignments(file).preferences.entries[0].path=="C:/avatars/main.vrm","remote save changed local model");
    }else if(mode==L"invalid"){
        need(!set_player_assignment(data,{first,"",true}),"enabled empty model accepted");
        need(!set_player_assignment(data,{first,"//server/private.vrm",true}),"network model path accepted");
        need(set_player_assignment(data,{first,"C:/avatars/model.vrm",false}),"valid disabled model rejected");
        auto duplicate=data;const PlayerAssignment repeated{first,"C:/avatars/model.vrm",false};duplicate.entries.push_back(repeated);need(!write_player_assignments(file,duplicate),"duplicate token saved");
        need(create_player_assignments(file,data),"valid preferences could not be created");
        const auto valid=bytes(file);
        for(const auto& bad:{std::string("{}"),std::string("[]"),std::string(4*1024*1024+1,' '),std::string(R"({"schemaVersion":1,"schemaVersion":1,"secret":"bad","entries":[]})")}){
            write(file,bad);need(read_player_assignments(file).status==PlayerAssignmentsStatus::Invalid,"malformed preferences accepted");
            need(!write_player_assignments(file,data)&&bytes(file)==bad,"corrupt preferences silently overwritten");
        }
        auto future=valid;auto at=future.find("\"schemaVersion\": 1");need(at!=std::string::npos,"fixture schema marker absent");future.replace(at,18,"\"schemaVersion\": 9");write(file,future);
        need(read_player_assignments(file).status==PlayerAssignmentsStatus::UnsupportedVersion&&!write_player_assignments(file,data)&&bytes(file)==future,"future preferences overwritten");
        need(!write_player_assignments(directory/L"absent"/L"settings.json",data),"missing directory falsely reported saved");
        write(file,valid);auto changed=data;changed.secret[0]^=1;need(!write_player_assignments(file,changed)&&bytes(file)==valid,"existing secret silently rotated");
    }else if(mode==L"capacity"){
        for(std::uint64_t n=1;n<=512;++n)need(set_player_assignment(data,{player_assignment_token(data.secret,n),"C:/avatars/remembered.vrm",n%2==0}),"valid assignment evicted or rejected before capacity");
        const auto first_path=data.entries.front().path,first_token=data.entries.front().token;
        need(!set_player_assignment(data,{player_assignment_token(data.secret,513),"C:/avatars/extra.vrm",true}),"full store silently evicted an assignment");
        need(data.entries.size()==512&&data.entries.front().token==first_token&&data.entries.front().path==first_path,"capacity erased first assignment");
        need(set_player_assignment(data,{first_token,"C:/avatars/updated.vrm",false})&&data.entries.size()==512,"existing assignment cannot be edited at capacity");
        need(write_player_assignments(file,data)&&read_player_assignments(file).preferences.entries.size()==512,"full valid store did not survive restart");
    }else throw std::runtime_error("unknown fixture mode");
    std::cout<<"PASS private player assignment behavior\n";return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
#else
#include <iostream>
int main(){std::cerr<<"FAIL private player assignment preferences are not implemented\n";return 1;}
#endif
