"""Real ImGui player-menu fixture; CPU only, with no game or global input."""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class PlayerMenuTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1]
        cls.compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if os.name != 'nt' or not cls.compiler:
            raise unittest.SkipTest('Windows x64 GCC required')
        previous_error_mode = ctypes.windll.kernel32.GetErrorMode()
        ctypes.windll.kernel32.SetErrorMode(previous_error_mode | 0x8003)
        cls.addClassCleanup(ctypes.windll.kernel32.SetErrorMode, previous_error_mode)
        cls.folder = tempfile.TemporaryDirectory(prefix='evrm-player-menu-', ignore_cleanup_errors=True)
        cls.addClassCleanup(cls.folder.cleanup)
        cls.objects = []
        for source in ('imgui.cpp', 'imgui_draw.cpp', 'imgui_widgets.cpp', 'imgui_tables.cpp'):
            obj = Path(cls.folder.name) / (Path(source).stem + '.o')
            command = [cls.compiler, '-std=c++17', '-O2', '-c', str(cls.root / 'vendor/imgui' / source), '-o', str(obj)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=180)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)
            cls.objects.append(str(obj))

    def run_fixture(self, name):
        binary = Path(self.folder.name) / (name + '.exe')
        command = [self.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                   str(self.root / 'tests' / (name + '.cpp')), str(self.root / 'src/mod_menu.cpp'), *self.objects, '-o', str(binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('PASS ', result.stdout)
        print(result.stdout.strip())

    def test_player_menu(self):
        self.run_fixture('player_menu')

    def test_legacy_avatar_menu(self):
        self.run_fixture('avatar_menu')

    def test_legacy_menu(self):
        self.run_fixture('mod_menu')


if __name__ == '__main__':
    unittest.main()
