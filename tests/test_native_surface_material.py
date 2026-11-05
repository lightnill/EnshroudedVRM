"""Actual material feature routing/constants; no graphics or game access."""
from native_fixture_compiler import default_compiler, run_compiler
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

class NativeSurfaceMaterialTests(unittest.TestCase):
    def test_effective_selection_and_native_constants(self):
        compiler = default_compiler()
        if not compiler:
            self.skipTest('Set EVRM_TEST_COMPILER to pinned x64 MSVC 14.44')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-surface-policy-') as folder:
            binary = Path(folder) / 'surface_policy.exe'
            result = run_compiler([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                                     str(root / 'tests/native_surface_material.cpp'), '-o', str(binary)],
                                    capture_output=True, text=True, timeout=180)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main()
