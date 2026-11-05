"""Actual exported service state and backend, extracted renderer transactions.

Trusted pose publications replace the live graphics bridge; no game/device.
"""
from native_fixture_compiler import default_compiler, run_compiler
from pathlib import Path
import ctypes,os,shutil,subprocess,tempfile,unittest
class NativePlayerFlowTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=default_compiler()
        headers=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        dxc=os.environ.get('EVRM_SHADER_COMPILER') or shutil.which('dxc')
        if os.name!='nt' or not compiler or not headers or not dxc:raise unittest.SkipTest('Windows MSVC 14.44, Vulkan headers and DXC required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        cls.temp=tempfile.TemporaryDirectory(prefix='evrm-player-flow-',ignore_cleanup_errors=True);cls.addClassCleanup(cls.temp.cleanup);cls.folder=Path(cls.temp.name)
        root=Path(__file__).resolve().parents[1];source=(root/'src/avatar_renderer.cpp').read_text()
        local=source[source.index('bool AvatarRenderer::stage_candidate('):source.index('bool AvatarRenderer::before_present(')]
        remote=source[source.index('std::shared_ptr<const PreparedAvatar> AvatarRenderer::prepared_player_model('):source.index('bool AvatarRenderer::before_present_players(')]
        (cls.folder/'flow_renderer_methods.inc').write_text((local+remote).replace('AvatarRenderer::','FlowRenderer::'))
        start=source.index('for(auto it=impl->players.begin();',source.index('bool AvatarRenderer::before_present_players('));end=source.index('        GpuSnapshot snapshot;',start)
        (cls.folder/'flow_before_present.inc').write_text(source[start:end])
        built=subprocess.run([dxc,'-spirv','-fspv-target-env=vulkan1.1','-WX','-T','cs_6_0','-E','main','-Fo',str(cls.folder/'native_player_commit.comp.spv'),str(root/'shaders/native_player_commit.hlsl')],capture_output=True,text=True,timeout=60)
        if built.returncode:raise RuntimeError(built.stdout+built.stderr)
        for entry,name in (('cs_skin','native_skinning.comp.spv'),('cs_finalize','native_skinning.finalize.spv')):
            built=subprocess.run([dxc,'-spirv','-fspv-target-env=vulkan1.1','-WX','-T','cs_6_0','-E',entry,'-Fo',str(cls.folder/name),str(root/'shaders/native_skinning.hlsl')],capture_output=True,text=True,timeout=60)
            if built.returncode:raise RuntimeError(built.stdout+built.stderr)
        cls.binary=cls.folder/'flow.exe'
        sources=[p for p in sorted((root/'src').glob('native*.cpp')) if p.name not in ('native_backend.cpp','native_visual_slot_write.cpp')]
        sources += [root/'src'/name for name in ('mouth_deformation.cpp','avatar_model.cpp','avatar_service.cpp','avatar_settings.cpp','player_assignments.cpp','vrm_asset.cpp','retarget.cpp')]
        built=run_compiler([compiler,'-std=c++17','-O2','-static','-Wall','-Wextra','-Werror','-Wno-subobject-linkage','-DEVRM_NATIVE_BACKEND_TESTING','-I'+headers,'-I'+str(cls.folder),str(root/'tests/native_player_flow.cpp'),*[str(p) for p in sources],'-lbcrypt','-lole32','-luuid','-luser32','-o',str(cls.binary)],capture_output=True,text=True,timeout=300)
        if built.returncode:raise RuntimeError(built.stdout+built.stderr)
    def run_case(self,mode):
        with tempfile.TemporaryDirectory(prefix='evrm-flow-state-',ignore_cleanup_errors=True) as folder:
            result=subprocess.run([str(self.binary),str(self.folder),folder,mode],capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())
    def test_candidate_only_remote_capture_activation_rejoin_and_none(self):self.run_case('candidate')
    def test_device_adoption_none_reenable_and_recreation(self):self.run_case('adoption')
    def test_offscreen_ready_target_does_not_block_other_targets(self):self.run_case('fairness')
if __name__=='__main__':unittest.main()
