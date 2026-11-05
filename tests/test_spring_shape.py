"""Exercise hair shape retention using synthetic rigs, including virtual tips."""
from native_fixture_compiler import default_compiler, run_compiler
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class SpringShapeTests(unittest.TestCase):
    def test_shape_and_collision_constraints(self):
        compiler = default_compiler()
        if not compiler:
            self.skipTest('Set EVRM_TEST_COMPILER to a pinned x64 MSVC 14.44 compiler')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-spring-shape-') as directory:
            binary = Path(directory) / 'spring_shape.exe'
            run = run_compiler([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                  '-static', str(root / 'tests/spring_shape.cpp'),
                                  str(root / 'src/spring_bones.cpp'), '-o', str(binary)],
                                 capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())


if __name__ == '__main__':
    unittest.main()
