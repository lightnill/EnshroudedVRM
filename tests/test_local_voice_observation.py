"""Native outgoing-voice observation boundaries; synthetic PCM, never a game."""
import os
import json
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class LocalVoiceObservationTests(unittest.TestCase):
    def test_borrowed_native_buffer_after_successful_encoding(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler or os.name != 'nt':
            self.skipTest('Windows x64 GCC required')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-borrowed-') as folder:
            binary = Path(folder) / 'borrowed.exe'
            command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                       str(root / 'tests/local_voice_borrowed.cpp'),
                       *[str(root / 'src' / name) for name in ('local_voice_observation.cpp', 'local_voice_copy.cpp', 'voice_clock.cpp')],
                       '-Wl,--wrap=__imp_VirtualQuery', '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
            print(run.stdout.strip())
            fault = subprocess.run([str(binary), '--fault'], capture_output=True, text=True, timeout=30)
            self.assertEqual(fault.returncode & 0xffffffff, 0xc0000005, 'Original fault was hidden')

    def test_real_windows_interception_and_opt_in_admission(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler or os.name != 'nt':
            self.skipTest('Windows x64 GCC required')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-voice-hook-', ignore_cleanup_errors=True) as folder:
            folder = Path(folder)
            vendor = root / 'vendor/minhook'
            pins = json.loads((vendor / 'PROVENANCE.json').read_text())
            import hashlib
            for pin in pins['files']:
                self.assertEqual(hashlib.sha256((vendor / pin['path']).read_bytes()).hexdigest(), pin['sha256'])
            self.assertEqual((vendor / 'LICENSE.txt').read_bytes(), (root / 'portable/notices/MinHook-BSD.txt').read_bytes())
            objects = []
            for name in ('buffer', 'hook', 'trampoline', 'hde/hde64'):
                obj = folder / (Path(name).name + '.o')
                command = [str(Path(compiler).with_name('gcc.exe')), '-std=c11', '-O2', '-c',
                           str(vendor / ('src/' + name + '.c')), '-o', str(obj)]
                result = subprocess.run(command, capture_output=True, text=True, timeout=90)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                objects.append(str(obj))
            binary = folder / 'voice_hook.exe'
            command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                       str(root / 'tests/local_voice_observer.cpp'), str(root / 'src/local_voice_observer.cpp'),
                       str(root / 'src/local_voice_observation.cpp'), str(root / 'src/local_voice_copy.cpp'), *[str(root / 'src' / name) for name in
                           ('voice_clock.cpp', 'voice_stream.cpp', 'voice_features.cpp', 'voice_profiles.cpp',
                            'voice_analyser.cpp', 'voice_automatic.cpp', 'voice_worker.cpp', 'voice_profile_io.cpp', 'voice_setup.cpp')], *objects, '-o', str(binary)]
            result = subprocess.run(command, capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS real x64 hook', result.stdout)
            for marker_kind in ('absent', 'directory', 'regular'):
                with self.subTest(marker=marker_kind):
                    capture = folder / marker_kind
                    capture.mkdir()
                    marker = capture / 'voice-observer.request'
                    if marker_kind == 'directory':
                        marker.mkdir()
                    elif marker_kind == 'regular':
                        marker.write_bytes(b'')
                    log = capture / 'private.log'
                    env = {**os.environ, 'ENSHROUDED_VRM_CAPTURE_DIR': str(capture), 'ENSHROUDED_VRM_LOG': str(log)}
                    result = subprocess.run([str(binary), 'admission'], env=env, capture_output=True, text=True, timeout=30)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    if marker_kind == 'regular':
                        self.assertFalse(marker.exists())
                        lines = log.read_text().splitlines()
                        self.assertEqual(len(lines), 1, lines)
                        self.assertIn('voice-observer.declined reason=source-identity', lines[0])
                    else:
                        self.assertFalse(log.exists(), 'Unrequested observation must remain inactive')
                        self.assertEqual(marker.exists(), marker_kind == 'directory')
            log = folder / 'feature-request.log'
            env = {**os.environ, 'ENSHROUDED_VRM_CAPTURE_DIR': str(folder), 'ENSHROUDED_VRM_LOG': str(log)}
            result = subprocess.run([str(binary), 'feature-request'], env=env, capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            lines = log.read_text().splitlines()
            self.assertEqual(len(lines), 1, lines)
            self.assertIn('reason=source-identity', lines[0])

    def test_native_format_forwarding_and_bounded_diagnostics(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler or os.name != 'nt':
            self.skipTest('Set EVRM_TEST_COMPILER to a Windows x64 GCC compiler')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-local-voice-') as folder:
            binary = Path(folder) / 'local_voice_observation.exe'
            result = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                                     str(root / 'tests/local_voice_observation.cpp'),
                                     str(root / 'src/local_voice_observation.cpp'), str(root / 'src/local_voice_copy.cpp'), str(root / 'src/voice_clock.cpp'),
                                     '-Wl,--wrap=__imp_ReadProcessMemory', '-Wl,--wrap=__imp_VirtualQuery', '-o', str(binary)],
                                    capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS local voice format/forwarding/counters', result.stdout)
            print(result.stdout.strip())
            result = subprocess.run([str(binary), '--encoder-fault'], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode & 0xffffffff, 0xc0000005, 'Original encoder fault was contained or changed: ' + result.stdout + result.stderr)


if __name__ == '__main__':
    unittest.main()
