"""Guarded CPU-only tests of real NativeBackend surface integration; no GPU."""
from native_fixture_compiler import default_compiler, run_compiler, native_dependencies
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest


class NativeSurfaceBackendTests(unittest.TestCase):
    def test_actual_backend_surface_integration(self):
        compiler = default_compiler()
        headers = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if os.name != 'nt' or not compiler or not headers:
            self.skipTest('Windows MSVC 14.44 and explicit Vulkan headers required')
        headers = Path(headers).as_posix()
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode() | 0x8003)
        root = Path(__file__).resolve().parents[1]
        evidence = os.environ.get('EVRM_SURFACE_BACKEND_EVIDENCE')
        evidence = Path(evidence) if evidence else None
        if evidence:
            evidence.mkdir(parents=True, exist_ok=True)
        sources = [p for p in sorted((root / 'src').glob('native*.cpp'))
                   if p.name not in ('native_backend.cpp', 'native_state.cpp', 'native_visual_slot_write.cpp')]
        translation_units = [root / 'tests/native_surface_backend.cpp', *sources,
                             root / 'src/mouth_deformation.cpp']
        dependencies, consumed = native_dependencies(
            [compiler, '-std=c++17',
             '-DEVRM_NATIVE_BACKEND_TESTING', '-I' + headers,
             *[p.relative_to(root).as_posix() for p in translation_units]],
            cwd=root, timeout=60)
        if evidence:
            (evidence / 'dependencies.txt').write_text(dependencies.stdout + dependencies.stderr)
        self.assertEqual(dependencies.returncode, 0, dependencies.stdout + dependencies.stderr)
        consumed.add(Path(__file__).resolve())
        inputs = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(consumed)}
        if evidence:
            (evidence / 'dependencies.txt').write_text(dependencies.stdout + dependencies.stderr)
            (evidence / 'inputs.sha256.json').write_text(json.dumps(inputs, indent=2))
        with tempfile.TemporaryDirectory(prefix='evrm-surface-backend-', ignore_cleanup_errors=True) as directory:
            folder = Path(directory)
            # Fake loader accepts only SPIR-V headers. No shader executes and no
            # system Vulkan loader/device is loaded by this executable.
            for name in ('avatar_native.surface.vert', 'avatar_native.surface.gbuffer',
                         'avatar_native.surface.oit', 'native_prepare.surface.comp',
                         'native_menu_prepare.surface.comp', 'native_menu_prepare.comp', 'native_skinning.comp',
                         'native_skinning.finalize', 'native_skinning.surface.comp'):
                (folder / (name + '.spv')).write_bytes(struct.pack('<5I', 0x07230203, 0, 0, 0, 0))
            binary = folder / 'native_surface_backend.exe'
            command = [compiler, '-std=c++17', '-O2', '-static', '-Wall', '-Wextra', '-Werror',
                       '-Wno-subobject-linkage', '-DEVRM_NATIVE_BACKEND_TESTING', '-I' + headers,
                       str(root / 'tests/native_surface_backend.cpp'), *map(str, sources),
                       str(root / 'src/mouth_deformation.cpp'), '-lbcrypt', '-o', str(binary)]
            built = run_compiler(command, capture_output=True, text=True, timeout=300)
            if evidence:
                (evidence / 'compile.command.json').write_text(json.dumps(command, indent=2))
                (evidence / 'compile.txt').write_text(f'exit={built.returncode}\n' + built.stdout + built.stderr)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            binary_hash = hashlib.sha256(binary.read_bytes()).hexdigest()
            if evidence:
                (evidence / 'binary.sha256.txt').write_text(binary_hash + '\n')
            failures = []
            for case in ('render-surface', 'render-menu', 'background-surface', 'background-menu', 'adoption', 'cancel-failure', 'sharing', 'descriptors', 'no-map', 'initialize-bounds', 'skin-streams', 'frame-buffer-failure', 'frame-pool-failure'):
                self.assertEqual(hashlib.sha256(binary.read_bytes()).hexdigest(), binary_hash)
                env = os.environ.copy()
                env['ENSHROUDED_VRM_LOG'] = str(folder / (case + '.log'))
                result = subprocess.run([str(binary), case, str(folder)], capture_output=True,
                                        text=True, timeout=30, env=env)
                output = f'exit={result.returncode}\n' + result.stdout + result.stderr
                if evidence:
                    (evidence / (case + '.txt')).write_text(output)
                    log = folder / (case + '.log')
                    if log.exists():
                        (evidence / (case + '.log')).write_bytes(log.read_bytes())
                print(case + ': ' + output.strip())
                if result.returncode or 'PASS actual' not in result.stdout:
                    failures.append(case + ': ' + output)
            self.assertFalse(failures, '\n'.join(failures))
            after = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(consumed)}
            if evidence:
                (evidence / 'inputs.after.sha256.json').write_text(json.dumps(after, indent=2))
            self.assertEqual(inputs, after, 'Consumed input changed during build/run; recheck exact inputs')


if __name__ == '__main__':
    unittest.main()
