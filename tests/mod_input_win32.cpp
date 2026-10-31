#include "../src/mod_input_win32.hpp"
#include "../src/mod_settings_service.hpp"
#include <iostream>
#include <stdexcept>
#include <bitset>
extern "C" HCURSOR (WINAPI* __imp_SetCursor)(HCURSOR);
extern "C" UINT (WINAPI* __imp_GetRawInputData)(HRAWINPUT,UINT,LPVOID,PUINT,UINT);
namespace {
std::bitset<256> game_keys;std::bitset<5> game_buttons;unsigned key_releases=0,raw_calls=0,ordinary=0;LONG game_dx=0;
std::bitset<1024> game_scan_keys;
unsigned scan_identity(const RAWKEYBOARD& raw){return (raw.MakeCode&255u)|((raw.Flags&(RI_KEY_E0|RI_KEY_E1))<<7);}
void require(bool value,const char* message){if(!value)throw std::runtime_error(message);}
LRESULT CALLBACK game(HWND window,UINT message,WPARAM w,LPARAM l){
    if(message==WM_INPUT){RAWINPUT raw{};UINT size=0;++raw_calls;
        if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,nullptr,&size,sizeof(RAWINPUTHEADER))!=0||size>sizeof raw)return 0;
        if(GetRawInputData(reinterpret_cast<HRAWINPUT>(l),RID_INPUT,&raw,&size,sizeof(RAWINPUTHEADER))==UINT(-1))return 0;
        if(raw.header.dwType==RIM_TYPEKEYBOARD){game_scan_keys[scan_identity(raw.data.keyboard)]=!(raw.data.keyboard.Flags&RI_KEY_BREAK);const auto key=raw.data.keyboard.VKey;if(key<256){const bool down=!(raw.data.keyboard.Flags&RI_KEY_BREAK);game_keys[key]=down;if(!down)++key_releases;}}
        if(raw.header.dwType==RIM_TYPEMOUSE){game_dx+=raw.data.mouse.lLastX;for(unsigned i=0;i<5;++i){if(raw.data.mouse.usButtonFlags&(1u<<(2*i)))game_buttons.set(i);
            if(raw.data.mouse.usButtonFlags&(2u<<(2*i)))game_buttons.reset(i);}}
        return DefWindowProcW(window,message,w,l);
    }
    if(message==WM_APP+17)++ordinary;
    // This mimics the inspected game's default legacy-key handlers.
    return DefWindowProcW(window,message,w,l);
}
void key(HWND window,unsigned vk,bool down){RAWINPUT raw{};raw.header.dwType=RIM_TYPEKEYBOARD;raw.header.dwSize=sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD);
    raw.data.keyboard.VKey=USHORT(vk);raw.data.keyboard.MakeCode=USHORT(MapVirtualKeyW(vk,MAPVK_VK_TO_VSC));raw.data.keyboard.Flags=down?0:RI_KEY_BREAK;
    raw.data.keyboard.Message=down?WM_KEYDOWN:WM_KEYUP;evrm::mod_input_test_raw(window,raw);}
void scan_key(HWND window,unsigned vk,unsigned scan,unsigned flags,bool down){RAWINPUT raw{};raw.header.dwType=RIM_TYPEKEYBOARD;raw.header.dwSize=sizeof(RAWINPUTHEADER)+sizeof(RAWKEYBOARD);
    raw.data.keyboard.VKey=USHORT(vk);raw.data.keyboard.MakeCode=USHORT(scan);raw.data.keyboard.Flags=USHORT(flags|(down?0:RI_KEY_BREAK));raw.data.keyboard.Message=down?WM_KEYDOWN:WM_KEYUP;evrm::mod_input_test_raw(window,raw);}
void button(HWND window,unsigned index,bool down){RAWINPUT raw{};raw.header.dwType=RIM_TYPEMOUSE;raw.header.dwSize=sizeof raw;
    raw.data.mouse.usButtonFlags=USHORT((down?1u:2u)<<(2*index));evrm::mod_input_test_raw(window,raw);}
void pump(){MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}}
HCURSOR WINAPI unexpected_cursor(HCURSOR) {return nullptr;}
}
namespace evrm {ModSettings mod_settings_snapshot()noexcept{return {};}}
int main(int argc,char** argv){try{
    WNDCLASSW cls{};cls.hInstance=GetModuleHandleW(nullptr);cls.lpfnWndProc=&game;cls.lpszClassName=L"EVRMHiddenInputFixture";
    require(RegisterClassW(&cls)!=0,"window class");
    HWND window=CreateWindowExW(0,cls.lpszClassName,L"hidden input fixture",WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,cls.hInstance,nullptr);
    require(window!=nullptr&&!IsWindowVisible(window),"hidden window");
    if(argc==3&&std::string(argv[2])=="--unexpected-import"){
        const auto raw_before=__imp_GetRawInputData;DWORD previous=0,ignored=0;
        require(VirtualProtect(&__imp_SetCursor,sizeof(__imp_SetCursor),PAGE_READWRITE,&previous)!=FALSE,"fixture import page");
        __imp_SetCursor=&unexpected_cursor;
        require(VirtualProtect(&__imp_SetCursor,sizeof(__imp_SetCursor),previous,&ignored)!=FALSE,"fixture import page restored");
        require(!evrm::mod_input_attach(window),"unexpected prior hook refused");
        require(__imp_GetRawInputData==raw_before&&GetWindowLongPtrW(window,GWLP_WNDPROC)==reinterpret_cast<LONG_PTR>(&game),"failure leaves other imports/window unchanged");
        DestroyWindow(window);std::cout<<"PASS unexpected game import declines admission before any other patch\n";return 0;
    }
    require(!evrm::mod_input_attach(GetDesktopWindow()),"external window refused");
    require(evrm::mod_input_attach(window)&&evrm::mod_input_attach(window),"same-window repeated attach");
    evrm::mod_input_test_focus(true);
    const std::string mode=argc==3?argv[2]:"";
    if(mode=="--cursor-snapshot-failure"||mode=="--cursor-visibility-failure"){
        key(window,'W',true);button(window,0,true);evrm::mod_input_test_cursor_failure(mode=="--cursor-snapshot-failure"?1:2);key(window,VK_F8,true);
        require(!evrm::mod_input_open(window)&&!game_keys['W']&&!game_buttons[0],"cursor failure releases held movement and mouse");
        key(window,'W',false);button(window,0,false);require(!game_keys['W']&&!game_buttons[0],"failed-open tails remain neutral");
        require(evrm::mod_input_test_counters().show_balance==0,"failed cursor visibility balances owned increments");DestroyWindow(window);
        std::cout<<"PASS cursor setup failure releases held controls and restores ownership\n";return 0;
    }
    if(mode=="--paired-raw-keys"||mode=="--paired-raw-release"){
        struct Pair{unsigned key,scan,first_flags,second_scan,second_flags;};
        const Pair pairs[]={{VK_SHIFT,0x2a,0,0x36,0},{VK_CONTROL,0x1d,0,0x1d,RI_KEY_E0},{VK_MENU,0x38,0,0x38,RI_KEY_E0},{VK_RETURN,0x1c,0,0x1c,RI_KEY_E0}};
        for(const auto& pair:pairs){scan_key(window,pair.key,pair.scan,pair.first_flags,true);scan_key(window,pair.key,pair.second_scan,pair.second_flags,true);
            if(mode=="--paired-raw-release")scan_key(window,pair.key,pair.second_scan,pair.second_flags,false);
            key(window,VK_F8,true);key(window,VK_F8,false);require(game_scan_keys.none(),"every forwarded physical identity released on open");
            key(window,VK_ESCAPE,true);key(window,VK_ESCAPE,false);scan_key(window,pair.key,pair.scan,pair.first_flags,false);scan_key(window,pair.key,pair.second_scan,pair.second_flags,false);
            require(game_scan_keys.none(),"paired physical release tails stay neutral");}
        DestroyWindow(window);std::cout<<"PASS paired modifiers and keypad physical identities release independently\n";return 0;
    }
    key(window,VK_INSERT,true);require(!evrm::mod_input_open(window)&&game_keys[VK_INSERT],"Insert remains available to the game and Steam overlay");
    key(window,VK_INSERT,false);require(!game_keys[VK_INSERT],"Insert release forwarded");key_releases=0;
    key(window,'W',true);button(window,0,true);require(game_keys['W']&&game_buttons[0],"original raw path");
    SetCapture(window);require(evrm::mod_input_test_capture()==window,"game capture fixture");
    const auto before=evrm::mod_input_test_counters();
    key(window,VK_F8,true);require(evrm::mod_input_open(window),"raw F8 opens");
    require(!game_keys['W']&&!game_buttons[0]&&key_releases==1,"owned raw releases reach original game path");
    require(evrm::mod_input_test_capture()==nullptr,"opening releases game mouse capture");
    SetCapture(window);ReleaseCapture();SetCapture(window);require(evrm::mod_input_test_capture()==nullptr,"game cannot recapture while menu is open");
    const auto opened=evrm::mod_input_test_counters();require(opened.show_balance>0,"cursor ownership");
    require(SetCursorPos(500,500)&&ClipCursor(nullptr),"game cursor calls intercepted");SetCursor(nullptr);
    const auto suppressed=evrm::mod_input_test_counters();require(suppressed.cursor_moves==opened.cursor_moves&&suppressed.clips==opened.clips,"no game warp/clip while open");
    key(window,VK_F8,false);key(window,'A',true);button(window,1,true);
    require(!game_keys['A']&&!game_buttons[1],"menu input blocked on raw path");
    RAWINPUT mouse{};mouse.header.dwType=RIM_TYPEMOUSE;mouse.header.dwSize=sizeof mouse;mouse.data.mouse.lLastX=99;
    evrm::mod_input_test_raw(window,mouse);require(game_dx==0,"raw camera motion suppressed while open");
    SendMessageW(window,WM_APP+17,0,0);require(ordinary==1,"ordinary messages forwarded");
    evrm::mod_input_request_close(window);pump();require(!evrm::mod_input_open(window),"render-side close posted to window thread");
    require(evrm::mod_input_test_counters().show_balance==before.show_balance,"cursor balance restored");
    key(window,'A',false);mouse.data.mouse.lLastX=5;mouse.data.mouse.usButtonFlags=RI_MOUSE_RIGHT_BUTTON_UP;
    evrm::mod_input_test_raw(window,mouse);require(game_dx==5&&!game_buttons[1],"fresh motion survives a filtered close tail");
    key(window,'A',true);require(game_keys['A'],"fresh key after close");
    key(window,VK_F8,true);key(window,VK_F8,false);require(evrm::mod_input_open(window),"reopen");
    const auto moves=evrm::mod_input_test_counters().cursor_moves;evrm::mod_input_test_focus(false);SendMessageW(window,WM_KILLFOCUS,0,0);
    require(!evrm::mod_input_open(window)&&evrm::mod_input_test_counters().show_balance==0,"focus loss restores ownership");
    require(evrm::mod_input_test_counters().cursor_moves==moves,"focus loss does not warp another app");
    key(window,VK_F8,true);require(!evrm::mod_input_open(window),"background hotkey ignored");key(window,VK_F8,false);
    evrm::mod_input_test_focus(true);key(window,VK_F8,true);key(window,VK_F8,false);require(evrm::mod_input_open(window),"focus recovered");
    evrm::mod_input_retire(window);pump();require(!evrm::mod_input_open(window),"retirement closes");
    key(window,VK_F8,true);key(window,VK_F8,false);require(!evrm::mod_input_open(window),"retired adapter cannot capture");
    require(evrm::mod_input_attach(window),"new renderer re-admits live window");key(window,VK_F8,true);key(window,VK_F8,false);
    require(evrm::mod_input_open(window),"recreated renderer opens");DestroyWindow(window);
    require(!evrm::mod_input_open(window)&&evrm::mod_input_test_counters().show_balance==0,"destruction restores ownership");
    require(!IsWindow(window)&&!evrm::mod_input_attach(window),"dead window refused");
    HWND retiring=CreateWindowExW(0,cls.lpszClassName,L"retiring hidden fixture",WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,cls.hInstance,nullptr);
    evrm::mod_input_test_destroy_during_attach(true);require(!evrm::mod_input_attach(retiring)&&!IsWindow(retiring),"destroyed attach cannot publish a dead owner");
    evrm::mod_input_test_destroy_during_attach(false);
    HWND replacement=CreateWindowExW(0,cls.lpszClassName,L"replacement hidden fixture",WS_OVERLAPPEDWINDOW,0,0,800,600,nullptr,nullptr,cls.hInstance,nullptr);
    std::uint64_t old_admission=0,new_admission=0;
    require(evrm::mod_input_attach(replacement,&old_admission),"new HWND admitted after attach destruction");
    evrm::mod_input_retire(replacement,old_admission);
    require(evrm::mod_input_attach(replacement,&new_admission)&&new_admission!=old_admission,"new renderer admission advances generation");pump();
    require(evrm::mod_input_admission(replacement)==new_admission,"stale queued retirement cannot revoke new admission");
    key(replacement,VK_F8,true);key(replacement,VK_F8,false);require(evrm::mod_input_open(replacement),"new admission still accepts hotkey after old retirement");DestroyWindow(replacement);
    // The fixture's cursor operations are fake; no focus, cursor or OS input was changed.
    std::cout<<"PASS hidden Win32 raw/legacy ownership, held releases, cursor, focus and retirement; no global input\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
