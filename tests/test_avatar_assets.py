"""Real Vulkan avatar upload/retirement fixtures without game or user assets."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
class AvatarAssetTests(unittest.TestCase):
    def test_gpu_upload_lifetimes(self):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++');shader=os.environ.get('EVRM_SHADER_COMPILER');vulkan=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if os.name!='nt' or not all((compiler,shader,vulkan)):self.skipTest('Windows compiler, DXC and Vulkan headers required')
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-avatar-gpu-',ignore_cleanup_errors=True) as folder:
            folder=Path(folder)
            for source,entry,name in (('shaders/native_skinning.hlsl','cs_skin','native_skinning.comp.spv'),('shaders/native_skinning.hlsl','cs_finalize','native_skinning.finalize.spv'),('tests/avatar_texture_probe.hlsl','cs_main','avatar_texture_probe.spv'),('tests/avatar_material_probe.hlsl','cs_material','avatar_material_probe.spv')):
                result=subprocess.run([shader,'-spirv','-fspv-target-env=vulkan1.1','-T','cs_6_0','-E',entry,str(root/source),'-Fo',str(folder/name)],capture_output=True,text=True,timeout=60)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            binary=folder/'assets.exe'
            sources=('native_avatar_assets.cpp','native_skinning_gpu.cpp','native_skinning.cpp','native_avatar_pose.cpp','native_material_plan.cpp','mouth_deformation.cpp')
            built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-municode','-I'+vulkan,str(root/'tests/native_avatar_assets.cpp'),*[str(root/'src'/name) for name in sources],'-o',str(binary)],capture_output=True,text=True,timeout=180)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            try:result=subprocess.run([str(binary),str(folder)],capture_output=True,text=True,timeout=30)
            except subprocess.TimeoutExpired as failure:
                self.fail('GPU fixture timed out: '+repr(failure.stdout)+' '+repr(failure.stderr))
            self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())
if __name__=='__main__':unittest.main()
