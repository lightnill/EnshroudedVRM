"""Real owner-filtered bridge fixtures with certified partial observations only."""
from pathlib import Path
import ctypes, os, shutil, subprocess, tempfile, unittest


class OwnerPartialPaletteTests(unittest.TestCase):
    def test_actual_certified_observations_and_rejections(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        headers = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if os.name != 'nt' or not compiler or not headers:
            self.skipTest('Windows GCC and explicit Vulkan headers required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode() | 0x8003)
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-partial-palette-', ignore_cleanup_errors=True) as directory:
            folder = Path(directory); binary = folder / 'partial-palette.exe'
            built = subprocess.run([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                                    '-I' + headers, str(root / 'tests/owner_partial_palette.cpp'),
                                    str(root / 'src/enshrouded_bridge.cpp'), '-o', str(binary)],
                                   capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            cases = ('short', 'long', 'bad-tail', 'uncertified', 'ownerless', 'accountless', 'bad-connection', 'zero-connection', 'out-of-roster',
                     'wrong-source', 'wrong-epoch', 'preview-owner', 'bad-handle', 'unowned', 'undrawn', 'truncated-prefix',
                     'missing-core', 'duplicate-joint', 'two-groups', 'guid-agree', 'guid-disagree',
                     'bad-policy', 'extra-field', 'wrong-prefix', 'wrong-type', 'nonprefix',
                     'policy-on-independent', 'policy-flag-false', 'auxiliary', 'auxiliary-ambiguity', 'mapped-tail',
                     'overlap', 'rejecting-overlap', 'independent-reject', 'full-short', 'full-long', 'full-bad-tail')
            for case in cases:
                with self.subTest(case=case):
                    target = folder / case; target.mkdir()
                    ran = subprocess.run([str(binary), str(target), case], capture_output=True, text=True, timeout=30)
                    self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
                    print(ran.stdout.strip())


if __name__ == '__main__':
    unittest.main()
