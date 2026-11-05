"""Real MSVC build and integer-reference boundary checks for clock scaling."""
import ctypes
import os
from pathlib import Path
import random
import subprocess
import tempfile
import unittest
from native_fixture_compiler import default_compiler, run_compiler


class MsvcClockScalingTests(unittest.TestCase):
    def test_saturating_scaling_matches_unbounded_integer_reference(self):
        self.assertEqual(os.name, 'nt', 'MSVC port verification requires Windows')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode() | 0x8003)
        compiler = default_compiler()
        self.assertEqual(Path(compiler).name.lower(), 'cl.exe', 'Set the pinned MSVC compiler')
        self.assertIn('14.44.', compiler, 'Verify the requested 14.44 toolset')
        root = Path(__file__).resolve().parents[1]
        maximum = (1 << 64) - 1
        cases = [(0, 0, 0), (9, 8, 1), (0, 1, 1), (0, maximum, 1),
                 (0, maximum, maximum), (maximum - 1, maximum, 3),
                 (0, maximum, 1_000_000_000), (0, maximum, 999_999_999),
                 (0, maximum, 1_000_000_001), (0, 1 << 63, 1 << 63)]
        generator = random.Random(1444)
        for _ in range(4000):
            start = generator.getrandbits(64)
            end = generator.randrange(start, maximum + 1)
            frequency = generator.choice([0, 1, 3, 1000, 10_000_000,
                                          1_000_000_000, generator.getrandbits(64)])
            cases.append((start, end, frequency))
        for bit in range(64):
            value = 1 << bit
            for adjustment in (-1, 0, 1):
                frequency = max(1, value + adjustment)
                cases.append((0, maximum, min(frequency, maximum)))
        expected = [0 if end < start or not frequency else
                    min(maximum, (end - start) * 1_000_000_000 // frequency)
                    for start, end, frequency in cases]
        with tempfile.TemporaryDirectory(prefix='evrm-msvc-clock-') as directory:
            folder = Path(directory)
            executable = folder / 'clock.exe'
            built = run_compiler([compiler, '/nologo', '/std:c++17', '/EHsc', '/MT',
                                    '/O2', '/W4', '/WX', '/Fe' + str(executable),
                                    str(root / 'tests/msvc_clock_scaling.cpp'),
                                    str(root / 'src/voice_clock.cpp')], cwd=folder,
                                   capture_output=True, text=True, timeout=180)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(executable)],
                                    input=''.join(f'{start} {end} {frequency}\n'
                                                  for start, end, frequency in cases),
                                    capture_output=True, text=True, timeout=30)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            actual = [int(row) for row in result.stdout.splitlines()]
            self.assertEqual(actual, expected)
            print(f'PASS MSVC clock scaling: {len(cases)} integer-reference cases')


if __name__ == '__main__':
    unittest.main()
