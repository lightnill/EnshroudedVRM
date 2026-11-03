#define VK_NO_PROTOTYPES
#include "../src/native_oit_draw.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <type_traits>
#include <vector>
#include <windows.h>

void need(bool value,const char* text){if(!value)throw std::runtime_error(text);}
struct float4 {float x,y,z,w;};
struct Range {float minimum=0,maximum=0;};
using std::max;
bool world_bounds(float4 clipMin,float4 clipMax,Range& result){
#include "oit_w_bounds.inc"
    result={wMin,wMax};
#include "oit_w_guard.inc"
    return true;
}
bool menu_bounds(float4 clipMin,float4 clipMax,Range& result){
#define EVRM_NATIVE_MENU_PREVIEW 1
#include "oit_w_bounds.inc"
#undef EVRM_NATIVE_MENU_PREVIEW
    result={wMin,wMax};
#include "oit_w_guard.inc"
    return true;
}

// Independent homogeneous Sutherland-Hodgman reference, not shader helpers.
using Point=std::array<double,4>;
double plane(const Point& p,unsigned n){switch(n){case 0:return p[3]+p[0];case 1:return p[3]-p[0];case 2:return p[3]+p[1];case 3:return p[3]-p[1];case 4:return p[2];default:return p[3]-p[2];}}
std::vector<Point> clipped(std::vector<Point> polygon){
    for(unsigned n=0;n<6&&!polygon.empty();++n){
        std::vector<Point> next;auto a=polygon.back();double da=plane(a,n);
        for(const auto& b:polygon){const double db=plane(b,n);
            if((da>=0)!=(db>=0)){Point intersection{};const double t=da/(da-db);for(unsigned k=0;k<4;++k)intersection[k]=a[k]+t*(b[k]-a[k]);next.push_back(intersection);}
            if(db>=0)next.push_back(b);
            a=b;da=db;
        }
        polygon=std::move(next);
    }
    return polygon;
}
void bounds_cases(){
    Range result;
    need(world_bounds({0,0,.01f,2},{0,0,.01f,4},result)&&result.minimum>1.99f&&result.minimum<2&&result.maximum>4&&result.maximum<4.01f,"ordinary front-facing depth range changed");
    need(world_bounds({-1,-1,.01f,-1},{1,1,.01f,2},result),"camera-straddling transparency rejected visible clipped fragments");
    need(result.minimum>.009f&&result.minimum<.01f&&result.maximum>2,"clipped interval lost the positive raster depth floor");
    need(!menu_bounds({-1,-1,.01f,-1},{1,1,.01f,2},result),"legacy menu predicate changed");
    need(world_bounds({-1,-1,.01f,-2},{1,1,.01f,-1},result)&&clipped({Point{-1,-1,.01,-2},Point{1,-1,.01,-1},Point{0,1,.01,-1}}).empty(),"fully clipped BLEND set must not veto other geometry");
    need(!world_bounds({0,0,.00001f,-1},{0,0,.01f,1},result),"unproved tiny raster floor bypassed numeric guard");
    need(!world_bounds({0,0,-.01f,-1},{0,0,.01f,1},result),"nonpositive raster floor bypassed numeric guard");
    need(!world_bounds({0,0,.01f,-1},{0,0,.01f,2000000},result),"maximum depth cap changed");
    need(!world_bounds({0,0,.01f,-1},{0,0,.01f,100},result),"depth spread cap changed");
    need(!world_bounds({0,0,.01f,4},{0,0,.01f,2},result),"reversed interval accepted");
    unsigned visible=0;std::uint32_t state=0x1234abcd;
    const auto random=[&](){state=1664525u*state+1013904223u;return float(state>>8)*(1.f/16777216.f);};
    for(unsigned test=0;test<1024;++test){
        std::vector<Point> triangle;
        float4 lo{INFINITY,INFINITY,INFINITY,INFINITY},hi{-INFINITY,-INFINITY,-INFINITY,-INFINITY};
        for(unsigned v=0;v<3;++v){const std::array<float,4> p{4*random()-2,4*random()-2,.01f+.002f*random(),6*random()-3};triangle.push_back({p[0],p[1],p[2],p[3]});
            lo.x=std::min(lo.x,p[0]);lo.y=std::min(lo.y,p[1]);lo.z=std::min(lo.z,p[2]);lo.w=std::min(lo.w,p[3]);
            hi.x=std::max(hi.x,p[0]);hi.y=std::max(hi.y,p[1]);hi.z=std::max(hi.z,p[2]);hi.w=std::max(hi.w,p[3]);}
        need(world_bounds(lo,hi,result),"bounded triangle with proved positive raster floor rejected");
        const auto polygon=clipped(std::move(triangle));if(polygon.empty())continue;++visible;
        for(const auto& p:polygon)need(p[3]>=result.minimum&&p[3]<=result.maximum,"admitted interval excludes an independently clipped vertex");
        if(polygon.size()>=3){const double fragment_w=1/(.2/polygon[0][3]+.3/polygon[1][3]+.5/polygon[2][3]);
            need(fragment_w>=result.minimum&&fragment_w<=result.maximum,"interval excludes perspective-interpolated fragment W");}
    }
    need(visible>100,"independent clipping cases did not exercise visible triangles");
    std::cout<<"PASS actual HLSL W bounds: camera crossing, empty clipped set, legacy menu, numeric refusals; independently clipped visible triangles="<<visible<<"/1024\n";
}

namespace pipeline_fixture {
template<class T>T handle(std::uintptr_t value){if constexpr(std::is_pointer_v<T>)return reinterpret_cast<T>(value);else return static_cast<T>(value);}
unsigned created=0,destroyed=0;
VkResult VKAPI_CALL create(VkDevice,VkPipelineCache,std::uint32_t count,const VkGraphicsPipelineCreateInfo* info,const VkAllocationCallbacks*,VkPipeline* out){
    need(count==1&&info&&out,"invalid pipeline fixture request");
    need(info->pInputAssemblyState&&info->pInputAssemblyState->topology==VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,"clipping proof requires triangles");
    const auto* raster=info->pRasterizationState;const auto* viewport=info->pViewportState;
    need(raster&&viewport&&!raster->pNext&&!viewport->pNext&&!raster->depthClampEnable&&!raster->rasterizerDiscardEnable,"OIT pipeline no longer guarantees standard depth clipping");
    const auto* dynamic=info->pDynamicState;need(dynamic,"dynamic state missing");
    for(unsigned i=0;i<dynamic->dynamicStateCount;++i)need(dynamic->pDynamicStates[i]!=VK_DYNAMIC_STATE_DEPTH_CLAMP_ENABLE_EXT&&dynamic->pDynamicStates[i]!=VK_DYNAMIC_STATE_DEPTH_CLIP_ENABLE_EXT&&dynamic->pDynamicStates[i]!=VK_DYNAMIC_STATE_DEPTH_CLIP_NEGATIVE_ONE_TO_ONE_EXT,"dynamic clipping invalidates OIT bound proof");
    *out=handle<VkPipeline>(12);++created;return VK_SUCCESS;
}
void VKAPI_CALL destroy(VkDevice,VkPipeline,const VkAllocationCallbacks*){++destroyed;}
void VKAPI_CALL bind(VkCommandBuffer,VkPipelineBindPoint,VkPipeline){}
void VKAPI_CALL descriptors(VkCommandBuffer,VkPipelineBindPoint,VkPipelineLayout,std::uint32_t,std::uint32_t,const VkDescriptorSet*,std::uint32_t,const std::uint32_t*){}
void VKAPI_CALL draw(VkCommandBuffer,std::uint32_t,std::uint32_t,std::uint32_t,std::uint32_t){}
PFN_vkVoidFunction VKAPI_CALL get(VkDevice,const char* name){
    if(!std::strcmp(name,"vkCreateGraphicsPipelines"))return reinterpret_cast<PFN_vkVoidFunction>(create);
    if(!std::strcmp(name,"vkDestroyPipeline"))return reinterpret_cast<PFN_vkVoidFunction>(destroy);
    if(!std::strcmp(name,"vkCmdBindPipeline"))return reinterpret_cast<PFN_vkVoidFunction>(bind);
    if(!std::strcmp(name,"vkCmdBindDescriptorSets"))return reinterpret_cast<PFN_vkVoidFunction>(descriptors);
    if(!std::strcmp(name,"vkCmdDraw"))return reinterpret_cast<PFN_vkVoidFunction>(draw);
    return nullptr;
}
void run(){
    {evrm::NativeOitDrawPipelines pipeline(handle<VkDevice>(1),get,handle<VkPipelineLayout>(2),handle<VkShaderModule>(3),handle<VkShaderModule>(4),true);}
    need(created==1&&destroyed==1,"actual OIT pipeline lifecycle not exercised");
    std::cout<<"PASS actual OIT pipeline guarantees triangle depth clipping and zero-to-one clip volume\n";
}
}
int main(){
    SetErrorMode(SEM_FAILCRITICALERRORS|SEM_NOGPFAULTERRORBOX|SEM_NOOPENFILEERRORBOX);
    try{pipeline_fixture::run();bounds_cases();return 0;}
    catch(const std::exception& error){std::cerr<<"FAIL "<<error.what()<<'\n';return 1;}
}
