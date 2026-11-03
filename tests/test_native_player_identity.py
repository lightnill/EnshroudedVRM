"""Attested player ownership against owned CPU memory; no game or GPU access."""
from pathlib import Path
import os
import shutil
import subprocess
import tempfile
import unittest


class NativePlayerIdentityTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.root = Path(__file__).resolve().parents[1]
        cls.compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if os.name != 'nt' or not cls.compiler or not Path(cls.compiler).is_file():
            raise unittest.SkipTest('Windows GCC required')
        cls.folder = tempfile.TemporaryDirectory(prefix='evrm-player-identity-', ignore_cleanup_errors=True)
        cls.binary = Path(cls.folder.name) / 'native_player_identity.exe'
        command = [cls.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                   str(cls.root/'tests/native_player_identity.cpp'), str(cls.root/'src/native_owner.cpp'), '-o', str(cls.binary)]
        result = subprocess.run(command, capture_output=True, text=True, timeout=180)
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

    def test_two_and_sixteen_qualified_players(self):
        self.scenarios('two', 'sixteen')

    def test_account_connection_and_local_selection(self):
        self.scenarios('wrong-account', 'wrong-connection', 'wrong-local', 'wrong-slot', 'connection-slot', 'missing-selector')

    def test_duplicate_reused_inactive_and_overlapping_owners(self):
        self.scenarios('duplicate', 'reuse', 'inactive', 'overlap', 'missing-state')

    def test_exact_image_original_producer_and_named_binding(self):
        self.scenarios('unsupported', 'producer', 'service-binding')

    def test_fresh_submission_guards(self):
        self.scenarios('guard-account', 'guard-connection', 'guard-generation', 'guard-handle')

    def test_candidate_metadata_bound_to_scalar_guards(self):
        self.scenarios('candidate-account', 'candidate-connection', 'candidate-record', 'candidate-clear', 'alias-image')

    def test_single_total_batch_budget_and_disjoint_target_order(self):
        self.scenarios('batch-one', 'batch-two', 'batch-sixteen', 'batch-subset', 'batch-reverse', 'batch-empty', 'batch-overbound', 'batch-reuse', 'batch-stale', 'batch-duplicate', 'batch-deadline')

    def test_bulk_submission_revalidates_shared_guards_once(self):
        self.scenarios('batch-revalidate-valid', 'batch-revalidate-sixteen', 'batch-revalidate-account-memory',
                       'batch-revalidate-model-memory', 'batch-revalidate-model-metadata', 'batch-revalidate-record-metadata',
                       'batch-revalidate-entity-metadata', 'batch-revalidate-duplicate', 'batch-revalidate-heterogeneous',
                       'batch-revalidate-empty', 'batch-revalidate-deadline')

    def test_work_deadline_and_clock_regression(self):
        self.scenarios('deadline', 'regressed')


if __name__ == '__main__':
    unittest.main()
