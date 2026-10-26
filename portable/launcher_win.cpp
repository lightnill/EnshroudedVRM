#include "launcher_core.hpp"
#include "../vendor/tinygltf/json.hpp"
#include <commdlg.h>
#include <shellapi.h>
#include <tlhelp32.h>
#include <algorithm>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>

namespace {
using namespace evrm::portable;
using Json=nlohmann::json;
constexpr wchar_t title[]=L"Enshrouded VRM — lightnill";
struct Options {Selection selection;bool check=false,launch=false,wait=false,version=false;fs::path report;};
Environment inherited_environment(){Environment env;const auto block=GetEnvironmentStringsW();if(!block)throw std::runtime_error("Windows could not read the launch environment.");for(auto p=block;*p;p+=std::wcslen(p)+1){std::wstring line=p;const auto split=line.find(L'=',line.front()==L'='?1:0);if(split!=std::wstring::npos)env[line.substr(0,split)]=line.substr(split+1);}FreeEnvironmentStringsW(block);return env;}
fs::path executable_folder(){std::vector<wchar_t> path(32768);const auto n=GetModuleFileNameW(nullptr,path.data(),DWORD(path.size()));if(!n||n>=path.size())throw std::runtime_error("The launcher folder could not be located.");return fs::path(std::wstring(path.data(),n)).parent_path();}
std::wstring variable(const wchar_t*name){const auto n=GetEnvironmentVariableW(name,nullptr,0);if(!n||n>32768)return {};std::wstring value(n,L'\0');const auto got=GetEnvironmentVariableW(name,value.data(),n);if(!got||got>=n)return {};value.resize(got);return value;}
fs::path detect_game(){
 std::vector<fs::path> roots;wchar_t steam[32768]{};DWORD bytes=sizeof steam;if(RegGetValueW(HKEY_CURRENT_USER,L"Software\\Valve\\Steam",L"SteamPath",RRF_RT_REG_SZ,nullptr,steam,&bytes)==ERROR_SUCCESS&&steam[0])roots.emplace_back(steam);
 for(const auto*name:{L"ProgramFiles(x86)",L"ProgramFiles"}){const auto base=variable(name);if(!base.empty())roots.emplace_back(fs::path(base)/L"Steam");}
 const auto primary=roots;for(const auto&root:primary){try{const auto file=root/L"steamapps/libraryfolders.vdf";if(!fs::is_regular_file(file)||fs::file_size(file)>2u*1024u*1024u)continue;std::ifstream in(file,std::ios::binary);const std::string vdf{std::istreambuf_iterator<char>(in),std::istreambuf_iterator<char>()};const auto libraries=steam_libraries(vdf);roots.insert(roots.end(),libraries.begin(),libraries.end());}catch(...){/* Browsing remains available if a library list is malformed. */}}
 for(const auto&root:roots){const auto game=root/L"steamapps/common/Enshrouded/enshrouded.exe";std::error_code error;if(fs::is_regular_file(game,error))return game;}return {};
}
void write_json(const fs::path&p,const Json&value){std::ofstream out(p,std::ios::binary|std::ios::trunc);out<<value.dump(2)<<'\n';if(!out)throw std::runtime_error("The portable folder could not save local settings or launch details. Extract it into a writable folder.");}
Selection defaults(const fs::path&root){Selection s;s.root=root;try{const auto file=root/L"State/settings.json";if(fs::is_regular_file(file)&&fs::file_size(file)<32768){std::ifstream in(file,std::ios::binary);const auto settings=Json::parse(in);s.game=fs::u8path(settings.value("game",std::string{}));s.model=fs::u8path(settings.value("model",std::string{}));s.springs=settings.value("springs",true);}}catch(...){/* Invalid old settings are replaced only after an explicit launch. */}if(s.game.empty())s.game=detect_game();if(s.model.empty()){std::error_code error;std::vector<fs::path> models;for(fs::directory_iterator it(root/L"Avatars",error),end;!error&&it!=end;it.increment(error)){if(it->is_regular_file(error)&&_wcsicmp(it->path().extension().c_str(),L".vrm")==0)models.push_back(it->path());if(models.size()>1)break;}if(models.size()==1)s.model=models.front();}return s;}
Options arguments(int argc,wchar_t**argv){Options o;o.selection=defaults(executable_folder());for(int i=1;i<argc;++i){const std::wstring arg=argv[i];if(arg==L"--version")o.version=true;else if(arg==L"--check")o.check=true;else if(arg==L"--launch")o.launch=true;else if(arg==L"--wait")o.wait=true;else if(arg==L"--no-springs")o.selection.springs=false;else if(arg==L"--game"||arg==L"--model"||arg==L"--report"){if(++i>=argc)throw std::runtime_error("A launcher option is missing its file path.");if(arg==L"--game")o.selection.game=argv[i];else if(arg==L"--model")o.selection.model=argv[i];else o.report=argv[i];}else throw std::runtime_error("Unknown launcher option.");}if(o.check&&o.launch)throw std::runtime_error("Choose either --check or --launch.");if(o.wait&&!o.launch)throw std::runtime_error("--wait requires --launch.");return o;}
std::wstring session_name(){SYSTEMTIME now{};GetLocalTime(&now);wchar_t value[96]{};swprintf(value,96,L"%04u%02u%02u-%02u%02u%02u-%lu-%llu",now.wYear,now.wMonth,now.wDay,now.wHour,now.wMinute,now.wSecond,GetCurrentProcessId(),GetTickCount64());return value;}
bool game_running(){const auto snapshot=CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS,0);if(snapshot==INVALID_HANDLE_VALUE)throw std::runtime_error("Windows could not check whether Enshrouded is already running.");PROCESSENTRY32W p{};p.dwSize=sizeof p;bool found=false;for(BOOL ok=Process32FirstW(snapshot,&p);ok;ok=Process32NextW(snapshot,&p))if(_wcsicmp(p.szExeFile,L"enshrouded.exe")==0){found=true;break;}CloseHandle(snapshot);return found;}
struct Child {PROCESS_INFORMATION info{};~Child(){if(info.hThread)CloseHandle(info.hThread);if(info.hProcess)CloseHandle(info.hProcess);}};
struct LaunchState {bool started=false;DWORD pid=0;};
bool requested_report(const fs::path&path,Json&result){if(path.empty())return true;try{write_json(path,result);return true;}catch(const std::exception&e){result["reportWarning"]=e.what();return false;}}
Json start(const LaunchPlan&plan,bool wait,LaunchState&state){
 if(game_running())throw std::runtime_error("Close Enshrouded before launching it through this mod.");
 fs::create_directories(plan.log.parent_path());fs::create_directories(plan.cache);
 const auto settings=plan.selection.root/L"State/settings.json";write_json(settings,{{"game",plan.selection.game.u8string()},{"model",plan.selection.model.u8string()},{"springs",plan.selection.springs}});
 auto block=environment_block(plan.environment);auto command=plan.command;STARTUPINFOW startup{};startup.cb=sizeof startup;Child child;
 if(!CreateProcessW(plan.selection.game.c_str(),command.data(),nullptr,nullptr,FALSE,CREATE_UNICODE_ENVIRONMENT,block.data(),plan.selection.game.parent_path().c_str(),&startup,&child.info))throw std::runtime_error("Windows could not start Enshrouded (error "+std::to_string(GetLastError())+"). No administrator rights are required.");
 state.started=true;state.pid=child.info.dwProcessId;
 Json result={{"gameStarted",true},{"processId",state.pid},{"executable",plan.selection.game.u8string()},{"model",plan.selection.model.u8string()},{"logPath",plan.log.u8string()},{"runtimeSha256",plan.runtime_hash},{"captureFiles",false},{"gpuSkinning",true},{"publicationStatus",plan.publication_status}};
 FILETIME created{},exit{},kernel{},user{};
 if(GetProcessTimes(child.info.hProcess,&created,&exit,&kernel,&user))result["startFileTimeUtc"]=(std::uint64_t(created.dwHighDateTime)<<32)|created.dwLowDateTime;
 else result["identityWarning"]="Game started; Windows could not read its creation time (error "+std::to_string(GetLastError())+").";
 try{write_json(plan.selection.root/L"State/last-session.json",result);}catch(const std::exception&e){result["settingsWarning"]=e.what();}
 if(wait){std::cout<<result.dump()<<std::endl;const auto waited=WaitForSingleObject(child.info.hProcess,INFINITE);DWORD code=0;if(waited==WAIT_OBJECT_0&&GetExitCodeProcess(child.info.hProcess,&code))result["exitCode"]=code;else result["waitWarning"]="Game started; its exit status could not be collected.";}
 return result;
}

constexpr int game_edit=101,game_browse=102,model_edit=103,model_browse=104,spring_check=105,launch_button=106,logs_button=107;
Selection current;HFONT font=nullptr;HWND game_field=nullptr,model_field=nullptr,springs=nullptr;int dpi=96;
int px(int n){return MulDiv(n,dpi,96);}
std::wstring field(HWND h){const int n=GetWindowTextLengthW(h);std::wstring text(n+1,L'\0');GetWindowTextW(h,text.data(),n+1);text.resize(n);return text;}
void control(HWND parent,const wchar_t*type,const wchar_t*label,DWORD style,int id,int x,int y,int w,int h){const auto child=CreateWindowExW(std::wcscmp(type,L"EDIT")==0?WS_EX_CLIENTEDGE:0,type,label,WS_CHILD|WS_VISIBLE|style,px(x),px(y),px(w),px(h),parent,reinterpret_cast<HMENU>(INT_PTR(id)),GetModuleHandleW(nullptr),nullptr);SendMessageW(child,WM_SETFONT,reinterpret_cast<WPARAM>(font),TRUE);if(id==game_edit)game_field=child;if(id==model_edit)model_field=child;if(id==spring_check)springs=child;}
void browse(HWND parent,bool game){wchar_t path[32768]{};const auto prior=field(game?game_field:model_field);if(prior.size()<32768)std::copy(prior.begin(),prior.end(),path);const wchar_t*filter=game?L"Enshrouded (enshrouded.exe)\0enshrouded.exe\0\0":L"VRM avatars (*.vrm)\0*.vrm\0\0";const auto initial=current.root/L"Avatars";OPENFILENAMEW dialog{};dialog.lStructSize=sizeof dialog;dialog.hwndOwner=parent;dialog.lpstrFilter=filter;dialog.lpstrFile=path;dialog.nMaxFile=32768;dialog.lpstrInitialDir=game?nullptr:initial.c_str();dialog.lpstrTitle=game?L"Choose enshrouded.exe":L"Choose your VRM avatar";dialog.Flags=OFN_FILEMUSTEXIST|OFN_PATHMUSTEXIST|OFN_NOCHANGEDIR|OFN_EXPLORER;if(GetOpenFileNameW(&dialog))SetWindowTextW(game?game_field:model_field,path);}
LRESULT CALLBACK window(HWND h,UINT message,WPARAM w,LPARAM l){
 if(message==WM_CREATE){const auto dc=GetDC(h);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(h,dc);font=CreateFontW(-MulDiv(10,dpi,72),0,0,0,FW_NORMAL,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_DEFAULT_PRECIS,CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY,DEFAULT_PITCH,L"Segoe UI");
  control(h,L"STATIC",L"Enshrouded VRM  0.1.0 preview",0,0,24,20,610,28);control(h,L"STATIC",L"by lightnill · Local appearance · Use your own VRM",0,0,24,51,610,24);
  control(h,L"STATIC",L"Game",0,0,24,91,580,22);control(h,L"EDIT",current.game.c_str(),WS_TABSTOP|ES_AUTOHSCROLL,game_edit,24,116,488,28);control(h,L"BUTTON",L"Browse…",WS_TABSTOP,game_browse,524,115,96,30);
  control(h,L"STATIC",L"Avatar",0,0,24,158,580,22);control(h,L"EDIT",current.model.c_str(),WS_TABSTOP|ES_AUTOHSCROLL,model_edit,24,183,488,28);control(h,L"BUTTON",L"Browse…",WS_TABSTOP,model_browse,524,182,96,30);
  control(h,L"BUTTON",L"Hair and clothing motion",WS_TABSTOP|BS_AUTOCHECKBOX,spring_check,24,229,360,26);SendMessageW(springs,BM_SETCHECK,current.springs?BST_CHECKED:BST_UNCHECKED,0);
  control(h,L"STATIC",L"Only you see the avatar. Tested in Offline mode.\nExtract into a writable folder; keep the files together.",0,0,24,270,590,46);
  control(h,L"BUTTON",L"Open logs",WS_TABSTOP,logs_button,24,336,116,34);control(h,L"BUTTON",L"Launch game",WS_TABSTOP|BS_DEFPUSHBUTTON,launch_button,450,336,170,34);return 0;
 }
 if(message==WM_COMMAND){LaunchState state;try{switch(LOWORD(w)){case game_browse:browse(h,true);return 0;case model_browse:browse(h,false);return 0;case logs_button:{const auto folder=current.root/L"State/Logs";fs::create_directories(folder);ShellExecuteW(h,L"open",folder.c_str(),nullptr,nullptr,SW_SHOWNORMAL);return 0;}case launch_button:{EnableWindow(GetDlgItem(h,launch_button),FALSE);current.game=field(game_field);current.model=field(model_field);current.springs=SendMessageW(springs,BM_GETCHECK,0,0)==BST_CHECKED;const auto plan=prepare_launch(current,inherited_environment(),session_name());start(plan,false,state);DestroyWindow(h);return 0;}}}catch(const std::exception&e){if(state.started){MessageBoxW(h,(L"Game started (process "+std::to_wstring(state.pid)+L"). The launcher could not finish its local report: "+wide(e.what())).c_str(),title,MB_OK|MB_ICONINFORMATION);DestroyWindow(h);}else{EnableWindow(GetDlgItem(h,launch_button),TRUE);MessageBoxW(h,wide(e.what()).c_str(),title,MB_OK|MB_ICONINFORMATION);}}return 0;}
 if(message==WM_DESTROY){if(font){DeleteObject(font);font=nullptr;}PostQuitMessage(0);return 0;}return DefWindowProcW(h,message,w,l);
}
int run(){int argc=0;auto argv=CommandLineToArgvW(GetCommandLineW(),&argc);if(!argv)throw std::runtime_error("Windows could not read the launcher options.");Options opts;try{opts=arguments(argc,argv);}catch(...){LocalFree(argv);throw;}LocalFree(argv);
 if(opts.version){std::cout<<"Enshrouded VRM 0.1.0-preview by lightnill\n";return 0;}
 if(opts.check||opts.launch){LaunchState state;try{const auto plan=prepare_launch(opts.selection,inherited_environment(),session_name());Json result;if(opts.launch)result=start(plan,opts.wait,state);else result={{"ok",true},{"version","0.1.0-preview"},{"publicationStatus",plan.publication_status},{"runtimeSha256",plan.runtime_hash},{"captureFiles",false},{"gameStarted",false}};const bool report_ok=requested_report(opts.report,result);std::cout<<result.dump()<<std::endl;return !state.started&&!report_ok?1:result.value("exitCode",0);}catch(const std::exception&e){Json result={{"ok",false},{"error",e.what()},{"gameStarted",state.started}};if(state.started)result["processId"]=state.pid;requested_report(opts.report,result);std::cerr<<result.dump()<<std::endl;return state.started?0:1;}}
 current=opts.selection;WNDCLASSW c{};c.lpfnWndProc=window;c.hInstance=GetModuleHandleW(nullptr);c.lpszClassName=L"LightnillEnshroudedVRMPortable";c.hCursor=LoadCursorW(nullptr,IDC_ARROW);c.hbrBackground=reinterpret_cast<HBRUSH>(COLOR_WINDOW+1);RegisterClassW(&c);
 const auto dc=GetDC(nullptr);dpi=GetDeviceCaps(dc,LOGPIXELSX);ReleaseDC(nullptr,dc);RECT rect{0,0,px(644),px(396)};AdjustWindowRectEx(&rect,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,FALSE,0);const auto h=CreateWindowExW(0,c.lpszClassName,title,WS_OVERLAPPED|WS_CAPTION|WS_SYSMENU|WS_MINIMIZEBOX,CW_USEDEFAULT,CW_USEDEFAULT,rect.right-rect.left,rect.bottom-rect.top,nullptr,nullptr,c.hInstance,nullptr);if(!h)throw std::runtime_error("Windows could not open the launcher.");ShowWindow(h,SW_SHOWNORMAL);MSG message{};while(GetMessageW(&message,nullptr,0,0)>0){if(!IsDialogMessageW(h,&message)){TranslateMessage(&message);DispatchMessageW(&message);}}return 0;
}
}
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){try{return run();}catch(const std::exception&e){MessageBoxW(nullptr,evrm::portable::wide(e.what()).c_str(),title,MB_OK|MB_ICONINFORMATION);return 1;}}
