"""Pinned x64 MSVC runner for production builds and legacy native fixtures.

The adapter translates only reviewed legacy options. Each translation unit has
its own object file, including sources with identical basenames. C files retain
the old g++ C++ semantics unless C mode is explicitly selected. No GCC fallback
or process-wide subprocess patching is used.
"""
from __future__ import annotations

import ctypes
from ctypes import wintypes
from functools import lru_cache
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

MSVC_ROOT = Path(r'C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Tools\MSVC\14.44.35207')
SDK_ROOT = Path(r'C:\Program Files (x86)\Windows Kits\10')
SDK_VERSION = '10.0.28000.0'
DEFAULT_COMPILER = str(MSVC_ROOT / 'bin/Hostx64/x64/cl.exe')
COMPILER_VERSION = '19.44.35229.0'
DEFAULT_EVIDENCE = Path(__file__).resolve().parents[2] / 'private/native-fixtures/native-compiler-invocations.jsonl'


def default_compiler():
    """An explicit override must still identify the approved compiler."""
    return os.environ.get('EVRM_TEST_COMPILER') or DEFAULT_COMPILER


def _version(path):
    version = ctypes.WinDLL('version', use_last_error=True)
    version.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    version.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    version.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    version.VerQueryValueW.argtypes = [ctypes.c_void_p, wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_void_p), ctypes.POINTER(wintypes.UINT)]
    size = version.GetFileVersionInfoSizeW(str(path), None)
    data = ctypes.create_string_buffer(size)
    if not size or not version.GetFileVersionInfoW(str(path), 0, size, data):
        raise OSError(f'Cannot read tool version: {path}')
    pointer = ctypes.c_void_p()
    length = wintypes.UINT()
    if not version.VerQueryValueW(data, '\\', ctypes.byref(pointer), ctypes.byref(length)):
        raise OSError(f'Cannot query tool version: {path}')
    # VS_FIXEDFILEINFO: signature, structure version, file-version MS, LS.
    words = ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD))
    return '.'.join(map(str, (words[2] >> 16, words[2] & 65535, words[3] >> 16, words[3] & 65535)))


@lru_cache(maxsize=1)
def toolchain_identity():
    if os.name != 'nt':
        raise OSError('The pinned MSVC toolchain requires Windows')
    result = {'toolset': '14.44.35207', 'sdk_version': SDK_VERSION}
    for name in ('compiler', 'linker'):
        path = Path(DEFAULT_COMPILER) if name == 'compiler' else Path(DEFAULT_COMPILER).with_name('link.exe')
        result[name] = {'path': str(path), 'version': _version(path),
                        'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    if result['compiler']['version'] != COMPILER_VERSION:
        raise ValueError(f'Expected pinned MSVC {COMPILER_VERSION}, got {result["compiler"]["version"]}')
    return result


def prepare_msvc_environment(compiler: str, base_environment=None) -> dict:
    compiler = Path(compiler)
    if not compiler.is_absolute() or compiler.resolve() != Path(DEFAULT_COMPILER).resolve():
        raise ValueError(f'Use the pinned x64 MSVC compiler: {DEFAULT_COMPILER}')
    toolchain_identity()
    includes = [MSVC_ROOT / 'include', *[SDK_ROOT / 'Include' / SDK_VERSION / name
                                      for name in ('ucrt', 'shared', 'um', 'winrt', 'cppwinrt')]]
    libraries = [MSVC_ROOT / 'lib/x64', SDK_ROOT / 'Lib' / SDK_VERSION / 'ucrt/x64',
                 SDK_ROOT / 'Lib' / SDK_VERSION / 'um/x64']
    bins = [compiler.parent, SDK_ROOT / 'bin' / SDK_VERSION / 'x64']
    for path in [*includes, *libraries, *bins]:
        if not path.is_dir():
            raise FileNotFoundError(f'Pinned MSVC/SDK directory missing: {path}')
    env = dict(os.environ if base_environment is None else base_environment)
    # Environment keys are case-insensitive on Windows; remove every spelling.
    inherited_path = next((str(value) for key, value in env.items() if key.upper() == 'PATH'), '')
    for key in list(env):
        if key.upper() in ('PATH', 'INCLUDE', 'LIB', 'LIBPATH', 'CL', '_CL_', 'LINK', '_LINK_'):
            del env[key]
    env['PATH'] = ';'.join(map(str, bins)) + (';' + inherited_path if inherited_path else '')
    env['INCLUDE'] = ';'.join(map(str, includes))
    env['LIB'] = ';'.join(map(str, libraries))
    env['LIBPATH'] = str(MSVC_ROOT / 'lib/x64')
    return env


def _record(env, command, cwd, returncode=None, **extra):
    destination = Path(env.get('EVRM_NATIVE_BUILD_EVIDENCE') or DEFAULT_EVIDENCE)
    destination.parent.mkdir(parents=True, exist_ok=True)
    record = {'event': 'invocation', 'time_ns': time.time_ns(), 'pid': os.getpid(),
              'identity': toolchain_identity(), 'command': list(map(str, command)),
              'cwd': str(Path(cwd or Path.cwd()).resolve()), 'returncode': returncode, **extra}
    # One append write keeps concurrent fixture receipts separate without an
    # environment dump (which can contain user secrets).
    with destination.open('a', encoding='utf-8') as stream:
        stream.write(json.dumps(record, ensure_ascii=False) + '\n')


def _unsupported(option):
    raise ValueError(f'Unsupported native compiler option: {option}')


def _supported_linker_option(option):
    # LINK can warn about an unknown switch and still emit a successful binary.
    # Validate complete options here; recognized prefixes are insufficient.
    normalized = option.upper()
    if normalized in {'/NOLOGO', '/MACHINE:X64', '/INCREMENTAL:NO', '/DLL',
                      '/DEBUG', '/DEBUG:FULL', '/MAP', '/NXCOMPAT',
                      '/DYNAMICBASE', '/HIGHENTROPYVA', '/BREPRO',
                      '/OPT:REF', '/OPT:ICF', '/GUARD:CF'}:
        return True
    name, separator, value = normalized.partition(':')
    if not separator or not value or any(ord(char) < 32 for char in value):
        return False
    if name in {'/PDB', '/PDBALTPATH', '/MAP', '/IMPLIB', '/DEF', '/OUT',
                '/LIBPATH', '/ENTRY', '/EXPORT', '/NODEFAULTLIB'}:
        return True
    number = r'(?:0X[0-9A-F]+|[0-9]+)'
    if name in {'/BASE', '/STACK'}:
        return re.fullmatch(number + r'(?:,' + number + r')?', value) is not None
    if name == '/VERSION':
        return re.fullmatch(r'[0-9]+(?:\.[0-9]+)?', value) is not None
    if name == '/SUBSYSTEM':
        return re.fullmatch(r'(?:CONSOLE|WINDOWS)(?:,[0-9]+(?:\.[0-9]+)?)?', value) is not None
    return False


def _translate(arguments):
    compiler_flags = ['/nologo', '/utf-8', '/EHsc', '/MT', '/std:c++17', '/O2',
                      '/D_CRT_SECURE_NO_WARNINGS']
    linker_flags = ['/NOLOGO', '/MACHINE:X64', '/INCREMENTAL:NO']
    sources, inputs = [], []
    output, compile_only, preprocess, language, unicode = None, False, False, 'c++', False
    iterator = iter(map(os.fspath, arguments))
    for argument in iterator:
        if argument in ('-o', '/Fe', '/Fo'):
            output = next(iterator)
        elif argument.startswith(('/Fe', '/Fo')) and len(argument) > 3:
            output = argument[3:]
        elif argument in ('-c', '/c'):
            compile_only = True
        elif argument in ('-E', '/E', '/EP'):
            preprocess = True
        elif argument == '-P':
            compiler_flags.append('/EP')
        elif argument == '-x':
            language = next(iterator)
            if language not in ('c', 'c++'):
                _unsupported('-x ' + language)
        elif argument in ('-std=c11', '/std:c11'):
            language = 'c'
            compiler_flags = [flag for flag in compiler_flags if not flag.startswith('/std:')]
            compiler_flags.append('/std:c11')
        elif argument in ('-std=c++17', '/std:c++17'):
            pass
        elif argument in ('-O2', '/O2', '-static', '/MT', '/nologo', '/EHsc', '/utf-8'):
            pass
        elif argument in ('-O0', '/Od'):
            compiler_flags = [flag for flag in compiler_flags if flag != '/O2']
            compiler_flags.append('/Od')
        elif argument in ('-Wall', '-Wextra'):
            if '/W4' not in compiler_flags:
                compiler_flags.append('/W4')
                # The existing GCC -Wall/-Wextra policy did not enable
                # -Wshadow. Keep that policy when translating legacy flags;
                # native /W4 requests retain their full MSVC diagnostics.
                compiler_flags.extend(['/wd4456', '/wd4457', '/wd4458', '/wd4459'])
        elif argument == '-Werror':
            compiler_flags.append('/WX')
        elif argument == '-pedantic':
            compiler_flags.append('/permissive-')
        elif argument == '-Wno-subobject-linkage':
            # GCC's anonymous-type linkage diagnostic has no MSVC equivalent.
            # /W4 /WX remain active; no MSVC warning category is disabled.
            pass
        elif argument in ('-ffunction-sections', '-fdata-sections'):
            compiler_flags.append('/Gy' if argument == '-ffunction-sections' else '/Gw')
        elif argument == '-fstack-protector-strong':
            compiler_flags.append('/GS')
        elif argument in ('-include', '/FI'):
            compiler_flags.append('/FI' + next(iterator))
        elif argument.startswith('/FI'):
            compiler_flags.append(argument)
        elif argument in ('-municode', '-mwindows'):
            if argument == '-municode':
                unicode = True
                compiler_flags += ['/DUNICODE', '/D_UNICODE']
            else:
                linker_flags.append('/SUBSYSTEM:WINDOWS')
        elif argument in ('-shared', '/LD'):
            linker_flags.append('/DLL')
        elif argument.startswith('/d1trimfile:'):
            prefix = argument.partition(':')[2]
            if not Path(prefix).is_absolute() or not prefix.endswith(('\\', '/')) or any(ord(char) < 32 or char == '"' for char in prefix):
                _unsupported(argument)
            compiler_flags.append(argument)
        elif argument in ('-I', '-D', '-U', '/I', '/D', '/U'):
            compiler_flags.append('/' + argument[1:] + next(iterator))
        elif argument.startswith(('-I', '-D', '-U')):
            compiler_flags.append('/' + argument[1:])
        elif argument.startswith(('/I', '/D', '/U')) and not argument.upper().startswith(('/DLL', '/DEBUG', '/DYNAMICBASE')):
            compiler_flags.append(argument)
        elif argument.startswith('-L'):
            linker_flags.append('/LIBPATH:' + (argument[2:] or next(iterator)))
        elif argument.startswith('-l'):
            library = argument[2:]
            if not re.fullmatch(r'[A-Za-z0-9_.-]+', library):
                _unsupported(argument)
            inputs.append(library + '.lib')
        elif argument.startswith('-Wl,'):
            options = argument[4:].split(',')
            index = 0
            while index < len(options):
                option = options[index]
                translations = {'--nxcompat': '/NXCOMPAT', '--dynamicbase': '/DYNAMICBASE',
                                '--high-entropy-va': '/HIGHENTROPYVA', '--no-insert-timestamp': '/Brepro',
                                '--gc-sections': '/OPT:REF'}
                if option in translations:
                    linker_flags.append(translations[option])
                elif option in ('--out-implib', '--image-base') and index + 1 < len(options):
                    index += 1
                    linker_flags.append(('/IMPLIB:' if option == '--out-implib' else '/BASE:') + options[index])
                elif option.startswith('--wrap=__imp_'):
                    target = option[len('--wrap=__imp_'):]
                    if target == 'VirtualQuery':
                        compiler_flags.append('/DEVRM_TEST_WRAP_VirtualQuery')
                    elif target == 'ReadProcessMemory':
                        # The fixture defines the actual IAT pointer on MSVC,
                        # preserving its assertion against generic process reads.
                        compiler_flags.append('/D__wrap___imp_ReadProcessMemory=__imp_ReadProcessMemory')
                    else:
                        _unsupported(argument)
                else:
                    _unsupported(argument)
                index += 1
        elif argument.lower() == '/link':
            for option in iterator:
                if not _supported_linker_option(option):
                    _unsupported(option)
                linker_flags.append(option)
        elif argument in ('/TP', '/TC'):
            language = 'c++' if argument == '/TP' else 'c'
        elif re.fullmatch(r'/(?:W[0-4]|WX-?|wd[0-9]+|we[0-9]+|Z7|Zi|Gy|Gw|GS|Brepro|permissive-|Zc:[A-Za-z0-9,+-]+|guard:cf|EHsc|utf-8)', argument):
            compiler_flags.append(argument)
        elif argument == '/sourceDependencies':
            compiler_flags.extend([argument, next(iterator)])
        elif argument.startswith('/Fd'):
            compiler_flags.append(argument)
        elif Path(argument).suffix.lower() in ('.cpp', '.cc', '.cxx', '.c', '.hlsl'):
            sources.append(argument)
        elif Path(argument).suffix.lower() == '.def':
            linker_flags.append('/DEF:' + argument)
        elif Path(argument).suffix.lower() in ('.obj', '.o', '.res', '.lib', '.a'):
            inputs.append(argument)
        else:
            _unsupported(argument)
    if any(flag.startswith('/d1trimfile:') for flag in compiler_flags) and '/Brepro' not in compiler_flags:
        raise ValueError('Source-root trimming requires the verified /Brepro option')
    if unicode and not any(flag.startswith('/SUBSYSTEM:') for flag in linker_flags) and '/DLL' not in linker_flags:
        linker_flags.append('/SUBSYSTEM:CONSOLE')
    return compiler_flags, linker_flags, sources, inputs, output, compile_only, preprocess, language


def run_compiler(arguments, **kwargs):
    """Run the pinned compiler/linker and retain subprocess.run error semantics."""
    arguments = list(map(os.fspath, arguments))
    if not arguments:
        raise ValueError('A pinned compiler argument is required')
    if kwargs.get('shell'):
        raise ValueError('Native compiler execution does not support shell=True')
    env = prepare_msvc_environment(arguments[0], kwargs.pop('env', None))
    compiler_flags, linker_flags, sources, inputs, output, compile_only, preprocess, language = _translate(arguments[1:])
    if not sources and (compile_only or preprocess):
        raise ValueError('Native compiler source argument is required')
    if compile_only and len(sources) != 1:
        raise ValueError('Compile-only calls require one source and one unique output')
    if not output and not preprocess:
        raise ValueError('Native compiler calls require an explicit output path')
    checked = kwargs.pop('check', False)
    timeout = kwargs.pop('timeout', None)
    deadline = time.monotonic() + timeout if timeout is not None else None
    text_mode = bool(kwargs.get('text') or kwargs.get('universal_newlines') or kwargs.get('encoding') or kwargs.get('errors'))
    collected_stdout = '' if text_mode else b''
    collected_stderr = '' if text_mode else b''
    result = None

    def invoke(command):
        nonlocal collected_stdout, collected_stderr, result
        remaining = max(0, deadline - time.monotonic()) if deadline is not None else None
        try:
            part = subprocess.run(command, env=env, timeout=remaining, **kwargs)
        except subprocess.TimeoutExpired:
            _record(env, command, kwargs.get('cwd'), timed_out=True)
            raise
        _record(env, command, kwargs.get('cwd'), returncode=part.returncode)
        if part.stdout is not None:
            collected_stdout += part.stdout
        if part.stderr is not None:
            collected_stderr += part.stderr
        result = subprocess.CompletedProcess(arguments, part.returncode,
                                             collected_stdout if part.stdout is not None else None,
                                             collected_stderr if part.stderr is not None else None)
        return part.returncode == 0

    with tempfile.TemporaryDirectory(prefix='evrm-msvc-objects-') as scratch:
        objects = []
        if preprocess:
            if len(sources) != 1 or inputs:
                raise ValueError('Preprocessing requires exactly one source')
            flags = [flag for flag in compiler_flags if flag not in ('/O2', '/EHsc')]
            if '/EP' not in flags:
                flags.append('/E')
            invoke([arguments[0], *flags, '/TP' if language == 'c++' else '/TC', sources[0]])
        else:
            for index, source in enumerate(sources):
                obj = output if compile_only else str(Path(scratch) / f'{index:04d}-{Path(source).stem}.obj')
                flags = [flag for flag in compiler_flags if language != 'c' or flag != '/EHsc']
                if not invoke([arguments[0], *flags, '/TP' if language == 'c++' else '/TC', '/c', source, '/Fo' + obj,
                               '/Fd' + str(Path(scratch) / f'{index:04d}.pdb')]):
                    break
                objects.append(obj)
            else:
                if not compile_only:
                    invoke([str(Path(arguments[0]).with_name('link.exe')), *linker_flags, '/OUT:' + output,
                            *objects, *inputs])
    if result is None:
        raise ValueError('Native compiler call did not contain compilable or linkable inputs')
    if checked:
        result.check_returncode()
    return result


def main(argv=None):
    arguments = sys.argv[1:] if argv is None else argv
    try:
        return run_compiler(arguments).returncode
    except (OSError, ValueError, StopIteration) as error:
        print(f'native compiler: {error}', file=sys.stderr)
        return 2


if __name__ == '__main__':
    sys.exit(main())
