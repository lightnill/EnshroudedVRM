#include "../portable/launcher_core.hpp"
#include "../vendor/tinygltf/json.hpp"
#include <fstream>
#include <iostream>
#include <stdexcept>
using namespace evrm::portable;
using Json=nlohmann::json;
void check(bool value,const char*message){if(!value)throw std::runtime_error(message);}
template<class Fn>void rejects(Fn fn,const char*message){bool failed=false;try{fn();}catch(...){failed=true;}check(failed,message);}
void put(const fs::path&p,const std::string&data){fs::create_directories(p.parent_path());std::ofstream file(p,std::ios::binary);file<<data;check(bool(file),"fixture write failed");}
int main(int argc,char**argv){try{
 check(argc==2,"fixture directory argument required");
 check(quote_argument(L"C:\\Mod Folder\\game.exe")==L"\"C:\\Mod Folder\\game.exe\"","Windows path quoting missing");
 check(quote_argument(L"")==L"\"\"","empty argument quoting missing");
 check(quote_argument(L"a\"b\\")==L"\"a\\\"b\\\\\"","quote/backslash escaping differs");
 const std::wstring unicode=L"C:\\モデル & 100% !\\アバター.vrm";check(wide(utf8(unicode))==unicode,"Unicode round trip failed");
 for(const auto*name:{"Runtime/a.dll","Compatibility/profile.json","Avatars/README.txt"})check(safe_member(name),"ordinary manifest member rejected");
 for(const auto*name:{"","../secret","x/../secret","C:/secret","/secret","x\\secret","a//b","a/./b","a:b","a/NUL","a/con.txt","a/name. ","a/name."})check(!safe_member(name),"unsafe manifest path accepted");
 const std::vector<unsigned char> header={'g','l','T','F',2,0,0,0,12,0,0,0};check(valid_glb_header(header,12),"valid GLB declined");
 auto invalid=header;invalid[4]=1;check(!valid_glb_header(invalid,12),"old GLB accepted");check(!valid_glb_header(header,13),"truncated GLB accepted");check(!valid_glb_header({},12),"short header accepted");
 const auto libs=steam_libraries(R"vdf("libraryfolders" { "0" { "path" "C:\\Steam" "apps" { "1203620" "1" } } "1" { "path" "D:\\Steam Library" } })vdf");
 check(libs.size()==2&&libs[0]==fs::path(L"C:\\Steam")&&libs[1]==fs::path(L"D:\\Steam Library"),"Steam library decoding failed");
 const auto old=steam_libraries(R"vdf("LibraryFolders" { "1" "E:\\Steam" })vdf");check(old.size()==1&&old[0]==fs::path(L"E:\\Steam"),"legacy Steam library skipped");
 rejects([]{steam_libraries("\"libraryfolders\" { \"path\" \"unterminated");},"malformed VDF accepted");
 Environment inherited{{L"Path",L"unchanged"},{L"ENSHROUDED_VRM_TRACE_COMMANDS",L"1"},{L"enshrouded_vrm_capture_player_anchor",L"1"},{L"VK_INSTANCE_LAYERS",L"VK_LAYER_OTHER"}};const auto before=inherited;
 Selection paths{fs::path(L"C:\\Portable & Mod"),fs::path(L"D:\\Steam\\enshrouded.exe"),fs::path(unicode),true};
 const auto child=launch_environment(inherited,paths,"abcd",L"C:\\Portable & Mod\\State\\Logs\\test.log",L"C:\\Portable & Mod\\State\\Cache");
 check(inherited==before,"parent environment modified");check(!child.count(L"ENSHROUDED_VRM_TRACE_COMMANDS")&&!child.count(L"ENSHROUDED_VRM_CAPTURE_PLAYER_ANCHOR"),"diagnostic options inherited");
 check(child.at(L"ENSHROUDED_VRM_CAPTURE_FRAMES")==L"none"&&child.at(L"ENSHROUDED_VRM_GPU_SKINNING")==L"1"&&child.at(L"ENSHROUDED_VRM_TOOL_SLOTS")==L"1","release defaults wrong");
 check(child.at(L"ENSHROUDED_VRM_MODEL")==unicode&&child.at(L"Path")==L"unchanged","model/path changed");check(child.at(L"VK_INSTANCE_LAYERS").find(L"VK_LAYER_OTHER")!=std::wstring::npos,"existing Vulkan layer lost");
 const auto block=environment_block(child);check(block.size()>2&&block.back()==0&&block[block.size()-2]==0,"environment block lacks double terminator");
 const auto fixture=fs::absolute(fs::u8path(argv[1]));
 const auto root=fixture/fs::u8path("relocated mod & ! 模型");Selection s{root,fixture/"game"/"enshrouded.exe",fixture/fs::u8path("my avatar モデル.vrm"),true};
 put(s.game,"abc");check(sha256(s.game)=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad","SHA256 vector differs");
 put(s.model,std::string(header.begin(),header.end()));
 std::vector<std::string> members={"Runtime/enshrouded_vrm_runtime.dll","Runtime/enshrouded_vrm_layer.json","Runtime/shaders/avatar.vert.spv","Compatibility/profile.json","Compatibility/catalog.json"};
 Json manifest={{"schema","enshrouded-vrm-portable-v1"},{"version","0.1.0-preview"},{"publication_status","local-test-only"},{"game_sha256",sha256(s.game)},{"files",Json::array()}};
 for(const auto&name:members){put(root/fs::u8path(name),"synthetic fixture");manifest["files"].push_back({{"path",name},{"sha256",sha256(root/fs::u8path(name))}});}
 put(root/"release-manifest.json",manifest.dump());
 auto plan=prepare_launch(s,inherited,L"synthetic-session");check(plan.selection.root==root&&plan.command==quote_argument(s.game.wstring()),"relocated launch command wrong");check(!fs::exists(root/"State"),"dry-run verifier created state");
 manifest["publication_status"]="preview";put(root/"release-manifest.json",manifest.dump());
 check(plan.publication_status=="local-test-only","legacy status was not preserved");
 auto preview_plan=prepare_launch(s,inherited,L"preview-session");check(preview_plan.publication_status=="preview","preview status was not preserved");check(preview_plan.selection.root==root,"preview manifest did not preserve package root");
 manifest["publication_status"]="unknown";put(root/"release-manifest.json",manifest.dump());rejects([&]{prepare_launch(s,inherited,L"test");},"unknown publication status accepted");
 manifest["publication_status"]="preview";put(root/"release-manifest.json",manifest.dump());
 const auto layer_list=plan.environment.at(L"VK_LAYER_PATH");const auto layer_folder=layer_list.substr(0,layer_list.find(L';'));
 check(std::all_of(layer_folder.begin(),layer_folder.end(),[](wchar_t c){return c<128;}),"Unicode package did not provide a Vulkan-compatible layer path");
 check(fs::is_directory(fs::path(layer_folder)),"Vulkan layer alias does not name the actual runtime folder");
 check(plan.environment.at(L"ENSHROUDED_VRM_PROFILE")== (root/"Compatibility/profile.json").wstring(),"developer profile path retained");
 put(root/fs::u8path(members[0]),"tampered");rejects([&]{prepare_launch(s,inherited,L"test");},"tampered runtime accepted");put(root/fs::u8path(members[0]),"synthetic fixture");
 put(s.game,"unsupported build");rejects([&]{prepare_launch(s,inherited,L"test");},"unsupported game accepted");put(s.game,"abc");
 auto missing=s;missing.model=fixture/"missing.vrm";rejects([&]{prepare_launch(missing,inherited,L"test");},"missing model accepted");
 auto delimited=s;delimited.root=fixture/"portable;preview";for(const auto&name:members)put(delimited.root/fs::u8path(name),"synthetic fixture");put(delimited.root/"release-manifest.json",manifest.dump());
 rejects([&]{prepare_launch(delimited,inherited,L"test");},"semicolon extraction folder produced an unusable Vulkan search path");
 manifest["files"].push_back({{"path","../outside"},{"sha256",sha256(s.game)}});put(root/"release-manifest.json",manifest.dump());rejects([&]{prepare_launch(s,inherited,L"test");},"traversal manifest accepted");
 std::cout<<"Portable launcher: quoting, Unicode, Steam libraries, child environment, relocation, hashes and invalid inputs passed\n";return 0;
}catch(const std::exception&e){std::cerr<<e.what()<<'\n';return 1;}}
