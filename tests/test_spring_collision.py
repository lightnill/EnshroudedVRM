"""Run real spring collision regressions with synthetic geometry only."""
from native_fixture_compiler import default_compiler, run_compiler
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class SpringCollisionTests(unittest.TestCase):
    def test_collision_and_render_pose_constraints(self):
        compiler = default_compiler()
        if not compiler:
            self.skipTest('Set EVRM_TEST_COMPILER to a pinned x64 MSVC 14.44 compiler')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-spring-collision-') as directory:
            binary = Path(directory) / 'spring_collision.exe'
            run = run_compiler([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                  '-static', str(root / 'tests/spring_collision.cpp'),
                                  str(root / 'src/spring_bones.cpp'), '-o', str(binary)],
                                 capture_output=True, text=True, timeout=120)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())


if __name__ == '__main__':
    unittest.main()
