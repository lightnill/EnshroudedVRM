"""Metadata-only diagnostics policy; never opens the game or a microphone."""
import os,shutil,subprocess,tempfile,unittest
from pathlib import Path
class FallbackTests(unittest.TestCase):
    def test_episode_limits_and_named_capture(self):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:self.skipTest('Native compiler required')
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-fallback-') as folder:
            binary=Path(folder)/'fallback.exe'
            built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(root/'tests/fallback_diagnostics.cpp'),'-o',str(binary)],capture_output=True,text=True,timeout=90)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=20)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr);print(run.stdout.strip())
if __name__=='__main__':unittest.main()
