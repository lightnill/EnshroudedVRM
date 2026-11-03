"""Exact updated native scenery identities; owned CPU code, no graphics device."""
from pathlib import Path
import ctypes,os,shutil,subprocess,tempfile,unittest

class GameSceneCoverageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if os.name!='nt' or not compiler:raise unittest.SkipTest('explicit Windows compiler required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        cls.root=Path(__file__).resolve().parents[1];cls.folder=tempfile.TemporaryDirectory(prefix='evrm-game-scene-',ignore_cleanup_errors=True)
        cls.addClassCleanup(cls.folder.cleanup);cls.binary=Path(cls.folder.name)/'game_scene.exe'
        r=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(cls.root/'tests/game_scene_coverage.cpp'),str(cls.root/'src/native_body_coverage.cpp'),'-o',str(cls.binary)],capture_output=True,text=True,timeout=120)
        if r.returncode:raise RuntimeError(r.stdout+r.stderr)
    def run_case(self,scenario):
        r=subprocess.run([str(self.binary),scenario],capture_output=True,text=True,timeout=30)
        self.assertEqual(r.returncode,0,r.stdout+r.stderr)
    def test_updated_building_keeps_scenery_without_character_authority(self):self.run_case('building')
    def test_mixed_unknown_and_previous_shader_tuples_decline(self):self.run_case('negative')
    def test_unchanged_terrain_and_character_domains(self):self.run_case('unchanged')

if __name__=='__main__':unittest.main()
