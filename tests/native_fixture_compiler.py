"""Explicit native-test entrypoint shared by unittest and direct script runs."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import time

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'portable'))
from native_compiler import DEFAULT_COMPILER, default_compiler, run_compiler


def native_dependencies(command, *, cwd=None, timeout):
    """Use MSVC's JSON include trace, preserving exact Windows filenames."""
    command = list(map(os.fspath, command))
    sources = [value for value in command[1:] if Path(value).suffix.lower() in ('.cpp', '.cc', '.cxx', '.c')]
    flags = [value for value in command[1:] if value not in sources]
    if not sources:
        raise ValueError('Dependency tracing requires native sources')
    consumed = {Path(__file__).resolve(), Path(__file__).resolve().parents[1] / 'portable/native_compiler.py'}
    stdout, stderr, returncode = '', '', 0
    deadline = time.monotonic() + timeout
    with tempfile.TemporaryDirectory(prefix='evrm-msvc-dependencies-') as temporary:
        folder = Path(temporary)
        for index, source in enumerate(sources):
            trace, obj = folder / f'{index}.json', folder / f'{index}.obj'
            result = run_compiler([command[0], *flags, '-c', source, '/sourceDependencies', str(trace), '-o', str(obj)],
                                  cwd=cwd, capture_output=True, text=True, timeout=max(0, deadline-time.monotonic()))
            stdout += result.stdout
            stderr += result.stderr
            returncode = result.returncode
            if returncode:
                break
            data = json.loads(trace.read_text(encoding='utf-8-sig'))['Data']
            consumed.add(Path(data['Source']).resolve())
            consumed.update(Path(value).resolve() for value in data['Includes'])
    return subprocess.CompletedProcess(command, returncode, stdout, stderr), consumed


__all__ = ['DEFAULT_COMPILER', 'default_compiler', 'run_compiler', 'native_dependencies']
