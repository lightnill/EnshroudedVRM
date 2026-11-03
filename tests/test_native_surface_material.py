"""Actual material feature routing/constants; no graphics or game access."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class NativeSurfaceMaterialTests(unittest.TestCase):
    def test_effective_selection_and_native_constants(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:
            self.skipTest('Set EVRM_TEST_COMPILER to local x64 GCC')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-surface-policy-') as folder:
            binary = Path(folder) / 'surface_policy.exe'
            result = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                                     str(root / 'tests/native_surface_material.cpp'), '-o', str(binary)],
                                    capture_output=True, text=True, timeout=180)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main()
