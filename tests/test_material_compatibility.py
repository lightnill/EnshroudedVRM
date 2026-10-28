"""Compile and execute the real importer against synthetic material controls."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class MaterialCompatibilityTests(unittest.TestCase):
    def test_import_render_paths(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:
            self.skipTest('Set EVRM_TEST_COMPILER to a local x64 GCC compiler')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-material-test-') as folder:
            binary = Path(folder) / 'material_compatibility.exe'
            result = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra',
                                     '-Werror', '-static', str(root / 'tests/material_compatibility.cpp'),
                                     str(root / 'src/vrm_asset.cpp'), '-o', str(binary)],
                                    capture_output=True, text=True, timeout=180)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
