// Compile the renderer's real admission methods at a synthetic view boundary.
// The Python driver extracts them unchanged; no Vulkan device is needed.
#define main retarget_cache_fixture_main
#include "retarget_cache.cpp"
#undef main

struct RendererBoundary {
    vrm::Avatar avatar=rig();
    struct ViewMotion {
        std::unique_ptr<int> springs;
        std::unique_ptr<vrm::Retargeter> retargeter;
    };
    std::map<std::uint64_t,ViewMotion> view_motion;
#include "renderer_pose_methods.inc"
};

int main(){try{
    RendererBoundary renderer;
    const auto valid=source(renderer.avatar);
    auto options=aligned();options.avatar_rest_height=2;
    std::array<const int*,4> histories{};
    std::array<const vrm::Retargeter*,4> caches{};
    for(unsigned i=1;i<=4;++i){
        check(bool(renderer.retarget_pose(valid,options,i)),"Valid view rejected");
        auto& state=renderer.view_motion.at(i);
        state.springs=std::make_unique<int>(i);
        histories[i-1]=state.springs.get();caches[i-1]=state.retargeter.get();
    }
    const auto retained=[&](){
        check(renderer.view_motion.size()==4,"Rejected view changed retained view count");
        for(unsigned i=1;i<=4;++i){
            const auto found=renderer.view_motion.find(i);
            check(found!=renderer.view_motion.end(),"Rejected view discarded accepted history");
            check(found->second.springs.get()==histories[i-1]&&*found->second.springs==int(i),
                  "Rejected view replaced spring history");
            check(found->second.retargeter.get()==caches[i-1],"Rejected view replaced accepted cache");
        }
    };
    auto rejected=valid;rejected.current.at("hips")[12]=std::numeric_limits<float>::max();
    const auto reference=vrm::retarget(renderer.avatar,rejected,options);
    check(!reference,"Expected finite output-range rejection");
    for(unsigned view:{5u,6u,1u,7u}){
        const auto result=renderer.retarget_pose(rejected,options,view);
        check(!result&&result.error==reference.error,"Renderer changed retarget rejection");
        retained();
    }
    check(bool(renderer.retarget_pose(valid,options,1)),"Existing view did not recover");retained();
    check(bool(renderer.retarget_pose(valid,options,5)),"New view did not recover");
    check(renderer.view_motion.size()==1&&renderer.view_motion.count(5),"Successful new view did not follow four-view eviction");
    auto* admitted=renderer.view_motion.at(5).retargeter.get();
    check(bool(renderer.retarget_pose(valid,options,5)),"Warm admitted view failed");
    check(renderer.view_motion.at(5).retargeter.get()==admitted,"Warm view replaced its prepared cache");
    std::cout<<"PASS renderer admission: rejected new/existing views preserve all history, recovery, warm reuse and successful four-view eviction\n";
    return 0;
}catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}}
