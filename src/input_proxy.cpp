#include "input_proxy.hpp"
#include <cstring>
#include <utility>

extern "C" {
__declspec(dllimport) HRESULT WINAPI DirectInput8Create(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
__declspec(dllimport) HRESULT WINAPI DllCanUnloadNow();
__declspec(dllimport) HRESULT WINAPI DllGetClassObject(REFCLSID, REFIID, void**);
__declspec(dllimport) HRESULT WINAPI DllRegisterServer();
__declspec(dllimport) HRESULT WINAPI DllUnregisterServer();
__declspec(dllimport) const void* WINAPI GetdfDIJoystick();
}

namespace {
struct InputTable {
    decltype(&DirectInput8Create) create;
    decltype(&DllCanUnloadNow) unload;
    decltype(&DllGetClassObject) object;
    decltype(&DllRegisterServer) register_server;
    decltype(&DllUnregisterServer) unregister_server;
    decltype(&GetdfDIJoystick) joystick;
};
InputTable chain_table{};
PVOID volatile published_table=nullptr;
DWORD routing_slot=TLS_OUT_OF_INDEXES;

template<class Function> Function resolve(HMODULE provider,const char* name,HMODULE own,bool& self) noexcept {
    const auto address=GetProcAddress(provider,name);
    if(!address)return nullptr;
    MEMORY_BASIC_INFORMATION memory{};
    const void* raw=nullptr;
    static_assert(sizeof(raw)==sizeof(address));
    std::memcpy(&raw,&address,sizeof(raw));
    if(!VirtualQuery(raw,&memory,sizeof(memory)) || memory.AllocationBase==own) { self=true;return nullptr; }
    Function function=nullptr;
    static_assert(sizeof(function)==sizeof(address));
    std::memcpy(&function,&address,sizeof(function));return function;
}
InputTable* published() noexcept {
    return static_cast<InputTable*>(InterlockedCompareExchangePointer(&published_table,nullptr,nullptr));
}
template<class Function,class... Args>
auto invoke(Function target,Function system,Args&&... args) -> decltype(system(std::forward<Args>(args)...)) {
    if(!target)return system(std::forward<Args>(args)...);
    const auto error=GetLastError();
    const auto nested=TlsGetValue(routing_slot);
    if(nested || !TlsSetValue(routing_slot,reinterpret_cast<void*>(1))) {
        SetLastError(error);return system(std::forward<Args>(args)...);
    }
    SetLastError(error);
    const auto result=target(std::forward<Args>(args)...);
    const auto target_error=GetLastError();
    TlsSetValue(routing_slot,nullptr);
    SetLastError(target_error);return result;
}
}

namespace enshrouded::vrm {
InputChainResult input_proxy_publish_chain(HMODULE provider,HMODULE own) noexcept {
    bool self=false;
    chain_table.create=resolve<decltype(chain_table.create)>(provider,"DirectInput8Create",own,self);
    chain_table.unload=resolve<decltype(chain_table.unload)>(provider,"DllCanUnloadNow",own,self);
    chain_table.object=resolve<decltype(chain_table.object)>(provider,"DllGetClassObject",own,self);
    chain_table.register_server=resolve<decltype(chain_table.register_server)>(provider,"DllRegisterServer",own,self);
    chain_table.unregister_server=resolve<decltype(chain_table.unregister_server)>(provider,"DllUnregisterServer",own,self);
    chain_table.joystick=resolve<decltype(chain_table.joystick)>(provider,"GetdfDIJoystick",own,self);
    if(self)return InputChainResult::self_reference;
    routing_slot=TlsAlloc();
    if(routing_slot==TLS_OUT_OF_INDEXES)return InputChainResult::tls_unavailable;
    // One Windows TLS slot guards downstream reentry. It is allocated here,
    // outside loader lock, and retained with the provider until process exit.
    InterlockedExchangePointer(&published_table,&chain_table);
    return InputChainResult::active;
}
}

extern "C" {
HRESULT WINAPI evrm_DirectInput8Create(HINSTANCE instance, DWORD version, REFIID iid, void** output, IUnknown* outer) {
    const auto table=published();
    return invoke(table?table->create:nullptr,&DirectInput8Create,instance,version,iid,output,outer);
}
HRESULT WINAPI evrm_DllCanUnloadNow() {const auto t=published();return invoke(t?t->unload:nullptr,&DllCanUnloadNow);}
HRESULT WINAPI evrm_DllGetClassObject(REFCLSID clsid, REFIID iid, void** output) {
    const auto t=published();return invoke(t?t->object:nullptr,&DllGetClassObject,clsid,iid,output);
}
HRESULT WINAPI evrm_DllRegisterServer() {const auto t=published();return invoke(t?t->register_server:nullptr,&DllRegisterServer);}
HRESULT WINAPI evrm_DllUnregisterServer() {const auto t=published();return invoke(t?t->unregister_server:nullptr,&DllUnregisterServer);}
const void* WINAPI evrm_GetdfDIJoystick() {const auto t=published();return invoke(t?t->joystick:nullptr,&GetdfDIJoystick);}
}
