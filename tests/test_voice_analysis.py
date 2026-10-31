"""Native deterministic analysis/stream fixtures; never records or opens a game."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from native_voice_fixture import stage_voice_library


class VoiceAnalysisTests(unittest.TestCase):
    def run_native(self, name, sources):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler or os.name != 'nt':
            self.skipTest('Windows x64 GCC required')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-voice-analysis-', ignore_cleanup_errors=True) as folder:
            if 'voice_analyser.cpp' in sources:
                stage_voice_library(folder)
            binary = Path(folder) / (name + '.exe')
            command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                       str(root / ('tests/' + name + '.cpp'))]
            command.extend(str(root / ('src/' + source)) for source in sources)
            if name == 'voice_capture_generation':
                command.append('-Wl,--wrap=__imp_VirtualQuery')
            result = subprocess.run(command + ['-o', str(binary)], capture_output=True, text=True, timeout=90)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=40)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS ', result.stdout)
            print(result.stdout.strip())

    def test_bounded_stream_clock_and_snapshots(self):
        self.run_native('voice_stream', ('voice_stream.cpp', 'voice_clock.cpp'))

    def test_features_and_profiles(self):
        self.run_native('voice_features', ('voice_features.cpp', 'voice_profiles.cpp'))

    def test_vowels_neutral_resets_and_budget(self):
        self.run_native('voice_analyser', ('voice_analyser.cpp', 'voice_automatic.cpp', 'voice_features.cpp', 'voice_profiles.cpp', 'voice_clock.cpp', 'mouth_deformation.cpp'))

    def test_worker_lifetime_epochs_and_freshness(self):
        self.run_native('voice_worker', ('voice_worker.cpp', 'voice_analyser.cpp', 'voice_automatic.cpp', 'voice_features.cpp', 'voice_profiles.cpp', 'voice_stream.cpp', 'voice_clock.cpp'))

    def test_optional_profile_input(self):
        self.run_native('voice_profile_io', ('voice_profile_io.cpp', 'voice_profiles.cpp', 'voice_features.cpp'))

    def test_capture_generation_precedes_copy(self):
        self.run_native('voice_capture_generation', ('local_voice_observation.cpp', 'local_voice_copy.cpp', 'voice_worker.cpp', 'voice_analyser.cpp', 'voice_automatic.cpp',
                        'voice_features.cpp', 'voice_profiles.cpp', 'voice_stream.cpp', 'voice_clock.cpp'))


if __name__ == '__main__':
    unittest.main()
