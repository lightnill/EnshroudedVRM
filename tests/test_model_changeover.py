"""Owned-memory tool publication lifecycle; no game or executable patching."""
from native_fixture_compiler import default_compiler, run_compiler
from pathlib import Path
import ctypes, os, re, shutil, subprocess, tempfile, unittest

class ModelChangeoverTests(unittest.TestCase):
    def test_tool_slot_lifecycle(self):
        self.run_lifecycle(False)

    def test_tool_slot_process_pose_clock(self):
        self.run_lifecycle(True)

    def run_lifecycle(self, process_clock):
        compiler = default_compiler()
        if os.name != 'nt' or not compiler:
            self.skipTest('Windows native compiler required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode() | 0x8003)
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-changeover-', ignore_cleanup_errors=True) as folder:
            if process_clock:
                code=(root/'src/native_owner_process.cpp').read_text(encoding='utf-8')
                match=re.search(r'std::uint64_t NativeOwnerProcess::milliseconds\(\)const noexcept\s*\{(.*?)\}',code,re.S)
                self.assertIsNotNone(match)
                (Path(folder)/'native_owner_pose_clock.inc').write_text(match.group(1),encoding='utf-8')
            binary = Path(folder) / 'tool_slot_lifecycle.exe'
            command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                       '-I'+folder,*(['-DEVRM_TOOL_PROCESS_POSE_CLOCK'] if process_clock else []),
                       '-DEVRM_NATIVE_TOOL_SLOTS_TESTING', str(root/'tests/tool_slot_lifecycle.cpp'),
                       str(root/'src/native_tool_slots_runtime.cpp'), str(root/'src/native_tool_slots.cpp'),
                       str(root/'tests/standalone_fixture_context.cpp'), '-o', str(binary)]
            built = run_compiler(command, capture_output=True, text=True, timeout=180)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main()
