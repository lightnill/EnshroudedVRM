"""Attested player ownership against owned CPU memory; no game or GPU access."""
from native_fixture_compiler import default_compiler, run_compiler
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


class NativePlayerAvailableTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1]
        cls.compiler = default_compiler()
        if os.name != 'nt' or not cls.compiler or not Path(cls.compiler).is_file():
            raise unittest.SkipTest('Windows MSVC 14.44 required')
        cls.folder = tempfile.TemporaryDirectory(prefix='evrm-player-identity-', ignore_cleanup_errors=True)
        cls.binary = Path(cls.folder.name) / 'native_player_identity.exe'
        command = [cls.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                   str(cls.root/'tests/native_player_available.cpp'), str(cls.root/'src/native_owner.cpp'), '-o', str(cls.binary)]
        result = run_compiler(command, capture_output=True, text=True, timeout=180)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)

    @classmethod
    def tearDownClass(cls):
        if hasattr(cls, 'folder'):
            cls.folder.cleanup()

    def scenarios(self, *names):
        for name in names:
            with self.subTest(scenario=name):
                result = subprocess.run([str(self.binary), name], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                self.assertIn('PASS ', result.stdout)
                print(result.stdout.strip())

    def test_identity_only_roster_survives_absent_remote(self):
        self.scenarios('roster')

    def test_explicit_available_rows_and_shared_negative_proof(self):
        self.scenarios('absent', 'reverse', 'subset', 'sixteen', 'gone', 'recycled', 'negative-guard', 'spawn', 'strict', 'local-subset', 'sixteen-absent')

    def test_malformed_unknown_and_budget_failures_remain_hard(self):
        self.scenarios('missing-input', 'missing-equipment', 'state-stride', 'local-stride', 'remote-layout', 'local-header', 'local-pointer', 'probe-bound', 'absent-corrupt-third', 'duplicate-entity', 'duplicate-connection', 'bad-slot', 'two-local', 'duplicate', 'empty', 'overbound', 'overlap', 'local-absent', 'header', 'pointer', 'capacity', 'stride', 'component', 'zero-models', 'account-conflict', 'connection-conflict', 'unreadable', 'read-budget', 'byte-budget', 'deadline', 'regressed')

if __name__ == '__main__':
    unittest.main()
