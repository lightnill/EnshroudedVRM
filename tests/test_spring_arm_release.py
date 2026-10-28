"""Check moving-arm release with synthetic rigs, without private assets or the game."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class SpringArmReleaseTests(unittest.TestCase):
    def test_moving_arm_release_and_body_controls(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:
            self.skipTest('Set EVRM_TEST_COMPILER to a local x64 GCC compiler')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-spring-arm-') as directory:
            binary = Path(directory) / 'spring_arm_release.exe'
            run = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                  '-static', str(root / 'tests/spring_arm_release.cpp'),
                                  str(root / 'src/spring_bones.cpp'), '-o', str(binary)],
                                 capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())


if __name__ == '__main__':
    unittest.main()
