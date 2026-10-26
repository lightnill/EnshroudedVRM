#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cwchar>
// Synthetic no-window child, used only in native launcher regression tests.
// No graphics layer is loaded, and no actual game is required.
int WINAPI wWinMain(HINSTANCE,HINSTANCE,PWSTR,int){
 wchar_t path[32768]{},value[128]{};const auto n=GetEnvironmentVariableW(L"PORTABLE_TEST_MARKER",path,32768);if(!n||n>=32768)return 7;
 if(GetEnvironmentVariableW(L"ENSHROUDED_VRM_CAPTURE_FRAMES",value,128)!=4||std::wcscmp(value,L"none")!=0)return 8;
 if(GetEnvironmentVariableW(L"ENSHROUDED_VRM_TRACE_COMMANDS",value,128)||GetEnvironmentVariableW(L"ENSHROUDED_VRM_CAPTURE_PLAYER_ANCHOR",value,128))return 9;
 const auto file=CreateFileW(path,FILE_APPEND_DATA,FILE_SHARE_READ|FILE_SHARE_WRITE,nullptr,OPEN_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);if(file==INVALID_HANDLE_VALUE)return 10;const DWORD pid=GetCurrentProcessId();DWORD written=0;const bool ok=WriteFile(file,&pid,sizeof pid,&written,nullptr)&&written==sizeof pid;CloseHandle(file);return ok?0:11;
}
