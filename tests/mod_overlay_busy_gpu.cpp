// Inspect the private fence classifier without constructing a GPU renderer.
#if defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wsubobject-linkage"
#endif
#include "../src/mod_menu_gpu.cpp"
#if defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#include <iostream>
#include <stdexcept>
namespace { VkResult response=VK_NOT_READY;
VkResult VKAPI_CALL fence_status(VkDevice,VkFence){return response;}
PFN_vkVoidFunction VKAPI_CALL get(VkDevice,const char*){return reinterpret_cast<PFN_vkVoidFunction>(fence_status);}
void require(bool ok,const char* msg){if(!ok)throw std::runtime_error(msg);}}
int main(){try{evrm::MenuGpuContext state;state.gdpa=get;state.slots.resize(2);state.slots[1].submitted=true;
require(state.ready()==evrm::ModMenuGpuReadiness::Busy&&!state.uncertain,"not-ready fence is transient");response=VK_ERROR_DEVICE_LOST;require(state.ready()==evrm::ModMenuGpuReadiness::Failed&&state.uncertain,"fence error is uncertain and persistent");response=VK_SUCCESS;require(state.ready()==evrm::ModMenuGpuReadiness::Failed,"uncertain renderer cannot resume");std::cout<<"PASS production renderer distinguishes transient fence from uncertain failure\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
