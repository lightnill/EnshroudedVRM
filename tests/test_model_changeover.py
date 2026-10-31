"""Owned-memory tool publication lifecycle; no game or executable patching."""
from pathlib import Path
import ctypes, os, shutil, subprocess, tempfile, unittest

class ModelChangeoverTests(unittest.TestCase):
    def test_tool_slot_lifecycle(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if os.name != 'nt' or not compiler:
            self.skipTest('Windows native compiler required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode() | 0x8003)
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-changeover-', ignore_cleanup_errors=True) as folder:
            binary = Path(folder) / 'tool_slot_lifecycle.exe'
            command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                       '-DEVRM_NATIVE_TOOL_SLOTS_TESTING', str(root/'tests/tool_slot_lifecycle.cpp'),
                       str(root/'src/native_tool_slots_runtime.cpp'), str(root/'src/native_tool_slots.cpp'), '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=180)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main()
