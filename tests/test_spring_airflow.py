"""Exercise movement-derived hair airflow using synthetic rigs only."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class SpringAirflowTests(unittest.TestCase):
    def test_travel_airflow(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:
            self.skipTest('Set EVRM_TEST_COMPILER to a local x64 GCC compiler')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-spring-airflow-') as directory:
            binary = Path(directory) / 'spring_airflow.exe'
            run = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                  '-static', str(root / 'tests/spring_airflow.cpp'),
                                  str(root / 'src/spring_bones.cpp'), '-o', str(binary)],
                                 capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())


if __name__ == '__main__':
    unittest.main()
