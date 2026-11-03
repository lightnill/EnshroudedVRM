"""Private persistent choices in owned temporary files; no game or DLL."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest
class PlayerAssignmentsTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if os.name!='nt' or not compiler:raise unittest.SkipTest('Windows compiler required')
        cls.root=Path(__file__).resolve().parents[1]
        cls.folder=tempfile.TemporaryDirectory(prefix='evrm-player-assignments-',ignore_cleanup_errors=True);cls.addClassCleanup(cls.folder.cleanup)
        cls.exe=Path(cls.folder.name)/'assignment-fixture.exe'
        sources=[str(cls.root/'tests/player_assignments.cpp')];flags=[]
        if (cls.root/'src/player_assignments.cpp').exists():sources += [str(cls.root/'src/player_assignments.cpp'),str(cls.root/'src/avatar_settings.cpp')];flags += ['-municode']
        built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',*flags,*sources,'-lbcrypt','-o',str(cls.exe)],capture_output=True,text=True,timeout=180)
        if built.returncode:raise RuntimeError(built.stdout+built.stderr)
    def run_case(self,mode):
        with tempfile.TemporaryDirectory(prefix='evrm-private-choice-',ignore_cleanup_errors=True) as folder:
            ran=subprocess.run([str(self.exe),mode,folder],capture_output=True,text=True,timeout=30)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
    def test_installation_scoped_account_tokens(self):self.run_case('tokens')
    def test_independent_choices_and_none_survive_restart(self):self.run_case('persistence')
    def test_invalid_storage_does_not_clobber_choices(self):self.run_case('invalid')
    def test_full_store_does_not_evict_remembered_players(self):self.run_case('capacity')
if __name__=='__main__':unittest.main()
