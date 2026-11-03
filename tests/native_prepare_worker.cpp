// CPU-only worker contract. The sole fake is downstream pipeline creation.
// Breaks caught: render waits, duplicate/stale jobs, double adoption/destruction,
// freeing borrowed handles during creation, and quarantine freeing owned work.
#define VK_NO_PROTOTYPES
#include "../src/native_prepare_worker.hpp"
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <cmath>
#include <cstring>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <vector>
#include <windows.h>
using Worker=evrm::NativePrepareWorker;
using Key=Worker::Key;
template<class T>T handle(std::uintptr_t value){return reinterpret_cast<T>(value);}
void require(bool okay,const char* reason){if(!okay)throw std::runtime_error(reason);}
template<class F>void eventually(F f,const char* message){const auto end=std::chrono::steady_clock::now()+std::chrono::seconds(3);while(!f()){require(std::chrono::steady_clock::now()<end,message);std::this_thread::sleep_for(std::chrono::milliseconds(1));}}
struct Driver {
 std::mutex mutex;std::condition_variable changed;
 bool blocked=true,entered=false,released=false,active=false,early_destroy=false,bad_inputs=false;
 unsigned calls=0,destroys=0;VkResult result=VK_SUCCESS;bool return_handle=true;
 std::array<VkPipeline,8> destroyed{};
 std::thread::id caller;
 static Driver* current;
 static VkResult VKAPI_CALL create(VkDevice device,VkPipelineCache cache,unsigned count,const VkComputePipelineCreateInfo* info,const VkAllocationCallbacks* alloc,VkPipeline* out)noexcept{
  auto& d=*current;std::unique_lock<std::mutex> lock(d.mutex);++d.calls;d.active=d.entered=true;d.caller=std::this_thread::get_id();
  if(device!=handle<VkDevice>(1)||cache||count!=1||alloc||!info||info->sType!=VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO||info->pNext||info->flags||info->basePipelineHandle||info->basePipelineIndex||info->layout!=handle<VkPipelineLayout>(2)||info->stage.sType!=VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO||info->stage.stage!=VK_SHADER_STAGE_COMPUTE_BIT||info->stage.module!=handle<VkShaderModule>(3)||std::strcmp(info->stage.pName,"cs_prepare"))d.bad_inputs=true;
  d.changed.notify_all();if(d.blocked)d.changed.wait(lock,[&]{return d.released;});
  *out=d.return_handle?handle<VkPipeline>(100+d.calls):VK_NULL_HANDLE;d.active=false;return d.result;
 }
 static void VKAPI_CALL destroy(VkDevice device,VkPipeline pipeline,const VkAllocationCallbacks* alloc)noexcept{
  auto& d=*current;std::lock_guard<std::mutex> lock(d.mutex);if(d.active)d.early_destroy=true;if(device!=handle<VkDevice>(1)||alloc||!pipeline||d.destroys==d.destroyed.size()){d.bad_inputs=true;return;}d.destroyed[d.destroys++]=pipeline;
 }
 Driver(){current=this;}
 void wait(){std::unique_lock<std::mutex> lock(mutex);require(changed.wait_for(lock,std::chrono::seconds(2),[&]{return entered;}),"compiler not entered");}
 void release()noexcept{{std::lock_guard<std::mutex> lock(mutex);released=true;}changed.notify_all();}
 ~Driver(){release();}
};
Driver* Driver::current=nullptr;
struct ReleaseOnExit{Driver& driver;~ReleaseOnExit(){driver.release();}};
bool request(Worker& w,Key key){return w.request(key,handle<VkPipelineLayout>(2),handle<VkShaderModule>(3));}
Worker::Completion take(Worker& w,Key key){return w.poll(key,handle<VkPipelineLayout>(2),handle<VkShaderModule>(3));}
void responsive(){
 Driver d;Worker worker(handle<VkDevice>(1),Driver::create,Driver::destroy);ReleaseOnExit release{d};
 eventually([&]{return request(worker,Key::Menu);},"first request rejected");d.wait();
 const auto started=std::chrono::steady_clock::now();
 for(unsigned n=0;n<100;++n){request(worker,Key::SurfaceWorld);request(worker,Key::SurfaceMenu);request(worker,Key::Menu);require(!take(worker,Key::Menu).complete,"unfinished pipeline published");}
 require(std::chrono::steady_clock::now()-started<std::chrono::milliseconds(100),"render request/poll waited for compiler");
 require(!worker.request(static_cast<Key>(3),handle<VkPipelineLayout>(2),handle<VkShaderModule>(3)),"fourth keyed job accepted");
 require(!worker.request(Key::Menu,handle<VkPipelineLayout>(8),handle<VkShaderModule>(3)),"same key accepted mismatched layout");
 require(!worker.poll(Key::Menu,handle<VkPipelineLayout>(2),handle<VkShaderModule>(9)).complete,"mismatched module consumed result");
 std::vector<std::thread> callers;
 for(unsigned n=0;n<8;++n)callers.emplace_back([&]{for(unsigned j=0;j<300;++j){request(worker,Key::Menu);request(worker,Key::SurfaceWorld);request(worker,Key::SurfaceMenu);take(worker,Key::Menu);}});
 for(auto& thread:callers)thread.join();
 eventually([&]{return request(worker,Key::SurfaceWorld)&&request(worker,Key::SurfaceMenu);},"bounded jobs never queued");
 require(d.calls==1,"duplicate request reentered blocked compiler");d.release();
 std::array<VkPipeline,3> adopted{};unsigned index=0;
 for(const auto key:{Key::Menu,Key::SurfaceWorld,Key::SurfaceMenu}){Worker::Completion result;eventually([&]{result=take(worker,key);return result.complete;},"completion missing");require(result.result==VK_SUCCESS&&result.pipeline&&result.milliseconds>=0&&std::isfinite(result.milliseconds),"bad pipeline result or duration");adopted[index++]=result.pipeline;require(!take(worker,key).complete,"pipeline adopted twice");}
 worker.shutdown(false);require(d.calls==3&&!d.destroys&&!d.bad_inputs&&d.caller!=std::this_thread::get_id(),"wrong worker count, arguments, thread or adopted ownership");
 for(auto pipeline:adopted)Driver::destroy(handle<VkDevice>(1),pipeline,nullptr);
 require(d.destroys==3&&!d.early_destroy,"caller did not retain exactly adopted ownership");
 std::cout<<"PASS worker blocked compiler, concurrent bounded requests, exact inputs, identity and one-time adoption\n";
}
void failures(){
 for(const auto result:{VK_ERROR_OUT_OF_DEVICE_MEMORY,VK_SUCCESS}){
  Driver d;d.blocked=false;d.result=result;d.return_handle=result!=VK_SUCCESS;
  Worker worker(handle<VkDevice>(1),Driver::create,Driver::destroy);request(worker,Key::Menu);Worker::Completion done;
  eventually([&]{done=take(worker,Key::Menu);return done.complete;},"failed job never completed");
  require(done.result!=VK_SUCCESS&&!done.pipeline,"failure or null success transferred invalid ownership");
  for(unsigned n=0;n<10;++n){request(worker,Key::Menu);require(!take(worker,Key::Menu).complete,"failure was reported or compiled twice");}
  worker.shutdown(false);worker.shutdown(false);require(d.calls==1&&d.destroys==(d.return_handle?1u:0u)&&!d.early_destroy&&!d.bad_inputs,"partial failure handle leaked or was freed twice");
 }
 std::cout<<"PASS worker partial failure and null-success ownership remain bounded\n";
}
void teardown(bool quarantine){
 Driver d;Worker worker(handle<VkDevice>(1),Driver::create,Driver::destroy);ReleaseOnExit release{d};request(worker,Key::Menu);d.wait();request(worker,Key::SurfaceWorld);request(worker,Key::SurfaceMenu);
 std::atomic<bool> started{false},finished{false};std::thread closer([&]{started=true;worker.shutdown(quarantine);finished=true;});
 eventually([&]{return started.load();},"teardown not started");std::this_thread::sleep_for(std::chrono::milliseconds(20));
 const bool joined_early=finished.load();d.release();closer.join();
 require(!joined_early&&finished&&d.calls==1&&!d.early_destroy&&!d.bad_inputs&&d.destroys==(quarantine?0u:1u),"teardown did not join creation, cancel queued work or preserve quarantine");
 require(!request(worker,Key::Menu)&&!take(worker,Key::Menu).complete,"stopped worker accepted or published work");
 std::cout<<"PASS worker joins active creation, cancels queued jobs and "<<(quarantine?"quarantines":"destroys")<<" unadopted handle\n";
}
int main(){SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);std::set_terminate([]{std::fputs("FAIL terminate\n",stderr);std::_Exit(98);});std::signal(SIGABRT,[](int){std::fputs("FAIL abort\n",stderr);std::_Exit(99);});SetUnhandledExceptionFilter([](EXCEPTION_POINTERS* e)->LONG{std::fprintf(stderr,"FAIL unhandled exception=%08lx\n",e&&e->ExceptionRecord?e->ExceptionRecord->ExceptionCode:0UL);std::_Exit(97);});try{responsive();failures();teardown(false);teardown(true);}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}return 0;}
