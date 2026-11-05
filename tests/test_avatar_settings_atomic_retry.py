"""Real Windows atomic settings rename denial; no game or external files."""
from native_fixture_compiler import default_compiler, run_compiler
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


@unittest.skipUnless(os.name == 'nt', 'Windows file replacement semantics required')
class AvatarSettingsAtomicRetryTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1]
        private = cls.root.parent / 'private/native-fixtures/avatar-settings-atomic-retry'
        private.mkdir(parents=True, exist_ok=True)
        cls.work = tempfile.TemporaryDirectory(prefix='native-', dir=private)
        cls.addClassCleanup(cls.work.cleanup)
        cls.binary = Path(cls.work.name) / 'atomic_retry.exe'
        built = run_compiler([
            default_compiler(), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', '-municode',
            str(cls.root / 'tests/avatar_settings_atomic_retry.cpp'), '-o', str(cls.binary),
        ], capture_output=True, text=True, timeout=90)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)

    def run_case(self, case):
        with tempfile.TemporaryDirectory(prefix=case + '-', dir=self.work.name) as folder:
            result = subprocess.run([str(self.binary), case, folder], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS atomic avatar settings retry case', result.stdout)
            print(case + ': ' + result.stdout.strip())

    def test_real_reader_denial_recovers_same_flushed_temporary(self):
        self.run_case('transient-reader')

    def test_permanent_denial_is_bounded_and_preserves_old_file(self):
        self.run_case('permanent-denial')

    def test_readonly_target_is_not_retried(self):
        self.run_case('readonly')

    def test_other_errors_fail_immediately(self):
        self.run_case('other-errors')

    def test_changed_destination_guard_prevents_retry(self):
        self.run_case('changed-path')

    def test_create_only_does_not_replace_existing_target(self):
        self.run_case('create-only')


if __name__ == '__main__':
    unittest.main()
