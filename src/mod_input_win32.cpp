#include "mod_input_win32.hpp"
#include "mod_settings_service.hpp"
#include "runtime_log.hpp"
#include <atomic>
#include <array>
#include <cstring>
#include <memory>
#include <mutex>
#include <vector>
#include <algorithm>
#include <stdexcept>
namespace evrm {
namespace {
constexpr wchar_t property_name[]=L"EnshroudedVRM.ModInput.1";
using Raw=decltype(&GetRawInputData);using Clip=decltype(&ClipCursor);using Warp=decltype(&SetCursorPos);
using Cursor=decltype(&SetCursor);using Def=decltype(&DefWindowProcW);
using Capture=decltype(&SetCapture);using Release=decltype(&ReleaseCapture);
struct Functions {Raw raw=nullptr;Clip clip=nullptr;Warp warp=nullptr;Cursor cursor=nullptr;Def def=nullptr;Capture capture=nullptr;Release release=nullptr;};
Functions originals;
template<class Function>Function procedure_address(HMODULE module,const char* name)noexcept {
    const auto address=GetProcAddress(module,name);Function result=nullptr;
    static_assert(sizeof result==sizeof address);std::memcpy(&result,&address,sizeof result);return result;
}
thread_local const RAWINPUT* owned_packet=nullptr;
struct PacketScope {const RAWINPUT* before;explicit PacketScope(const RAWINPUT& p):before(owned_packet){owned_packet=&p;}~PacketScope(){owned_packet=before;}};
constexpr unsigned raw_key_count=1280;
unsigned virtual_key(unsigned key)noexcept {
    if(key==VK_LSHIFT||key==VK_RSHIFT)return VK_SHIFT;
    if(key==VK_LCONTROL||key==VK_RCONTROL)return VK_CONTROL;
    if(key==VK_LMENU||key==VK_RMENU)return VK_MENU;
    return key;
}
unsigned raw_identity(const RAWKEYBOARD& key)noexcept {
    if(key.VKey>=256||key.MakeCode>255)return raw_key_count;
    return key.MakeCode?(unsigned(key.MakeCode)|((key.Flags&(RI_KEY_E0|RI_KEY_E1))<<7)):(1024u+key.VKey);
}
struct Window {
    HWND handle=nullptr;WNDPROC previous=nullptr;
    std::recursive_mutex mutex;ModInputState input;
    std::array<RAWKEYBOARD,raw_key_count> raw_keys{};
    std::bitset<raw_key_count> raw_down,raw_forwarded,raw_latched;
    std::array<unsigned short,256> raw_counts{};
    std::atomic<bool> keyboard_supported{true};
    RAWINPUTHEADER last_header{};
    std::atomic<bool> capturing{false},allowed{true};
    std::atomic<std::uint64_t> admission{0};
    RECT game_clip{};bool clip_null=false,cursor_owned=false;POINT saved_position{};
    HCURSOR game_cursor=nullptr;unsigned show_calls=0;
    HWND game_capture=nullptr;
};
std::atomic<Window*> current{nullptr};
std::atomic<std::uint64_t> next_admission{1};
std::mutex attachment_mutex;
UINT close_message=0;
#ifdef EVRM_MOD_INPUT_TESTING
bool test_focus=false,test_destroy_attach=false;std::uint32_t test_time=1;ModInputTestCounters counters;
HWND test_capture=nullptr;
unsigned test_cursor_failure=0;
#endif
bool focused(HWND window)noexcept {
#ifdef EVRM_MOD_INPUT_TESTING
    return test_focus&&window!=nullptr;
#else
    return GetForegroundWindow()==window;
#endif
}
std::uint32_t message_time()noexcept {
#ifdef EVRM_MOD_INPUT_TESTING
    return ++test_time;
#else
    return std::uint32_t(GetMessageTime());
#endif
}
BOOL real_clip(const RECT* rectangle)noexcept {
#ifdef EVRM_MOD_INPUT_TESTING
    (void)rectangle;++counters.clips;return TRUE;
#else
    return originals.clip(rectangle);
#endif
}
BOOL real_warp(int x,int y)noexcept {
#ifdef EVRM_MOD_INPUT_TESTING
    (void)x;(void)y;++counters.cursor_moves;return TRUE;
#else
    return originals.warp(x,y);
#endif
}
HCURSOR real_cursor(HCURSOR cursor)noexcept {
#ifdef EVRM_MOD_INPUT_TESTING
    (void)cursor;++counters.cursor_sets;return nullptr;
#else
    return originals.cursor(cursor);
#endif
}
int show_cursor(bool visible)noexcept {
#ifdef EVRM_MOD_INPUT_TESTING
    counters.show_balance+=visible?1:-1;return counters.show_balance;
#else
    return ShowCursor(visible);
#endif
}
HWND capture_window()noexcept {
#ifdef EVRM_MOD_INPUT_TESTING
    return test_capture;
#else
    return GetCapture();
#endif
}
HWND real_capture(HWND window)noexcept {
#ifdef EVRM_MOD_INPUT_TESTING
    const auto before=test_capture;test_capture=window;return before;
#else
    return originals.capture(window);
#endif
}
BOOL real_release()noexcept {
#ifdef EVRM_MOD_INPUT_TESTING
    test_capture=nullptr;return TRUE;
#else
    return originals.release();
#endif
}
LRESULT real_default(HWND window,UINT message,WPARAM w,LPARAM l)noexcept {
#ifdef EVRM_MOD_INPUT_TESTING
    if(message==WM_INPUT&&owned_packet&&l==reinterpret_cast<LPARAM>(owned_packet)){++counters.cleanups;return 0;}
#endif
    return originals.def(window,message,w,l);
}
UINT owned_read(HRAWINPUT handle,UINT command,LPVOID data,PUINT size,UINT header)noexcept {
    if(!owned_packet||handle!=reinterpret_cast<HRAWINPUT>(const_cast<RAWINPUT*>(owned_packet)))return originals.raw(handle,command,data,size,header);
    if(!size||header!=sizeof(RAWINPUTHEADER)||(command!=RID_INPUT&&command!=RID_HEADER)){
        SetLastError(ERROR_INVALID_PARAMETER);return UINT(-1);}
    const auto count=command==RID_HEADER?UINT(sizeof(RAWINPUTHEADER)):owned_packet->header.dwSize;
    if(count>sizeof(RAWINPUT)||count<sizeof(RAWINPUTHEADER)){SetLastError(ERROR_INVALID_DATA);return UINT(-1);}
    if(!data){*size=count;return 0;}
    if(*size<count){*size=count;SetLastError(ERROR_INSUFFICIENT_BUFFER);return UINT(-1);}
    std::memcpy(data,owned_packet,count);*size=count;return count;
}
UINT WINAPI game_raw(HRAWINPUT handle,UINT command,LPVOID data,PUINT size,UINT header)noexcept{return owned_read(handle,command,data,size,header);}
LRESULT WINAPI game_default(HWND window,UINT message,WPARAM w,LPARAM l)noexcept {
    if(message==WM_INPUT&&owned_packet&&l==reinterpret_cast<LPARAM>(owned_packet))return 0;
    return real_default(window,message,w,l);
}
Window* cursor_owner()noexcept {
    auto* state=current.load(std::memory_order_acquire);
    return state&&state->capturing.load(std::memory_order_acquire)&&focused(state->handle)?state:nullptr;
}
BOOL WINAPI game_clip(const RECT* rectangle)noexcept {
    if(auto* state=cursor_owner()){std::lock_guard<std::recursive_mutex> guard(state->mutex);
        state->clip_null=rectangle==nullptr;if(rectangle)state->game_clip=*rectangle;return TRUE;}
    return real_clip(rectangle);
}
BOOL WINAPI game_warp(int x,int y)noexcept {
    if(cursor_owner())return TRUE;
    return real_warp(x,y);
}
HCURSOR WINAPI game_cursor(HCURSOR cursor)noexcept {
    if(auto* state=cursor_owner()){std::lock_guard<std::recursive_mutex> guard(state->mutex);
        const auto previous=state->game_cursor;state->game_cursor=cursor;return previous;}
    return real_cursor(cursor);
}
HWND WINAPI game_capture(HWND window)noexcept {
    if(auto* state=cursor_owner())if(window==state->handle){std::lock_guard<std::recursive_mutex> guard(state->mutex);const auto before=state->game_capture;state->game_capture=window;return before;}
    return real_capture(window);
}
BOOL WINAPI game_release()noexcept {
    if(auto* state=cursor_owner()){std::lock_guard<std::recursive_mutex> guard(state->mutex);state->game_capture=nullptr;return TRUE;}
    return real_release();
}
bool image_copy(HMODULE image,std::size_t rva,void* data,std::size_t size,std::size_t limit)noexcept {
    if(rva>limit||size>limit-rva)return false;
    auto at=reinterpret_cast<std::uintptr_t>(image)+rva;const auto end=at+size;
    if(at<reinterpret_cast<std::uintptr_t>(image)||end<at)return false;
    while(at<end){MEMORY_BASIC_INFORMATION memory{};
        if(!VirtualQuery(reinterpret_cast<void*>(at),&memory,sizeof memory)||memory.AllocationBase!=image||memory.Type!=MEM_IMAGE||
            memory.State!=MEM_COMMIT||(memory.Protect&(PAGE_GUARD|PAGE_NOACCESS)))return false;
        const auto next=reinterpret_cast<std::uintptr_t>(memory.BaseAddress)+memory.RegionSize;if(next<=at)return false;at=std::min(end,next);}
    SIZE_T copied=0;return ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<std::uint8_t*>(image)+rva,data,size,&copied)&&copied==size;
}
struct Import {void** slot=nullptr;void* expected=nullptr;void* replacement=nullptr;};
bool initialize_imports(){
    const auto user=GetModuleHandleW(L"user32.dll");const auto image=GetModuleHandleW(nullptr);if(!user||!image)return false;
    originals={procedure_address<Raw>(user,"GetRawInputData"),procedure_address<Clip>(user,"ClipCursor"),
        procedure_address<Warp>(user,"SetCursorPos"),procedure_address<Cursor>(user,"SetCursor"),procedure_address<Def>(user,"DefWindowProcW"),procedure_address<Capture>(user,"SetCapture"),procedure_address<Release>(user,"ReleaseCapture")};
    if(!originals.raw||!originals.clip||!originals.warp||!originals.cursor||!originals.def||!originals.capture||!originals.release)return false;
    IMAGE_DOS_HEADER dos{};IMAGE_NT_HEADERS64 nt{};
    if(!image_copy(image,0,&dos,sizeof dos,4096)||dos.e_magic!=IMAGE_DOS_SIGNATURE||dos.e_lfanew<0||dos.e_lfanew>1024*1024||
       !image_copy(image,dos.e_lfanew,&nt,sizeof nt,2*1024*1024)||nt.Signature!=IMAGE_NT_SIGNATURE||
       nt.FileHeader.Machine!=IMAGE_FILE_MACHINE_AMD64||nt.OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR64_MAGIC||
       nt.OptionalHeader.SizeOfImage<4096||nt.OptionalHeader.SizeOfImage>128*1024*1024)return false;
#ifndef EVRM_MOD_INPUT_TESTING
    if(nt.FileHeader.TimeDateStamp!=1792507660||nt.OptionalHeader.SizeOfImage!=44163072)return false;
#endif
    const auto limit=nt.OptionalHeader.SizeOfImage;const auto directory=nt.OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!directory.VirtualAddress||directory.Size<sizeof(IMAGE_IMPORT_DESCRIPTOR)||directory.Size>128*1024||
       directory.VirtualAddress>limit||directory.Size>limit-directory.VirtualAddress)return false;
    const std::array<const char*,7> names={{"GetRawInputData","ClipCursor","SetCursorPos","SetCursor","DefWindowProcW","SetCapture","ReleaseCapture"}};
    const std::array<void*,7> expected={{reinterpret_cast<void*>(originals.raw),reinterpret_cast<void*>(originals.clip),reinterpret_cast<void*>(originals.warp),reinterpret_cast<void*>(originals.cursor),reinterpret_cast<void*>(originals.def),reinterpret_cast<void*>(originals.capture),reinterpret_cast<void*>(originals.release)}};
    const std::array<void*,7> replacements={{reinterpret_cast<void*>(&game_raw),reinterpret_cast<void*>(&game_clip),reinterpret_cast<void*>(&game_warp),reinterpret_cast<void*>(&game_cursor),reinterpret_cast<void*>(&game_default),reinterpret_cast<void*>(&game_capture),reinterpret_cast<void*>(&game_release)}};
    std::bitset<7> found;std::vector<Import> pending;
    for(unsigned d=0;d<128&&std::size_t(d+1)*sizeof(IMAGE_IMPORT_DESCRIPTOR)<=directory.Size;++d){
        IMAGE_IMPORT_DESCRIPTOR descriptor{};if(!image_copy(image,directory.VirtualAddress+d*sizeof descriptor,&descriptor,sizeof descriptor,limit))return false;
        if(!descriptor.Name)break;
        std::array<char,64> library{};if(!image_copy(image,descriptor.Name,library.data(),library.size(),limit)||!std::memchr(library.data(),0,library.size()))return false;
        if(_stricmp(library.data(),"USER32.dll"))continue;
        if(!descriptor.OriginalFirstThunk||!descriptor.FirstThunk)return false;
        for(unsigned n=0;n<2048;++n){IMAGE_THUNK_DATA64 name{},value{};
            const auto source=std::uint64_t(descriptor.OriginalFirstThunk)+n*sizeof name,target=std::uint64_t(descriptor.FirstThunk)+n*sizeof value;
            if(target%alignof(void*))return false;
            if(!image_copy(image,source,&name,sizeof name,limit)||!image_copy(image,target,&value,sizeof value,limit))return false;
            if(!name.u1.AddressOfData)break;
            if(IMAGE_SNAP_BY_ORDINAL64(name.u1.Ordinal))continue;
            std::array<char,96> text{};
            if(name.u1.AddressOfData>UINT32_MAX||!image_copy(image,name.u1.AddressOfData+2,text.data(),text.size(),limit)||!std::memchr(text.data(),0,text.size()))return false;
            for(unsigned k=0;k<names.size();++k)if(!std::strcmp(text.data(),names[k])){
                auto* pointer=reinterpret_cast<void*>(value.u1.Function);
                if(pointer!=expected[k]&&pointer!=replacements[k])return false;
                if(pending.size()>=16)return false;
                pending.push_back({reinterpret_cast<void**>(reinterpret_cast<std::uint8_t*>(image)+target),pointer,replacements[k]});found.set(k);
            }
        }
    }
    if(!found.all())return false;
    for(const auto& entry:pending){if(entry.expected==entry.replacement)continue;
        DWORD protection=0;if(!VirtualProtect(entry.slot,sizeof(void*),PAGE_READWRITE,&protection))return false;
        const auto observed=InterlockedCompareExchangePointer(reinterpret_cast<PVOID volatile*>(entry.slot),entry.replacement,entry.expected);
        DWORD ignored=0;const bool restored=VirtualProtect(entry.slot,sizeof(void*),protection,&ignored)!=FALSE;
        if(observed!=entry.expected||!restored)return false;
    }
    return true;
}
void begin_cursor(Window& state){
    if(state.cursor_owned)return;
#ifdef EVRM_MOD_INPUT_TESTING
    if(test_cursor_failure==1)throw std::runtime_error("injected cursor snapshot failure");
    state.game_clip={0,0,1920,1080};state.saved_position={100,100};state.game_cursor=nullptr;
#else
    CURSORINFO cursor{};cursor.cbSize=sizeof cursor;
    if(!GetClipCursor(&state.game_clip)||!GetCursorPos(&state.saved_position)||!GetCursorInfo(&cursor))throw std::runtime_error("cursor state unavailable");
    state.game_cursor=cursor.hCursor;
#endif
    state.clip_null=false;
    state.cursor_owned=true;state.capturing.store(true,std::memory_order_release);
    state.game_capture=capture_window();if(state.game_capture==state.handle)real_release();
    real_clip(nullptr);real_cursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));
    for(unsigned n=0;n<32;++n){++state.show_calls;const auto visible=show_cursor(true);
#ifdef EVRM_MOD_INPUT_TESTING
        if(test_cursor_failure==2)continue;
#endif
        if(visible>=0)return;}
    throw std::runtime_error("cursor visibility unavailable");
}
void restore_cursor(Window& state,bool normal)noexcept {
    state.capturing.store(false,std::memory_order_release);if(!state.cursor_owned)return;
    state.cursor_owned=false;
    if(normal&&focused(state.handle)){real_clip(state.clip_null?nullptr:&state.game_clip);real_warp(state.saved_position.x,state.saved_position.y);real_cursor(state.game_cursor);if(state.game_capture==state.handle)real_capture(state.handle);}
    else{real_clip(nullptr);if(capture_window()==state.handle)real_release();}
    while(state.show_calls){--state.show_calls;show_cursor(false);}
}
void relay(Window& state,const RAWINPUT& packet)noexcept {
    PacketScope scope(packet);CallWindowProcW(state.previous,state.handle,WM_INPUT,RIM_INPUT,reinterpret_cast<LPARAM>(&packet));
}
void release_raw_keys(Window& state)noexcept {
    state.raw_latched|=state.raw_down;
    for(unsigned identity=0;identity<raw_key_count;++identity)if(state.raw_forwarded[identity]){
        RAWINPUT packet{};packet.header=state.last_header;packet.header.dwType=RIM_TYPEKEYBOARD;packet.header.dwSize=sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD);
        packet.data.keyboard=state.raw_keys[identity];packet.data.keyboard.Flags|=RI_KEY_BREAK;packet.data.keyboard.Message=WM_KEYUP;relay(state,packet);
    }
    state.raw_forwarded.reset();
}
void releases(Window& state,const ModInputTransition& change)noexcept {
    if(change.opened||change.closed||change.release_keys.any()||change.release_buttons.any())release_raw_keys(state);
    for(unsigned key=0;key<256;++key)if(change.release_keys[key]){
        CallWindowProcW(state.previous,state.handle,WM_KEYUP,key,(LPARAM(1)<<31)|(LPARAM(1)<<30));
    }
    if(change.release_buttons.any()){
        RAWINPUT packet{};packet.header=state.last_header;packet.header.dwType=RIM_TYPEMOUSE;packet.header.dwSize=sizeof packet;
        for(unsigned button=0;button<5;++button)if(change.release_buttons[button])packet.data.mouse.usButtonFlags|=USHORT(2u<<(2*button));
        relay(state,packet);
        const UINT messages[]={WM_LBUTTONUP,WM_RBUTTONUP,WM_MBUTTONUP,WM_XBUTTONUP,WM_XBUTTONUP};
        for(unsigned button=0;button<5;++button)if(change.release_buttons[button])CallWindowProcW(state.previous,state.handle,messages[button],button>=3?WPARAM(button-2)<<16:0,0);
    }
}
void apply(Window& state,const ModInputTransition& change,bool normal=true){
    releases(state,change);
    if(change.opened)begin_cursor(state);
    if(change.closed)restore_cursor(state,normal);
}
bool raw_packet(LPARAM l,RAWINPUT& packet)noexcept {
    UINT size=sizeof packet;const auto read=owned_read(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,&packet,&size,sizeof(RAWINPUTHEADER));
    if(read==UINT(-1)||read>sizeof packet||read<sizeof(RAWINPUTHEADER)||packet.header.dwSize!=read)return false;
    return (packet.header.dwType==RIM_TYPEKEYBOARD&&read>=sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD))||
        (packet.header.dwType==RIM_TYPEMOUSE&&read>=sizeof(RAWINPUTHEADER)+sizeof(RAWMOUSE));
}
ModInputTransition button_event(Window& state,unsigned code,bool down,std::uint32_t time)noexcept {
    POINT point{};if(GetCursorPos(&point)&&ScreenToClient(state.handle,&point))return state.input.button(code,down,time,float(point.x),float(point.y));
    return state.input.button(code,down,time);
}
LRESULT CALLBACK procedure(HWND window,UINT message,WPARAM w,LPARAM l)noexcept {
    auto* state=static_cast<Window*>(GetPropW(window,property_name));
    if(!state)return DefWindowProcW(window,message,w,l);
    try{
        std::lock_guard<std::recursive_mutex> guard(state->mutex);const auto time=message_time();
        if(message==close_message){
            if(l&&std::uint64_t(l)!=state->admission.load(std::memory_order_acquire))return 0;
            if(w)state->allowed.store(false,std::memory_order_release);
            apply(*state,state->input.close(time));return 0;}
        if(message==WM_KILLFOCUS||(message==WM_ACTIVATEAPP&&!w)||message==WM_NCDESTROY){
            apply(*state,state->input.focus_lost(time),false);
            if(message==WM_NCDESTROY){state->allowed=false;RemovePropW(window,property_name);auto* expected=state;current.compare_exchange_strong(expected,nullptr);}
            return CallWindowProcW(state->previous,window,message,w,l);
        }
        if(!focused(window)&&state->input.open())apply(*state,state->input.focus_lost(time),false);
        const auto hotkey=state->allowed.load(std::memory_order_acquire)&&focused(window)?mod_settings_snapshot().hotkey:0x1000u;
        if(message==WM_INPUT){RAWINPUT packet{};
            if(raw_packet(l,packet)){
                state->last_header=packet.header;
                if(packet.header.dwType==RIM_TYPEKEYBOARD){const auto key=packet.data.keyboard.VKey;const bool down=!(packet.data.keyboard.Flags&RI_KEY_BREAK);
                    const auto identity=raw_identity(packet.data.keyboard);
                    if(identity==raw_key_count){state->keyboard_supported=false;state->allowed=false;release_raw_keys(*state);apply(*state,state->input.close(time),false);
                        return CallWindowProcW(state->previous,window,message,w,l);}
                    const bool previous=state->raw_down[identity];
                    if(down&&!previous){state->raw_keys[identity]=packet.data.keyboard;state->raw_down.set(identity);++state->raw_counts[virtual_key(key)];}
                    const auto logical=virtual_key(previous?state->raw_keys[identity].VKey:key);
                    if(!down&&previous){state->raw_down.reset(identity);if(state->raw_counts[logical])--state->raw_counts[logical];}
                    const bool latched=state->raw_latched[identity];
                    const auto change=state->input.key(logical,state->raw_counts[logical]!=0,time,hotkey);apply(*state,change);
                    const bool suppress=change.suppress||latched||state->raw_latched[identity];
                    if(!down){state->raw_latched.reset(identity);state->raw_forwarded.reset(identity);}
                    else if(!suppress)state->raw_forwarded.set(identity);
                    if(suppress)return real_default(window,message,w,l);
                }else{
                    const bool all_suppressed=state->input.motion(time).suppress;
                    bool suppressed=all_suppressed;auto filtered=packet;
                    for(unsigned button=0;button<5;++button)for(unsigned release=0;release<2;++release){const auto flag=USHORT((release?2u:1u)<<(2*button));
                        if(!(packet.data.mouse.usButtonFlags&flag))continue;
                        const auto change=button_event(*state,button,!release,time);apply(*state,change);
                        if(change.suppress){filtered.data.mouse.usButtonFlags&=USHORT(~flag);suppressed=true;}}
                    if(packet.data.mouse.usButtonFlags&(RI_MOUSE_WHEEL|RI_MOUSE_HWHEEL)){
                        const auto delta=SHORT(packet.data.mouse.usButtonData)/float(WHEEL_DELTA);
                        const auto change=state->input.wheel(packet.data.mouse.usButtonFlags&RI_MOUSE_HWHEEL?delta:0,packet.data.mouse.usButtonFlags&RI_MOUSE_WHEEL?delta:0,time);
                        apply(*state,change);if(change.suppress){filtered.data.mouse.usButtonFlags&=USHORT(~(RI_MOUSE_WHEEL|RI_MOUSE_HWHEEL));suppressed=true;}}
                    if(suppressed){if(!all_suppressed&&!state->input.open()&&!state->capturing.load(std::memory_order_acquire))relay(*state,filtered);
                        return real_default(window,message,w,l);}
                }
            }else if(state->input.open())return real_default(window,message,w,l);
        }
        if(message==WM_KEYDOWN||message==WM_KEYUP||message==WM_SYSKEYDOWN||message==WM_SYSKEYUP){
            const auto key=virtual_key(unsigned(w));const bool down=message==WM_KEYDOWN||message==WM_SYSKEYDOWN;
            const auto change=state->input.key(key,down||(key<256&&state->raw_counts[key]),time,hotkey);apply(*state,change);
            if(change.suppress)return 0;
        }
        unsigned button=0;bool down=false,is_button=true;
        switch(message){case WM_LBUTTONDOWN:case WM_LBUTTONDBLCLK:down=true;break;case WM_LBUTTONUP:break;
        case WM_RBUTTONDOWN:case WM_RBUTTONDBLCLK:button=1;down=true;break;case WM_RBUTTONUP:button=1;break;
        case WM_MBUTTONDOWN:case WM_MBUTTONDBLCLK:button=2;down=true;break;case WM_MBUTTONUP:button=2;break;
        case WM_XBUTTONDOWN:case WM_XBUTTONDBLCLK:button=HIWORD(w)==XBUTTON1?3:4;down=true;break;case WM_XBUTTONUP:button=HIWORD(w)==XBUTTON1?3:4;break;
        default:is_button=false;break;}
        if(is_button){const auto change=button_event(*state,button,down,time);apply(*state,change);if(change.suppress)return message==WM_XBUTTONDOWN||message==WM_XBUTTONUP?TRUE:0;}
        if(message==WM_MOUSEWHEEL||message==WM_MOUSEHWHEEL){const auto delta=SHORT(HIWORD(w))/float(WHEEL_DELTA);
            const auto change=state->input.wheel(message==WM_MOUSEHWHEEL?delta:0,message==WM_MOUSEWHEEL?delta:0,time);apply(*state,change);if(change.suppress)return 0;}
        if(state->input.open()){
            if(message==WM_SETCURSOR){real_cursor(LoadCursorW(nullptr,MAKEINTRESOURCEW(32512)));return TRUE;}
            if(message==WM_MOUSEMOVE||message==WM_CHAR||message==WM_SYSCHAR||message==WM_UNICHAR||message==WM_INPUT_DEVICE_CHANGE)return 0;
        }
        return CallWindowProcW(state->previous,window,message,w,l);
    }catch(...){std::lock_guard<std::recursive_mutex> guard(state->mutex);state->input.close(message_time());restore_cursor(*state,false);state->allowed=false;return real_default(window,message,w,l);}
}
}
bool mod_input_attach(HWND window,std::uint64_t* admission)noexcept {
    try{
        std::lock_guard<std::mutex> attach(attachment_mutex);DWORD process=0;
        if(!window||!IsWindow(window)||!GetWindowThreadProcessId(window,&process)||process!=GetCurrentProcessId()||GetAncestor(window,GA_ROOT)!=window)return false;
        if(auto* state=current.load(std::memory_order_acquire)){
            if(state->handle!=window)return false;
            if(!state->keyboard_supported.load(std::memory_order_acquire))return false;
            if(GetWindowLongPtrW(window,GWLP_WNDPROC)!=reinterpret_cast<LONG_PTR>(&procedure)){mod_input_request_close(window);return false;}
            const auto token=next_admission.fetch_add(1,std::memory_order_relaxed);state->admission.store(token,std::memory_order_release);
            state->allowed=true;if(admission)*admission=token;return true;
        }
        HMODULE pinned=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<LPCWSTR>(&procedure),&pinned))return false;
        static std::once_flag once;static bool imports_ready=false;std::call_once(once,[]{imports_ready=initialize_imports();});
        if(!imports_ready){log_event("mod-menu.input-unavailable reason=game-imports");return false;}
        if(!close_message)close_message=RegisterWindowMessageW(L"EnshroudedVRM.CloseMenu.1");
        if(!close_message)return false;
        if(GetPropW(window,property_name))return false;
        auto state=std::make_unique<Window>();state->handle=window;state->previous=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window,GWLP_WNDPROC));
        state->admission.store(next_admission.fetch_add(1,std::memory_order_relaxed),std::memory_order_release);
        if(!state->previous||!SetPropW(window,property_name,state.get()))return false;
        // Publish retained ownership before the callback becomes reachable. A
        // concurrent WM_NCDESTROY may clear it during SetWindowLongPtr itself.
        auto* retained=state.release();current.store(retained,std::memory_order_release);
        SetLastError(0);const auto previous=SetWindowLongPtrW(window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(&procedure));
        if(!previous){RemovePropW(window,property_name);auto* expected=retained;current.compare_exchange_strong(expected,nullptr);return false;}
        retained->previous=reinterpret_cast<WNDPROC>(previous);
#ifdef EVRM_MOD_INPUT_TESTING
        if(test_destroy_attach)DestroyWindow(window);
#endif
        const bool active=current.load(std::memory_order_acquire)==retained&&IsWindow(window);
        if(active&&admission)*admission=retained->admission.load(std::memory_order_acquire);
        return active;
    }catch(...){return false;}
}
bool mod_input_open(HWND window)noexcept {
    auto* state=current.load(std::memory_order_acquire);if(!state||state->handle!=window)return false;
    return state->capturing.load(std::memory_order_acquire);
}
std::uint64_t mod_input_admission(HWND window)noexcept {
    auto* state=current.load(std::memory_order_acquire);return state&&state->handle==window?state->admission.load(std::memory_order_acquire):0;
}
void mod_input_request_close(HWND window,std::uint64_t admission)noexcept {if(window&&close_message)PostMessageW(window,close_message,0,LPARAM(admission));}
void mod_input_retire(HWND window,std::uint64_t admission)noexcept {if(window&&close_message)PostMessageW(window,close_message,1,LPARAM(admission));}
ModInputFrame mod_input_drain(HWND window)noexcept {
    ModInputFrame result;auto* state=current.load(std::memory_order_acquire);if(!state||state->handle!=window)return result;
    try{
        std::lock_guard<std::recursive_mutex> guard(state->mutex);result.input=state->input.drain();result.open=state->input.open()&&focused(window);
        if(!result.open){if(state->input.open())mod_input_request_close(window);return result;}
        POINT point{};if(GetCursorPos(&point)&&ScreenToClient(window,&point)){result.x=float(point.x);result.y=float(point.y);}
        using Dpi=UINT(WINAPI*)(HWND);const auto dpi=procedure_address<Dpi>(GetModuleHandleW(L"user32.dll"),"GetDpiForWindow");
        if(dpi)result.dpi=std::clamp(dpi(window)/96.f,1.f,3.f);
    }catch(...){mod_input_request_close(window);}
    return result;
}
#ifdef EVRM_MOD_INPUT_TESTING
void mod_input_test_focus(bool value)noexcept {test_focus=value;}
void mod_input_test_destroy_during_attach(bool value)noexcept {test_destroy_attach=value;}
void mod_input_test_cursor_failure(unsigned value)noexcept {test_cursor_failure=value;}
void mod_input_test_raw(HWND window,const RAWINPUT& packet)noexcept {PacketScope scope(packet);SendMessageW(window,WM_INPUT,RIM_INPUT,reinterpret_cast<LPARAM>(&packet));}
ModInputTestCounters mod_input_test_counters()noexcept {return counters;}
HWND mod_input_test_capture()noexcept {return test_capture;}
#endif
}
