"""Execute the production shader expressions as scalar CPU arithmetic.

The small float3 adapter supplies HLSL arithmetic only. Expressions are read
from the real fragment shaders, not restated in an expected-value function.
DXC/SPIR-V and independent GPU checks cover the language/driver boundary.
"""
import os
from pathlib import Path
import re
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

ADAPTER = r'''
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <stdexcept>
#include "@MATERIAL_HEADER@"
struct float3 {
    float x,y,z;
    float3(float v=0):x(v),y(v),z(v){}
    float3(float a,float b,float c):x(a),y(b),z(c){}
};
float3 operator+(float3 a,float3 b){return {a.x+b.x,a.y+b.y,a.z+b.z};}
float3 operator*(float3 a,float3 b){return {a.x*b.x,a.y*b.y,a.z*b.z};}
float3 operator*(float3 a,float b){return a*float3(b);}
float3 min(float3 a,float3 b){return {std::min(a.x,b.x),std::min(a.y,b.y),std::min(a.z,b.z)};}
using std::isfinite; using std::max;
@HELPER_INCLUDE@
struct Color {float3 rgb;float w=0;};
struct ShaderMaterial {Color emissive;};
struct Base {float3 rgb;float a;};
struct Surface {float3 lit,unlit;};
struct Control {uint32_t x=0,y=0,z=0,w=0;};
struct Frame {Control control;};
uint32_t bits(float f){uint32_t u;std::memcpy(&u,&f,4);return u;}
float asfloat(uint32_t u){float f;std::memcpy(&f,&u,4);return f;}
struct uint2 {uint32_t x,y;uint2(uint32_t a,uint32_t b):x(a),y(b){}};
struct uint4 {uint32_t x,y,z,w;uint4(uint32_t a,uint32_t b,uint32_t c,uint32_t d):x(a),y(b),z(c),w(d){}};
struct FrameWriter {
    uint32_t words[4]{};
    void Store2(unsigned offset,uint2 v){words[(offset-192)/4]=v.x;words[(offset-192)/4+1]=v.y;}
    void Store4(unsigned offset,uint4 v){words[(offset-192)/4]=v.x;words[(offset-192)/4+1]=v.y;words[(offset-192)/4+2]=v.z;words[(offset-192)/4+3]=v.w;}
};
Control prepared_control(){
    FrameWriter preparedFrame;const bool history=true;const unsigned l=0;
    const auto arena_word=[](unsigned offset){return bits(offset==16?2.f:32.f);};
    (void)l;(void)arena_word;
    @PREPARE_STORES@
    return {preparedFrame.words[0],preparedFrame.words[1],preparedFrame.words[2],preparedFrame.words[3]};
}
bool previous_control_eligible(Control previousControl){
    if(@HISTORY_REJECTION@)return false;
    return true;
}
float3 opaque(float3 unlit,float3 emission,float exposure,float gain){
    [[maybe_unused]] ShaderMaterial material{{emission,0}};
    [[maybe_unused]] Surface detailed{{0},unlit};
    [[maybe_unused]] Frame frame{{0,0,bits(exposure),bits(gain)}};
    return @OPAQUE@;
}
float3 forward(float3 color,float3 light,float3 lit,float3 emission,float3 unlit,float e,bool legacy,bool enhanced){
    [[maybe_unused]] const Base base{color,.239215686f};
    [[maybe_unused]] const ShaderMaterial material{{emission,legacy?1.f:0.f}};
    [[maybe_unused]] const Surface detailed{lit,unlit};
    [[maybe_unused]] const float3 P=light,sunColor=0;
    [[maybe_unused]] const float visibility=1,sunFactor=1,exposure=e;
    if(enhanced)return @FORWARD_ENHANCED@;
    return @FORWARD_BASIC@;
}
bool near(float3 a,float3 b){return std::abs(a.x-b.x)<1e-5f&&std::abs(a.y-b.y)<1e-5f&&std::abs(a.z-b.z)<1e-5f;}
void check(bool value,const char* why){if(!value)throw std::runtime_error(why);}
int main(int argc,char** argv){try{
    check(argc==2,"one case required");const std::string name=argv[1];
    if(name=="prepare-factors"){const auto c=prepared_control();check(c.x==1&&c.y==1&&c.z==bits(2.f)&&c.w==bits(32.f),"final preparation commit discarded the admitted lighting factors");}
    else if(name=="history-factors"){
        check(previous_control_eligible({1,1,bits(2.f),bits(32.f)}),"lighting payload incorrectly invalidates admitted enhanced motion history");
        check(!previous_control_eligible({0,1,bits(2.f),bits(32.f)})&&!previous_control_eligible({1,2,bits(2.f),bits(32.f)}),"lighting payload weakened commit/history authority");
    }
    else if(name=="opaque-dark")check(near(opaque({.2f,.3f,.4f},0,64,32)*(64*32),{.2f,.3f,.4f}),"opaque matcap amplified by exposure/emissive gain");
    else if(name=="opaque-day")check(near(opaque(.2f,0,.05f,4),.2f),"daylight reflective value changed");
    else if(name=="opaque-emission")check(near(opaque(0,{3,2,1},64,32),{3,2,1}),"true material emission clipped");
    else if(name=="opaque-zero")check(near(opaque(.2f,0,0,32),0),"unavailable reflective exposure not neutral");
    else if(name=="opaque-negative")check(near(opaque(.2f,0,1,-1),0),"negative reflective gain not neutral");
    else if(name=="opaque-nan")check(near(opaque(.2f,0,std::numeric_limits<float>::quiet_NaN(),1),0),"nonfinite reflective gain not neutral");
    else if(name=="opaque-overflow")check(near(opaque(.2f,0,1e30f,1e30f),0),"overflowing reflective amplification not bounded");
    else if(name=="opaque-cancelling")check(near(opaque(.2f,0,1e-10f,1e10f),.2f),"finite cancelling gains attenuated twice");
    else if(name=="forward-dark")check(near(forward(0,0,0,0,{.2f,.3f,.4f},32,true,true),{.2f,.3f,.4f}),"transparent matcap amplified by exposure");
    else if(name=="legacy-basic")check(near(forward(1,{8,3,2},0,0,0,4,true,false),1),"legacy transparent base exceeds authored diffuse ceiling in Fast/Matte");
    else if(name=="legacy-enhanced")check(near(forward(1,{8,3,2},0,0,0,4,true,true),1),"legacy transparent base exceeds authored diffuse ceiling in Enhanced");
    else if(name=="legacy-dim")check(near(forward(.5f,.25f,0,0,0,.2f,true,false),.025f),"ordinary dim diffuse changed");
    else if(name=="legacy-rim-emission")check(near(forward(.25f,4,.5f,2,.2f,2,true,true),8.45f),"legacy base ceiling clipped independent rim or true emission");
    else if(name=="generic-hdr")check(near(forward(.2f,8,.1f,2,0,4,false,true),17.6f),"generic HDR diffuse or emission changed");
    else if(name=="material-kind"){
        vrm::Material m;m.alpha=vrm::AlphaMode::Blend;m.base_color[3]=61.f/255.f;
        auto s=std::make_shared<vrm::MaterialSurface>();m.surface=s;s->kind=vrm::SurfaceKind::MToon0;
        const auto legacy=evrm::native_avatar_material(m,3,2,false);
        check(legacy.emissive[3]==1,"legacy MToon diffuse policy not propagated");
        check(sizeof(legacy)==80&&legacy.base[3]==61.f/255.f&&legacy.alpha[1]==2&&legacy.alpha[3]==2,"policy changed ABI, alpha or preset");
        s->kind=vrm::SurfaceKind::MToon1;check(evrm::native_avatar_material(m,3,0,false).emissive[3]==0,"MToon1 incorrectly inherits legacy ceiling");
        s->kind=vrm::SurfaceKind::Gltf;check(evrm::native_avatar_material(m,3,0,false).emissive[3]==0,"generic material incorrectly inherits legacy ceiling");
    }
    else throw std::runtime_error("unknown case");
    std::cout<<"PASS "<<name<<'\n';return 0;
}catch(const std::exception& e){std::cerr<<"FAIL "<<e.what()<<'\n';return 1;}}
'''


class MaterialRadianceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:
            raise unittest.SkipTest('Set EVRM_TEST_COMPILER to local x64 GCC')
        cls.temp = tempfile.TemporaryDirectory(prefix='evrm-material-radiance-')
        folder = Path(cls.temp.name)
        oit = (ROOT / 'shaders/avatar_native_oit.hlsl').read_text()
        expressions = re.findall(r'const float3 L=([^;]+);', oit)
        if len(expressions) != 2:
            raise AssertionError('Expected the production Enhanced and Basic radiance expressions')
        gbuffer = (ROOT / 'shaders/avatar_native.hlsl').read_text()
        opaque = re.findall(r'^\s*o\.emissive=pack_rgb9e5\((.+)\);$', gbuffer, re.M)[0]
        preprocessed = subprocess.run([compiler, '-E', '-P', '-x', 'c++', '-DEVRM_NATIVE_SURFACE=1', str(ROOT / 'shaders/native_prepare.hlsl')], capture_output=True, text=True, timeout=30)
        if preprocessed.returncode:
            raise AssertionError('Preparation preprocessing failed:\n' + preprocessed.stderr)
        stores = '\n'.join(line for line in preprocessed.stdout.splitlines() if re.match(r'\s*preparedFrame\.Store[24]\((?:192|200)u,', line))
        if not stores:
            raise AssertionError('Production preparation commit was not found')
        rejection = re.findall(r'if\((previousControl\.x[^\n]+)\)return false;', preprocessed.stdout)[0]
        rejection = rejection.replace('any(previousControl.zw!=0u)', '(previousControl.z!=0u||previousControl.w!=0u)')
        helper = ROOT / 'shaders/native_material_radiance.hlsli'
        code = ADAPTER.replace('@MATERIAL_HEADER@', (ROOT / 'src/native_avatar_material.hpp').as_posix())
        code = code.replace('@HELPER_INCLUDE@', '#include "' + helper.as_posix() + '"' if helper.exists() else '')
        code = code.replace('@OPAQUE@', opaque).replace('@FORWARD_ENHANCED@', expressions[0]).replace('@FORWARD_BASIC@', expressions[1]).replace('@PREPARE_STORES@', stores).replace('@HISTORY_REJECTION@', rejection)
        cpp = folder / 'radiance.cpp'
        cpp.write_text(code)
        cls.binary = folder / 'radiance.exe'
        run = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', str(cpp), '-o', str(cls.binary)], capture_output=True, text=True, timeout=120)
        if run.returncode:
            raise AssertionError('CPU adapter did not compile:\n' + run.stdout + run.stderr)

    @classmethod
    def tearDownClass(cls):
        cls.temp.cleanup()

    def case(self, name):
        run = subprocess.run([str(self.binary), name], capture_output=True, text=True, timeout=10)
        self.assertEqual(run.returncode, 0, run.stdout + run.stderr)

    def test_opaque_dark(self): self.case('opaque-dark')
    def test_opaque_day(self): self.case('opaque-day')
    def test_true_emission(self): self.case('opaque-emission')
    def test_zero_exposure(self): self.case('opaque-zero')
    def test_negative_gain(self): self.case('opaque-negative')
    def test_nonfinite_gain(self): self.case('opaque-nan')
    def test_overflow_gain(self): self.case('opaque-overflow')
    def test_cancelling_gains(self): self.case('opaque-cancelling')
    def test_forward_dark(self): self.case('forward-dark')
    def test_legacy_basic_ceiling(self): self.case('legacy-basic')
    def test_legacy_enhanced_ceiling(self): self.case('legacy-enhanced')
    def test_ordinary_dim_diffuse(self): self.case('legacy-dim')
    def test_rim_and_emission(self): self.case('legacy-rim-emission')
    def test_generic_hdr(self): self.case('generic-hdr')
    def test_material_kind_and_alpha(self): self.case('material-kind')
    def test_prepared_factors_survive_commit(self): self.case('prepare-factors')
    def test_lighting_payload_preserves_motion_history(self): self.case('history-factors')


if __name__ == '__main__':
    unittest.main()
