"""Exercise world-space hair inertia with synthetic rigs and no private assets."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class SpringInertialTests(unittest.TestCase):
    def test_inertia_without_added_airflow(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:
            self.skipTest('Set EVRM_TEST_COMPILER to a local x64 GCC compiler')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-spring-inertial-') as directory:
            binary = Path(directory) / 'spring_inertial.exe'
            run = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                  '-static', str(root / 'tests/spring_inertial.cpp'),
                                  str(root / 'src/spring_bones.cpp'), '-o', str(binary)],
                                 capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())


if __name__ == '__main__':
    unittest.main()
