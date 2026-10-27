"""Run the developer fixture builder without loading a game or graphics layer."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest


HELPER = Path(__file__).resolve().parent / 'build_script_fixture.ps1'
POWERSHELL = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'


class ScriptFixtureBuildTests(unittest.TestCase):
    def run_helper(self, compiler, output):
        return subprocess.run([str(POWERSHELL), '-NoLogo', '-NoProfile', '-File', str(HELPER),
                               '-Compiler', str(compiler), '-OutputDirectory', str(output)],
                              capture_output=True, text=True, encoding='utf-8', errors='replace', timeout=45)

    def test_missing_compiler_fails_before_creating_output(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            output = root / 'never-created'
            result = self.run_helper(root / 'missing-g++.exe', output)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn('Compiler not found', result.stdout + result.stderr)
            self.assertFalse(output.exists())

    @unittest.skipUnless(os.environ.get('EVRM_FIXTURE_COMPILER'), 'Set EVRM_FIXTURE_COMPILER to a local Windows x64 g++.exe')
    def test_build_creates_only_the_synthetic_child_executable(self):
        with tempfile.TemporaryDirectory() as folder:
            output = Path(folder) / 'fixture build'
            result = self.run_helper(os.environ['EVRM_FIXTURE_COMPILER'], output)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual({path.name for path in output.iterdir()}, {'portable_child.exe'})
            self.assertEqual((output / 'portable_child.exe').read_bytes()[:2], b'MZ')


if __name__ == '__main__':
    unittest.main()
