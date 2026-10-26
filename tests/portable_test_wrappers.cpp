#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <cstring>
// Test executable only: inject one API failure and suppress modal test dialogs.
// Neither this object nor the synthetic child enters the binary package.
namespace {
int WINAPI message(HWND,LPCWSTR,LPCWSTR,UINT){return IDOK;}
BOOL WINAPI times(HANDLE p,LPFILETIME a,LPFILETIME b,LPFILETIME c,LPFILETIME d){
 wchar_t flag[8]{};if(GetEnvironmentVariableW(L"PORTABLE_TEST_FAIL_TIMES",flag,8)>0){SetLastError(ERROR_ACCESS_DENIED);return FALSE;}
 const auto address=GetProcAddress(GetModuleHandleW(L"kernel32.dll"),"GetProcessTimes");decltype(&GetProcessTimes) real=nullptr;static_assert(sizeof real==sizeof address);std::memcpy(&real,&address,sizeof real);return real?real(p,a,b,c,d):FALSE;
}
}
extern "C" {
decltype(&MessageBoxW) __wrap___imp_MessageBoxW=message;
decltype(&GetProcessTimes) __wrap___imp_GetProcessTimes=times;
}
