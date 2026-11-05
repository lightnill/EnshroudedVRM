"""Updated native contracts in owned CPU memory; no game or graphics device."""
from native_fixture_compiler import default_compiler, run_compiler
from pathlib import Path
import ctypes,json,os,shutil,subprocess,tempfile,unittest

class GameUpdateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=default_compiler()
        if os.name!='nt' or not compiler:raise unittest.SkipTest('explicit Windows compiler required')
        cls.root=Path(__file__).resolve().parents[1]
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        cls.folder=tempfile.TemporaryDirectory(prefix='evrm-game-update-',ignore_cleanup_errors=True)
        cls.addClassCleanup(cls.folder.cleanup);cls.exe=Path(cls.folder.name)/'native_game_update.exe'
        built=run_compiler([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',
            str(cls.root/'tests/native_game_update.cpp'),
            *[str(cls.root/'src'/name) for name in ('native_owner.cpp','local_voice_observation.cpp','local_voice_copy.cpp','voice_clock.cpp')],
            '-o',str(cls.exe)],capture_output=True,text=True,timeout=180)
        if built.returncode:raise RuntimeError(built.stdout+built.stderr)
    def run_case(self,name):
        ran=subprocess.run([str(self.exe),name],capture_output=True,text=True,timeout=30)
        self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr);print(ran.stdout)
    def test_new_world_layout(self):self.run_case('world')
    def test_new_preview_layout(self):self.run_case('preview')
    def test_new_voice_identity(self):self.run_case('voice')
    def test_debug_group_boundary(self):self.run_case('report')
    def test_mixed_bindings_decline(self):self.run_case('mixed-bindings')
    def test_previous_game_declines_before_layout(self):self.run_case('old-image')
    def test_changed_image_decline(self):self.run_case('changed-image')
    def test_previous_root_cannot_rescue_current(self):self.run_case('old-global')
    def test_previous_vtable_declines(self):self.run_case('old-vtable')
    def test_previous_descriptor_declines(self):self.run_case('old-descriptor')
    def test_current_input_startup_profile(self):
        profile=json.loads((self.root/'portable/bootstrap/input-trigger.json').read_bytes())
        self.assertEqual(profile,{'timestamp':1793791052,'image_size':44298240,
            'iat_rva':0x13cf598,'return_rva':0x139c9c7,
            'game_sha256':'5d9ef2f140b8ed49e30dbee655841b89d08baec20f799a1817e171a18a850c77'})

if __name__=='__main__':unittest.main()
