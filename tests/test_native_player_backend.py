"""Actual backend coordinator with fake Vulkan and synthetic owned memory.

No game, real graphics device, hooks, uploads, or external process reads.
"""
from pathlib import Path
import ctypes
import os
import shutil
import subprocess
import tempfile
import unittest

class NativePlayerBackendTests(unittest.TestCase):
    def test_owned_backend_coordinator(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        headers = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        dxc = os.environ.get('EVRM_SHADER_COMPILER') or shutil.which('dxc')
        if os.name != 'nt' or not compiler or not headers or not dxc:
            self.skipTest('Windows GCC, explicit Vulkan headers and DXC required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode() | 0x8003)
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-player-backend-', ignore_cleanup_errors=True) as directory:
            folder = Path(directory)
            shader = folder / 'native_player_commit.comp.spv'
            built = subprocess.run([dxc, '-spirv', '-fspv-target-env=vulkan1.1', '-WX',
                                    '-T', 'cs_6_0', '-E', 'main', '-Fo', str(shader),
                                    str(root / 'shaders/native_player_commit.hlsl')],
                                   capture_output=True, text=True, timeout=60)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            binary = folder / 'native_player_backend.exe'
            # The backend itself is included by the fixture for inspection;
            # external mutation/hook boundaries are explicitly inert stubs.
            sources = [p for p in sorted((root / 'src').glob('native*.cpp'))
                       if p.name not in ('native_backend.cpp', 'native_visual_slot_write.cpp')]
            command = [compiler, '-std=c++17', '-O2', '-static', '-Wall', '-Wextra', '-Werror',
                       '-Wno-subobject-linkage', '-DEVRM_NATIVE_BACKEND_TESTING', '-I' + headers,
                       str(root / 'tests/native_player_backend.cpp'),
                       *[str(p) for p in sources], str(root / 'src/mouth_deformation.cpp'),
                       '-lbcrypt', '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=300)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            env = os.environ.copy()
            log = folder / 'backend.log'
            env['ENSHROUDED_VRM_LOG'] = str(log)
            result = subprocess.run([str(binary), str(folder)], capture_output=True,
                                    text=True, timeout=60, env=env)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS actual before_submit', result.stdout)
            text = log.read_text() if log.exists() else ''
            self.assertNotIn('fixture-remote', text)
            self.assertNotIn('fixture-shared', text)
            self.assertNotIn('fixture-upload', text)
            self.assertNotIn('PRIVATE-DIAGNOSTIC-TOKEN', text)
            diagnostic_case = text.split('fixture.completion-preparation-begin', 1)
            self.assertEqual(len(diagnostic_case), 2, 'Completion fixture marker missing')
            rows = [line for line in diagnostic_case[1].splitlines() if 'avatar-fallback.player-preparation ' in line]
            self.assertEqual(len(rows), 2, 'Completed common-gate failures need per-side preparation counts, once per group')
            expected = (
                {'subjects': '2', 'local-prepared': '1', 'remote-prepared': '0', 'local-unprepared': '0',
                 'remote-unprepared': '1', 'unavailable': '0', 'oit-near-plane': '1', 'oit-other': '0', 'before-oit': '0'},
                {'subjects': '2', 'local-prepared': '1', 'remote-prepared': '0', 'local-unprepared': '0',
                 'remote-unprepared': '0', 'unavailable': '1', 'oit-near-plane': '0', 'oit-other': '0', 'before-oit': '0'},
            )
            for row, wanted in zip(rows, expected):
                values = dict(token.split('=', 1) for token in row.split() if '=' in token)
                self.assertEqual({key: values.get(key) for key in wanted}, wanted)
            owner_case = text.split('fixture.owner-observation-begin', 1)
            self.assertEqual(len(owner_case), 2, 'Owner observation fixture marker missing')
            owner_case = owner_case[1].split('fixture.owner-observation-end', 1)[0]
            owner_rows = [line for line in owner_case.splitlines() if 'avatar-fallback.player-owner-' in line]
            self.assertEqual(len(owner_rows), 2, 'Three quick capture failures need one diagnostic and one recovery')
            for row, event, wanted in zip(owner_rows, ('player-owner-gap', 'player-owner-recovered'), (
                {'stage': '1', 'requested': '1', 'selected': '0', 'local-requested': '1', 'local-selected': '0',
                 'owner-phase': '5', 'owner-failure': '1', 'detail-code': '2'},
                {'stage': '0', 'requested': '1', 'selected': '1', 'local-requested': '1', 'local-selected': '1'},
            )):
                self.assertIn('avatar-fallback.' + event + ' ', row)
                values = dict(token.split('=', 1) for token in row.split() if '=' in token)
                self.assertEqual({key: values.get(key) for key in wanted}, wanted)
            self.assertNotIn('4027383809', owner_case, 'Private account value leaked')
            self.assertNotIn('f00d0001', owner_case.lower(), 'Private account hex leaked')
            print(result.stdout.strip())

if __name__ == '__main__':
    unittest.main()
