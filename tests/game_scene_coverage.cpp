// Independent input facts from build26404966's active renderer metadata and
// the shader tuple in the failing local debug report. No native shader bytes.
#include "../src/native_body_coverage.hpp"
#include <algorithm>
#include <cstdio>
#include <stdexcept>
#include <string>
namespace {
using Digest=std::array<std::uint8_t,32>;
Digest digest(const char* text){Digest result{};const auto nibble=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(std::size_t i=0;i<result.size();++i){result[i]=std::uint8_t(nibble(text[i*2])*16+nibble(text[i*2+1]));}
    return result;}
void need(bool yes,const char* why){if(!yes)throw std::runtime_error(why);}
evrm::NativeBodyShaderIdentity building(){evrm::NativeBodyShaderIdentity value;
    value.vertex_bytes=8036;value.vertex_digest=digest("743159a78dbd804210bedf87e330aec87d80878874ad0c3eb73d9c252e420788");
    value.fragment_bytes=5284;value.fragment_digest=digest("db9368b153c6ed5864ec205c3f1fd9b9c9a5dc1950481b468f8064916266d51f");return value;}
}
int main(int argc,char** argv){try{need(argc==2,"scenario required");const std::string scenario=argv[1];
    using namespace evrm;auto value=building();
    if(scenario=="building"){
        const auto result=classify_native_body_coverage(value);
        need(result.kind==NativeBodyCoverageKind::chunklet&&result.transform_binding==2,
             "updated building tuple rejected: native scenery would taint avatar replacement");
        need(result.clip_kind==NativeClipKind::none&&!result.instance_binding&&!result.culled_binding,
             "building tuple acquired character instance clipping authority");
        need(classify_debug_report_scene(value)==DebugReportSceneFamily::Building,"updated building absent from report scene context");
    }else if(scenario=="negative"){
        auto changed=value;changed.vertex_digest[0]^=1;need(classify_native_body_coverage(changed).kind==NativeBodyCoverageKind::unknown,"unknown vertex accepted");
        changed=value;changed.fragment_digest[0]^=1;need(classify_native_body_coverage(changed).kind==NativeBodyCoverageKind::unknown,"mixed fragment accepted");
        changed=value;changed.vertex_bytes-=4;need(classify_native_body_coverage(changed).kind==NativeBodyCoverageKind::unknown,"wrong byte count accepted");
        changed=value;changed.graphics_stage_count=4;changed.tess_control_bytes=20;changed.tess_control_digest[0]=1;
        need(classify_native_body_coverage(changed).kind==NativeBodyCoverageKind::unknown,"unproved tessellation tuple accepted");
        changed=value;changed.mesh_bytes=20;changed.mesh_digest[0]=1;
        need(classify_native_body_coverage(changed).kind==NativeBodyCoverageKind::unknown,"unproved mesh tuple accepted");
        changed=value;changed.vertex_digest=digest("13e2ccd24b50e99c55a8001a6f77144f57931fa0f6c742dd9fe3f9dce59d06f4");
        need(classify_native_body_coverage(changed).kind==NativeBodyCoverageKind::unknown,"previous building shader remained an admitted current-game tuple");
    }else if(scenario=="unchanged"){
        value.vertex_bytes=35200;value.vertex_digest=digest("dc1b3a77be743a3bae9422d76d1f4c5f195405b851d3211eab6cdd70b7ed4dac");
        value.fragment_bytes=3696;value.fragment_digest=digest("8f31a706d837f9c14acd2ba51d1325631e1e1a96961cab3acd4b27db0de293b0");
        auto result=classify_native_body_coverage(value);need(result.kind==NativeBodyCoverageKind::chunklet&&result.transform_binding==3,"unchanged terrain tuple lost");
        value.vertex_bytes=26928;value.vertex_digest=digest("b03b19480b45cdde956cf36d1fe591d68465bda7dbe97b4a454477b5c2b2c86d");
        value.fragment_bytes=1820;value.fragment_digest=digest("ccffc4db09667628d15669216fd7d8f903c650988bd5c19d5b85e2e3a9c72bef");
        result=classify_native_body_coverage(value);need(result.kind==NativeBodyCoverageKind::clip&&result.instance_binding==4&&result.culled_binding==5,"unchanged character instance domain lost");
    }else need(false,"unknown scenario");
    std::printf("PASS game scene %s\n",scenario.c_str());return 0;
}catch(const std::exception& error){std::fprintf(stderr,"FAIL %s\n",error.what());return 1;}}
