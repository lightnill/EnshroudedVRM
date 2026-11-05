#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <iostream>
#include <string>
#include <vector>
#ifdef EVRM_HOST_VULKAN
#include <vulkan/vulkan.h>
#endif

struct IUnknown;
using Create = HRESULT(WINAPI*)(HINSTANCE, DWORD, REFIID, void**, IUnknown*);
using CanUnload = HRESULT(WINAPI*)();
using GetClass = HRESULT(WINAPI*)(REFCLSID, REFIID, void**);
using Joystick = const void*(WINAPI*)();
static const char* names[] = {"DirectInput8Create", "DllCanUnloadNow", "DllGetClassObject",
    "DllRegisterServer", "DllUnregisterServer", "GetdfDIJoystick"};

int wmain(int argc, wchar_t** argv) {
    if (argc < 3) return 90;
    const std::wstring mode = argv[2];
    wchar_t directory[32768]{};
    const auto length = GetSystemDirectoryW(directory, 32768);
    if (!length || length >= 32768) return 91;
    HMODULE vulkan = nullptr;
    if (mode == L"preload" || mode == L"vulkan") {
        const auto path = std::wstring(directory) + L"\\vulkan-1.dll";
        vulkan = LoadLibraryExW(path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32);
        if (!vulkan) return 92;
    }
    const std::wstring provider_path = mode == L"owned" && argc == 4 ? argv[3] : std::wstring(directory) + L"\\dinput8.dll";
    HMODULE input_first=nullptr,basename_input=nullptr,import_client=nullptr;
    if(mode==L"input-first") {
        // A dynamic basename request (including a later DLL's static import)
        // reuses the first mapped provider. This is distinct from the game's
        // initial EXE import graph, which discovers the game-root DLL first.
        input_first=LoadLibraryExW(provider_path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!input_first)return 102;
        wchar_t mapped[32768];const auto count=GetModuleFileNameW(input_first,mapped,32768);
        if(!count||count>=32768||CompareStringOrdinal(mapped,count,provider_path.c_str(),-1,TRUE)!=CSTR_EQUAL)return 103;
        basename_input=LoadLibraryW(L"dinput8.dll");
        if(basename_input!=input_first)return 104;
        auto client_path=std::wstring(argv[1]);const auto slash=client_path.find_last_of(L'\\');
        if(slash==std::wstring::npos)return 105;
        client_path.resize(slash+1);client_path+=L"input_import_client.dll";
        import_client=LoadLibraryExW(client_path.c_str(),nullptr,LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR|LOAD_LIBRARY_SEARCH_SYSTEM32);
        if(!import_client)return 106;
        const auto imported=reinterpret_cast<HMODULE(*)()>(GetProcAddress(import_client,"ImportedInputModule"));
        if(!imported||imported()!=input_first)return 107;
        if(GetModuleHandleW(argv[1])||GetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH",nullptr,0))return 108;
    }
    const auto module = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!module) { std::cerr << "input failed " << GetLastError() << '\n'; return 94; }
    const auto provider = LoadLibraryExW(provider_path.c_str(), nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!provider) { std::cerr << "provider failed " << GetLastError() << '\n'; return 93; }
    if(mode==L"input-first") {
        wchar_t mapped[32768];const auto count=GetModuleFileNameW(module,mapped,32768);
        if(module==input_first||!count||count>=32768||CompareStringOrdinal(mapped,count,argv[1],-1,TRUE)!=CSTR_EQUAL)return 109;
    }
    FARPROC actual[6]{}, original[6]{};
    for (unsigned i = 0; i < 6; ++i) {
        actual[i] = GetProcAddress(module, names[i]);
        original[i] = GetProcAddress(provider, names[i]);
        if (!actual[i] || !original[i] || actual[i] != GetProcAddress(module, MAKEINTRESOURCEA(i + 1))) return 95;
    }
    const GUID id{};
    void *expected_output = nullptr, *actual_output = nullptr;
    // Windows performs one-time initialization on its first create call, which
    // changes last error even for this rejected IID. Warm that provider before
    // comparing equivalent calls; the owned case pins first-call behavior too.
    if (mode != L"owned") {
        reinterpret_cast<Create>(original[0])(GetModuleHandleW(nullptr), 0, id, &expected_output, nullptr);
        expected_output = nullptr;
    }
    SetLastError(0xe0000071);
    const auto expected = reinterpret_cast<Create>(original[0])(GetModuleHandleW(nullptr), 0, id, &expected_output, nullptr);
    const auto expected_error = GetLastError();
    SetLastError(0xe0000071);
    const auto observed = reinterpret_cast<Create>(actual[0])(GetModuleHandleW(nullptr), 0, id, &actual_output, nullptr);
    const auto observed_error = GetLastError();
    if (expected != observed || expected_output != actual_output || expected_error != observed_error) {
        std::cerr << "create expected=" << std::hex << expected << " observed=" << observed
                  << " expectedOut=" << expected_output << " observedOut=" << actual_output
                  << " expectedError=" << expected_error << " observedError=" << observed_error << '\n';
        return 96;
    }
    for (const unsigned i : {1u, 2u, 5u}) {
        SetLastError(0xe0000071);
        const auto reference = i == 5 ? reinterpret_cast<Joystick>(original[i])() : nullptr;
        HRESULT reference_result = 0;
        if (i == 1) reference_result = reinterpret_cast<CanUnload>(original[i])();
        if (i == 2) reference_result = reinterpret_cast<GetClass>(original[i])(id, id, &expected_output);
        const auto error = GetLastError();
        SetLastError(0xe0000071);
        const auto result = i == 5 ? reinterpret_cast<Joystick>(actual[i])() : nullptr;
        HRESULT result_code = 0;
        if (i == 1) result_code = reinterpret_cast<CanUnload>(actual[i])();
        if (i == 2) result_code = reinterpret_cast<GetClass>(actual[i])(id, id, &actual_output);
        if (reference != result || reference_result != result_code || error != GetLastError()) return 97;
    }
    // Never invoke real Windows registration exports. Owned providers carry
    // sentinel-only implementations so all six signatures can be exercised.
    if (mode == L"owned") for (const unsigned i : {3u, 4u}) {
        SetLastError(0xe0000071);
        const auto expected_result = reinterpret_cast<CanUnload>(original[i])();
        const auto expected_last_error = GetLastError();
        SetLastError(0xe0000071);
        if (reinterpret_cast<CanUnload>(actual[i])() != expected_result || GetLastError() != expected_last_error) return 98;
    }
    wchar_t layer[32768]{};
    const auto layer_length=GetEnvironmentVariableW(L"VK_ADD_IMPLICIT_LAYER_PATH", layer, 32768);
    if (mode==L"decline" ? layer_length!=0 : layer_length==0) return 99;
#ifdef EVRM_HOST_VULKAN
    if (mode == L"vulkan") {
        auto create = reinterpret_cast<PFN_vkCreateInstance>(GetProcAddress(vulkan, "vkCreateInstance"));
        if (!create) return 100;
        VkApplicationInfo app{}; app.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO; app.apiVersion = VK_API_VERSION_1_1;
        VkInstanceCreateInfo info{}; info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO; info.pApplicationInfo = &app;
        VkInstance instance{};
        const auto result = create(&info, nullptr, &instance);
        if (result != VK_SUCCESS) { std::cerr << "Vulkan create failed " << result << '\n'; return 101; }
        auto proc = reinterpret_cast<PFN_vkGetInstanceProcAddr>(GetProcAddress(vulkan, "vkGetInstanceProcAddr"));
        auto destroy = reinterpret_cast<PFN_vkDestroyInstance>(proc(instance, "vkDestroyInstance"));
        destroy(instance, nullptr);
    }
#endif
    FreeLibrary(module);
    FreeLibrary(provider);
    if(import_client)FreeLibrary(import_client);
    if(basename_input)FreeLibrary(basename_input);
    if(input_first)FreeLibrary(input_first);
    if (vulkan) FreeLibrary(vulkan);
    if(mode==L"input-first")std::cout<<"system-input-first; basename/system-static-client bypass characterized; explicit root activates\n";
    std::cout << "input ABI, last error and layer preparation passed\n";
}
