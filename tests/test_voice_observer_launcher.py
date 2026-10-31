"""Per-run request cleanup, using a rejecting launcher fixture and no game."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest


class VoiceObserverLauncherTests(unittest.TestCase):
    def test_request_never_outlives_launcher_exit(self):
        if os.name != 'nt':
            self.skipTest('Windows PowerShell fixture')
        powershell = Path(os.environ['SystemRoot']) / 'System32/WindowsPowerShell/v1.0/powershell.exe'
        template = Path(__file__).resolve().parents[1] / 'portable/development/Play-voice-test.ps1'
        for code in (37, 0):
            with self.subTest(exit_code=code), tempfile.TemporaryDirectory(prefix='evrm-voice-launch-') as folder:
                root = Path(folder)
                shutil.copyfile(template, root / 'Play-voice-test.ps1')
                candidate = root / 'live-candidate/EnshroudedVRM-voice-observer-r3'
                candidate.mkdir(parents=True)
                (candidate / 'Launch-EnshroudedVRM.ps1').write_text(
                    "param([switch]$Play)\nif (-not $Play) { exit 91 }\nexit " + str(code) + '\n')
                result = subprocess.run([str(powershell), '-NoLogo', '-NoProfile', '-File',
                    str(root / 'Play-voice-test.ps1')], capture_output=True, text=True, timeout=30)
                self.assertEqual(result.returncode, code, result.stdout + result.stderr)
                self.assertFalse((candidate / 'State/Cache/voice-observer.request').exists(),
                                 'A later ordinary launch must not consume a stale request')


if __name__ == '__main__':
    unittest.main()
