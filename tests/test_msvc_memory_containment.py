"""Real MSVC x64 copy faults and private-destination boundaries; no game."""
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'portable'))
from native_compiler import DEFAULT_COMPILER, run_compiler


@unittest.skipUnless(os.name == 'nt', 'Windows x64 structured exceptions required')
class MsvcMemoryContainmentTests(unittest.TestCase):
    def test_scoped_faults_and_full_source_before_visual_output(self):
        private = ROOT.parent / 'private/msvc-memory'
        private.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='containment-', dir=private) as folder:
            binary = Path(folder) / 'containment.exe'
            environment = {**os.environ, 'EVRM_NATIVE_BUILD_EVIDENCE': str(private / 'compiler-invocations.jsonl')}
            built = run_compiler([
                DEFAULT_COMPILER, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                '-Wl,--wrap=__imp_VirtualQuery', str(ROOT / 'tests/msvc_memory_containment.cpp'),
                *[str(ROOT / 'src' / name) for name in
                  ('local_voice_copy.cpp', 'native_visual_slot_write.cpp', 'voice_clock.cpp')],
                '-lpsapi', '-lonecore', '-o', str(binary),
            ], env=environment, capture_output=True, text=True, timeout=90)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS MSVC scoped memory containment:', result.stdout)
            print(result.stdout.strip())


if __name__ == '__main__':
    unittest.main()
