#include "../src/mod_present_lease.hpp"
#include <iostream>
#include <stdexcept>
namespace {void require(bool v,const char* message){if(!v)throw std::runtime_error(message);}}
int main(){try{
    evrm::ModPresentLease a,b;require(!a.can_draw(true),"no draw before acquisition");
    a.acquired(VK_TIMEOUT);require(!a.can_draw(true),"failed acquire ignored");a.acquired(VK_SUCCESS);
    require(!a.can_draw(false)&&a.can_draw(true),"busy frame skipped without losing acquisition");
    require(a.submit_result(VK_ERROR_OUT_OF_HOST_MEMORY)==evrm::ModPresentAction::Original&&a.can_draw(true),"submit OOM preserves original waits");
    require(a.submit_result(VK_SUCCESS)==evrm::ModPresentAction::Replace&&a.retry_present(),"successful submit replaces waits");
    a.present_result(VK_ERROR_OUT_OF_DEVICE_MEMORY);require(a.retry_present()&&!a.can_draw(true),"present OOM retries same completion, no second UI submit");
    a.present_result(VK_SUCCESS);require(a.waiting_present()&&!a.retry_present(),"present completion remains owned");
    a.acquired(VK_SUCCESS);require(!a.can_draw(true),"host acquire return alone is not present completion");
    a.fence_complete();require(a.can_draw(true),"matching present fence allows reuse");
    b.acquired(VK_SUBOPTIMAL_KHR);require(b.can_draw(true),"independent image/swapchain ownership");
    b.submit_result(VK_SUCCESS);b.present_result(VK_ERROR_OUT_OF_DATE_KHR);require(b.waiting_present(),"out-of-date still enqueues its waits");
    b.retire();require(!b.can_draw(true)&&b.waiting_present(),"retirement keeps pending ownership");b.fence_complete();require(b.safe_to_release(),"completion permits retirement");
    a.submit_result(VK_SUCCESS);a.present_result(VK_ERROR_DEVICE_LOST);require(a.safe_to_release()&&!a.can_draw(true),"lost device permits teardown only");
    evrm::ModPresentLease unknown;unknown.acquired(VK_SUCCESS);require(unknown.submit_result(VK_ERROR_UNKNOWN)==evrm::ModPresentAction::Stop&&!unknown.safe_to_release(),"unknown submit quarantines");
    std::cout<<"PASS present ownership acquire/busy, submit errors, wait replacement/retry, retirement and device loss\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
