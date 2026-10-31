#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include "mod_input_state.hpp"
namespace evrm {
// Bind only HWNDs obtained from successful Vulkan Win32 surface creation.
bool mod_input_attach(HWND,std::uint64_t* admission=nullptr)noexcept;
bool mod_input_open(HWND)noexcept;
std::uint64_t mod_input_admission(HWND)noexcept;
void mod_input_request_close(HWND,std::uint64_t admission=0)noexcept;
void mod_input_retire(HWND,std::uint64_t admission=0)noexcept;
struct ModInputFrame {ModInputBatch input;bool open=false;float x=0,y=0,dpi=1;};
ModInputFrame mod_input_drain(HWND)noexcept;
#ifdef EVRM_MOD_INPUT_TESTING
struct ModInputTestCounters {unsigned cursor_moves=0,clips=0,cursor_sets=0,cleanups=0;int show_balance=0;};
void mod_input_test_focus(bool)noexcept;
void mod_input_test_destroy_during_attach(bool)noexcept;
void mod_input_test_cursor_failure(unsigned)noexcept;
void mod_input_test_raw(HWND,const RAWINPUT&)noexcept;
ModInputTestCounters mod_input_test_counters()noexcept;
HWND mod_input_test_capture()noexcept;
#endif
}
