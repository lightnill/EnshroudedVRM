"""Compile actual HLSL predicates as owned scalar/vector CPU code; no GPU/game/DLL."""
from native_fixture_compiler import default_compiler, run_compiler
from pathlib import Path
import ctypes,os,shutil,subprocess,tempfile,unittest
class NativePlayerAnchorTests(unittest.TestCase):
    def test_actual_shader_position_predicate_and_explicit_host_binding(self):
        compiler=default_compiler()
        headers=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if os.name!='nt' or not compiler or not headers:self.skipTest('Windows compiler and explicit Vulkan headers required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        root=Path(__file__).resolve().parents[1];source=(root/'shaders/native_prepare.hlsl').read_text()
        config_start=source.index('    // EVRM_PLAYER_CONFIG_BEGIN') if '// EVRM_PLAYER_CONFIG_BEGIN' in source else source.index('    if(!all(isfinite(control.config))')
        position_start=source.index('    EVRM_MENU_STAGE(7u);')+len('    EVRM_MENU_STAGE(7u);')
        with tempfile.TemporaryDirectory(prefix='evrm-player-anchor-',ignore_cleanup_errors=True) as folder:
            folder=Path(folder)
            (folder/'anchor_config.inc').write_text(source[config_start:source.index('    const uint v=',config_start)])
            (folder/'anchor_position.inc').write_text(source[position_start:source.index('    EVRM_MENU_STAGE(8u);',position_start)])
            binary=folder/'anchor.exe'
            built=run_compiler([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-I'+str(folder),'-I'+headers,str(root/'tests/native_player_anchor.cpp'),'-o',str(binary)],capture_output=True,text=True,timeout=120)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr);print(ran.stdout.strip())
if __name__=='__main__':unittest.main()
