"""Deterministic VRM mouth fixtures; no game, network or microphone."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class MouthAnimationTests(unittest.TestCase):
    def run_native(self, name, sources):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler or os.name != 'nt':
            self.skipTest('Windows x64 GCC required')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-mouth-', ignore_cleanup_errors=True) as folder:
            binary = Path(folder) / (name + '.exe')
            command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', str(root / 'tests' / (name + '.cpp'))]
            command.extend(str(root / 'src' / source) for source in sources)
            if name == 'mouth_gpu':
                include = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
                dxc = os.environ.get('EVRM_SHADER_COMPILER')
                self.assertTrue(include and dxc, 'Set EVRM_VULKAN_INCLUDE_DIRECTORY and EVRM_SHADER_COMPILER')
                command.append('-I' + include)
                for entry, output in (('cs_skin', 'native_skinning.comp.spv'), ('cs_finalize', 'native_skinning.finalize.spv')):
                    shader = subprocess.run([dxc, '-spirv', '-fspv-target-env=vulkan1.1', '-WX', '-T', 'cs_6_0', '-E', entry,
                                             str(root / 'shaders/native_skinning.hlsl'), '-Fo', str(Path(folder) / output)],
                                            capture_output=True, text=True, timeout=60)
                    self.assertEqual(shader.returncode, 0, shader.stdout + shader.stderr)
            result = subprocess.run(command + ['-o', str(binary)], capture_output=True, text=True, timeout=180)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            arguments = [str(binary)]
            if name == 'mouth_import' and os.environ.get('EVRM_TEST_AVATAR'):
                arguments.append(os.environ['EVRM_TEST_AVATAR'])
            if name == 'mouth_gpu':
                arguments.append(folder)
            result = subprocess.run(arguments, capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS ', result.stdout)
            print(result.stdout.strip())

    def test_import(self):
        self.run_native('mouth_import', ('vrm_asset.cpp',))

    def test_deformation(self):
        self.run_native('mouth_deformation', ('mouth_deformation.cpp',))

    def test_skinning(self):
        self.run_native('mouth_skinning', ('mouth_deformation.cpp', 'native_skinning.cpp', 'native_avatar_pose.cpp', 'native_material_plan.cpp'))

    def test_gpu(self):
        self.run_native('mouth_gpu', ('mouth_deformation.cpp', 'native_skinning_gpu.cpp', 'native_skinning.cpp', 'native_avatar_pose.cpp', 'native_material_plan.cpp'))

    def test_publication(self):
        self.run_native('mouth_publication', ('mouth_deformation.cpp', 'native_material_plan.cpp'))


if __name__ == '__main__':
    unittest.main()
