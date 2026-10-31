#include "../src/mouth_deformation.hpp"
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
namespace {
void need(bool value,const char* why){if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
bool close(double a,double b){return std::abs(a-b)<1e-6;}
}
int main(){
    vrm::MouthData data;data.available=9;auto weights=vrm::mouth_expression_weights(data,{.2f,.3f,0,.5f,0},.5f);
    need(close(weights[0],.1)&&weights[1]==0&&close(weights[3],.25),"strength/missing-channel mask redistributed weights");
    need(vrm::mouth_expression_weights(data,{1,0,0,0,0},std::numeric_limits<float>::quiet_NaN())==vrm::MouthWeights{},"invalid strength did not neutralize");
    data.available=3;data.binary=1;
    weights=vrm::mouth_expression_weights(data,{.5f,0,0,0,0},1);need(weights[0]==0,"binary threshold must be strictly greater than 0.5");
    weights=vrm::mouth_expression_weights(data,{std::nextafter(.5f,1.f),0,0,0,0},1);need(weights[0]==1,"binary threshold missed next representable float");
    weights=vrm::mouth_expression_weights(data,{.6f,.4f,0,0,0},1);need(weights[0]==1&&weights[1]==.4f,"binary output was renormalized away from one");
    vrm::Vertex vertex;vertex.position={1,2,3};vertex.normal={0,0,1};vertex.uv={.25f,.75f};vertex.joints={1,2,3,4};vertex.weights={.25f,.25f,.25f,.25f};
    const auto base=vertex;vrm::MouthDeltaRecord delta;delta.position[0]={1,-2,.5f};delta.normal[0]={0,1,0};
    need(vrm::apply_mouth_delta(vertex,delta,{.5f,0,0,0,0}),"literal reference rejected");
    need(vertex.position==std::array<float,3>{1.5f,1,3.25f}&&close(vertex.normal[1],1/std::sqrt(5.))&&close(vertex.normal[2],2/std::sqrt(5.)),"literal position/normal reference wrong");
    need(vertex.uv==base.uv&&vertex.joints==base.joints&&vertex.weights==base.weights,"mouth changed UV or skin attributes");
    vertex=base;delta={};delta.normal[0]={0,0,-1};need(vrm::apply_mouth_delta(vertex,delta,{1,0,0,0,0})&&vertex.normal==base.normal,"cancelled normal did not use base fallback");
    vertex=base;delta.position[0][0]=std::numeric_limits<float>::infinity();need(!vrm::apply_mouth_delta(vertex,delta,{1,0,0,0,0})&&std::memcmp(&vertex,&base,sizeof vertex)==0,"invalid delta changed base vertex");
    delta={};need(!vrm::apply_mouth_delta(vertex,delta,{std::numeric_limits<float>::quiet_NaN(),0,0,0,0})&&std::memcmp(&vertex,&base,sizeof vertex)==0,"invalid weight changed base vertex");
    const auto plain=vrm::mouth_position_interval(base,nullptr);need(plain.valid&&plain.low[0]==1&&plain.high[2]==3,"absent mouth changed local bounds");
    delta={};for(unsigned c=0;c<5;++c)delta.position[c]={float(int(c)-2),float(c+1)*.1f,float(c%2?-3:2)};
    const auto bounds=vrm::mouth_position_interval(base,&delta);need(bounds.valid,"finite interval rejected");
    for(unsigned mask=0;mask<32;++mask){weights={};for(unsigned c=0;c<5;++c)weights[c]=mask&(1u<<c)?1.f:0.f;
        vertex=base;need(vrm::apply_mouth_delta(vertex,delta,weights),"extreme combination rejected");
        for(unsigned axis=0;axis<3;++axis)need(vertex.position[axis]>=bounds.low[axis]&&vertex.position[axis]<=bounds.high[axis],"mouth corner escaped conservative bounds");}
    vertex=base;vertex.position[0]=10000;delta={};delta.position[0][0]=-10000;delta.position[1][0]=.001f;
    const auto cancelling=vrm::mouth_position_interval(vertex,&delta);need(cancelling.valid&&cancelling.low[0]<0,"cancelling arithmetic had no rounding allowance");
    const auto layout=vrm::mouth_storage_layout(2000000,vrm::max_mouth_records,vrm::max_mouth_records,vrm::max_mouth_ranges);
    need(layout.valid&&layout.lookup_offset==128000000&&layout.records_offset%16==0&&layout.optional_peak_bytes<=64ull*1024*1024,"storage ABI/budget invalid at hard caps");
    need(!vrm::mouth_storage_layout(2000001,1,1,1).valid,"flattened vertex overflow accepted");
    need(!vrm::mouth_storage_layout(3,vrm::max_mouth_records+1,vrm::max_mouth_records+1,1).valid,"record cap bypassed");
    need(!vrm::mouth_storage_layout(2000000,vrm::max_mouth_records,vrm::max_mouth_records,4096,64ull*1024*1024).valid,"influence metadata escaped optional budget");
    std::puts("PASS literal mouth weights/deformation, independent corners and bounded storage ABI");
}
