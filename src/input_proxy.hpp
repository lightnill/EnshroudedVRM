#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

struct IUnknown;

// The exported API names are linker aliases for these project-owned wrappers.
// Their system imports use a separate, absolute-path import library, resolved
// by Windows before any DllMain can call an input export.
extern "C" {
HRESULT WINAPI evrm_DirectInput8Create(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
HRESULT WINAPI evrm_DllCanUnloadNow();
HRESULT WINAPI evrm_DllGetClassObject(REFCLSID, REFIID, void**);
HRESULT WINAPI evrm_DllRegisterServer();
HRESULT WINAPI evrm_DllUnregisterServer();
const void* WINAPI evrm_GetdfDIJoystick();
}

namespace enshrouded::vrm {
// Called once at the verified startup gate, after the provider's DllMain has
// returned. Each absent export retains its own genuine Windows fallback.
enum class InputChainResult { active, self_reference, tls_unavailable };
InputChainResult input_proxy_publish_chain(HMODULE provider, HMODULE own) noexcept;
}
