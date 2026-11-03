"""Actual private recorders against nonthrowing fake Vulkan functions; no GPU."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class NativeSurfaceDrawTests(unittest.TestCase):
    def test_mixed_recording_preflight_and_lifetime(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        headers = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY') or os.environ.get('EVRM_VULKAN_INCLUDE')
        if os.name != 'nt' or not compiler or not headers or not (Path(headers) / 'vulkan/vulkan.h').is_file():
            self.skipTest('Windows GCC and explicit Vulkan headers required')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-surface-draw-', ignore_cleanup_errors=True) as directory:
            binary = Path(directory) / 'native_surface_draw.exe'
            result = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                                     '-I' + headers, str(root / 'tests/native_surface_draw.cpp'),
                                     str(root / 'src/native_draw.cpp'), str(root / 'src/native_oit_draw.cpp'),
                                     '-o', str(binary)], capture_output=True, text=True, timeout=120)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('native surface draw: all checks passed', result.stdout)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
