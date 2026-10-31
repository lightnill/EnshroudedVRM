"""Compile immutable retarget preparation tests with synthetic rigs only."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class RetargetCacheTests(unittest.TestCase):
    def test_rejected_view_preserves_history(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:
            self.skipTest('Set EVRM_TEST_COMPILER to a local x64 GCC compiler')
        root = Path(__file__).resolve().parents[1]
        renderer = (root / 'src/avatar_renderer.cpp').read_text(encoding='utf-8')
        start = renderer.index('    ViewMotion& motion(')
        end = renderer.index('    double contact_ms=', start)
        with tempfile.TemporaryDirectory(prefix='evrm-retarget-views-') as folder:
            generated = Path(folder)
            (generated / 'renderer_pose_methods.inc').write_text(renderer[start:end], encoding='utf-8')
            binary = generated / 'retarget_views.exe'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-static', '-I' + str(generated), str(root / 'tests/retarget_views.cpp'),
                                    str(root / 'src/retarget.cpp'), str(root / 'src/vrm_asset.cpp'),
                                    '-o', str(binary)], capture_output=True, text=True, timeout=180)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())

    def test_repeated_pose_preparation(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:
            self.skipTest('Set EVRM_TEST_COMPILER to a local x64 GCC compiler')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-retarget-cache-') as folder:
            binary = Path(folder) / 'retarget_cache.exe'
            build = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror',
                                    '-static', str(root / 'tests/retarget_cache.cpp'),
                                    str(root / 'src/retarget.cpp'), str(root / 'src/vrm_asset.cpp'),
                                    '-o', str(binary)], capture_output=True, text=True, timeout=180)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())


if __name__ == '__main__':
    unittest.main()
