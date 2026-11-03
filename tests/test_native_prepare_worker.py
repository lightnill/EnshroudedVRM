"""CPU-only bounded lazy compute worker; no Vulkan loader or GPU work."""
import ctypes
import hashlib
import json
import os
from pathlib import Path
import shlex
import shutil
import subprocess
import tempfile
import unittest


class NativePrepareWorkerTests(unittest.TestCase):
    def test_bounded_nonblocking_worker_and_ownership(self):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        headers = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if os.name != 'nt' or not compiler or not headers:
            self.skipTest('Windows GCC and explicit Vulkan headers required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode() | 0x8003)
        root = Path(__file__).resolve().parents[1]
        evidence = os.environ.get('EVRM_PREPARE_WORKER_EVIDENCE')
        evidence = Path(evidence) if evidence else None
        if evidence:
            evidence.mkdir(parents=True, exist_ok=True)
        common = [compiler, '-std=c++17', '-O2', '-static', '-Wall', '-Wextra', '-Werror',
                  '-I' + Path(headers).as_posix()]
        source = root / 'tests/native_prepare_worker.cpp'
        deps = subprocess.run([*common, '-MM', '-MT', 'dependencies', source.relative_to(root).as_posix()],
                              cwd=root, capture_output=True, text=True, timeout=30)
        if evidence:
            (evidence / 'dependencies.txt').write_text(deps.stdout + deps.stderr)
        self.assertEqual(deps.returncode, 0, deps.stdout + deps.stderr)
        consumed = {Path(__file__).resolve()}
        for row in deps.stdout.replace('\\\n', ' ').splitlines():
            for value in shlex.split(row.partition(': ')[2]):
                path = Path(value)
                consumed.add((root / path).resolve() if not path.is_absolute() else path.resolve())
        pins = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(consumed)}
        with tempfile.TemporaryDirectory(prefix='evrm-prepare-worker-', ignore_cleanup_errors=True) as directory:
            binary = Path(directory) / 'native_prepare_worker.exe'
            command = [*common, str(source), '-o', str(binary)]
            built = subprocess.run(command, capture_output=True, text=True, timeout=60)
            if evidence:
                (evidence / 'inputs.sha256.json').write_text(json.dumps(pins, indent=2))
                (evidence / 'compile.command.json').write_text(json.dumps(command, indent=2))
                (evidence / 'compile.txt').write_text(f'exit={built.returncode}\n' + built.stdout + built.stderr)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            if evidence:
                (evidence / 'binary.sha256.txt').write_text(hashlib.sha256(binary.read_bytes()).hexdigest() + '\n')
                (evidence / 'run.txt').write_text(f'exit={result.returncode}\n' + result.stdout + result.stderr)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertEqual(result.stdout.count('PASS worker'), 4, result.stdout)
            print(result.stdout.strip())
        after = {str(p): hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(consumed)}
        if evidence:
            (evidence / 'inputs.after.sha256.json').write_text(json.dumps(after, indent=2))
        self.assertEqual(pins, after, 'Consumed worker input changed during build/run')


if __name__ == '__main__':
    unittest.main()
