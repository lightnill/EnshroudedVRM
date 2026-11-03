"""CPU-only contract and compiled shader checks for common player admission."""
import ctypes
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


SOURCE = Path(__file__).resolve().parents[1]


class NativePlayerCommitTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        cls.headers = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        cls.dxc = os.environ.get('EVRM_SHADER_COMPILER') or shutil.which('dxc')
        cls.validator = os.environ.get('EVRM_SPIRV_VALIDATOR') or shutil.which('spirv-val')
        if (os.name != 'nt' or not cls.headers
                or not (Path(cls.headers) / 'vulkan/vulkan.h').is_file()
                or any(not tool or not shutil.which(tool)
                       for tool in (cls.compiler, cls.dxc, cls.validator))):
            raise unittest.SkipTest('Windows GCC, explicit Vulkan headers, DXC and spirv-val required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode() | 0x8003)

    def test_owned_vulkan_commands_lifetime_and_common_permits(self):
        with tempfile.TemporaryDirectory(prefix='evrm-player-commit-', ignore_cleanup_errors=True) as folder:
            output = Path(folder) / 'native_player_commit.exe'
            command = [self.compiler, '-std=c++17', '-O2', '-static', '-Wall', '-Wextra', '-Werror', '-pedantic',
                       '-I' + self.headers, str(SOURCE / 'tests/native_player_commit.cpp'),
                       str(SOURCE / 'src/native_player_commit.cpp'), '-o', str(output)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            shader = Path(folder) / 'native_player_commit.comp.spv'
            compiled = subprocess.run([self.dxc, '-spirv', '-fspv-target-env=vulkan1.1', '-WX',
                                       '-T', 'cs_6_0', '-E', 'main', '-Fo', str(shader),
                                       str(SOURCE / 'shaders/native_player_commit.hlsl')],
                                      capture_output=True, text=True, timeout=60)
            self.assertEqual(compiled.returncode, 0, compiled.stdout + compiled.stderr)
            validated = subprocess.run([self.validator, '--target-env', 'vulkan1.1', str(shader)],
                                       capture_output=True, text=True, timeout=30)
            self.assertEqual(validated.returncode, 0, validated.stdout + validated.stderr)
            checked = subprocess.run([str(output), str(shader)], capture_output=True,
                                     text=True, timeout=30)
            self.assertEqual(checked.returncode, 0, checked.stdout + checked.stderr)
            self.assertIn('native player commit: all checks passed', checked.stdout)


if __name__ == '__main__':
    unittest.main()
