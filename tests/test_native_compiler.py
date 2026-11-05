"""Real pinned-MSVC adapter regressions; no game, GPU, or external service."""
from pathlib import Path
import json
import os
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'portable'))
from native_compiler import DEFAULT_COMPILER, prepare_msvc_environment, run_compiler


@unittest.skipUnless(os.name == 'nt', 'The pinned MSVC toolchain is Windows-only')
class NativeCompilerTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix='evrm-msvc-adapter-')
        self.addCleanup(self.temporary.cleanup)
        self.folder = Path(self.temporary.name)

    def source(self, name, text):
        target = self.folder / name
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_text(text, encoding='utf-8')
        return target

    def build(self, *arguments, **kwargs):
        return run_compiler([DEFAULT_COMPILER, *map(str, arguments)],
                            capture_output=True, text=True, timeout=60, **kwargs)

    def test_pinned_environment_overrides_inherited_compiler_controls(self):
        env = prepare_msvc_environment(DEFAULT_COMPILER, {
            'PATH': os.environ['PATH'], 'CL': '/MD /DPOISON', '_CL_': '/MD',
            'LINK': '/DYNAMICBASE:NO', '_LINK_': '/DEBUG', 'INCLUDE': 'wrong',
            'LIB': 'wrong', 'KEEP_FIXTURE_VALUE': 'yes'})
        self.assertEqual(env['KEEP_FIXTURE_VALUE'], 'yes')
        self.assertTrue(env['PATH'].startswith(str(Path(DEFAULT_COMPILER).parent)))
        self.assertIn('14.44.35207', env['INCLUDE'])
        self.assertIn('10.0.28000.0', env['LIB'])
        for key in ('CL', '_CL_', 'LINK', '_LINK_'):
            self.assertNotIn(key, env)
        with self.assertRaisesRegex(ValueError, 'pinned'):
            prepare_msvc_environment(str(self.folder / 'g++.exe'))

    def test_compile_execute_and_record_actual_microsoft_tools(self):
        source = self.source('hello.cpp', '#include <windows.h>\n#include <cstdio>\n'
                             'int main(){std::printf("%u\\n", unsigned(sizeof(void*)));return sizeof(void*)==8?0:8;}\n')
        binary = self.folder / 'hello.exe'
        evidence = self.folder / 'invocations.jsonl'
        env = dict(os.environ, EVRM_NATIVE_BUILD_EVIDENCE=str(evidence))
        built = self.build('-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', source, '-o', binary, env=env)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        run = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        self.assertEqual((run.returncode, run.stdout.strip()), (0, '8'))
        records = [json.loads(line) for line in evidence.read_text().splitlines()]
        calls = [record for record in records if record.get('event') == 'invocation']
        self.assertTrue(any(Path(record['command'][0]).name.lower() == 'cl.exe' for record in calls))
        self.assertTrue(any(Path(record['command'][0]).name.lower() == 'link.exe' for record in calls))
        self.assertTrue(all(record['identity']['compiler']['version'] == '19.44.35229.0' for record in calls))
        self.assertTrue(all(record['identity']['sdk_version'] == '10.0.28000.0' for record in calls))

    def test_same_basename_objects_and_c_extension_keep_cpp_semantics(self):
        main = self.source('test/same.cpp', 'int answer();int main(){return answer()==42?0:1;}\n')
        implementation = self.source('src/same.cpp', 'int answer(){return 42;}\n')
        cpp_c = self.source('legacy.c', 'template<class T>T same(T x){return x;} int legacy(){return same(1);}\n')
        binary = self.folder / 'same.exe'
        built = self.build('-std=c++17', '-O2', '-static', main, implementation, cpp_c, '-o', binary)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)

    def test_explicit_c11_object_links_with_cpp_and_static_crt(self):
        source = self.source('vendor.c', '_Static_assert(sizeof(void*)==8,"x64");int answer(void){return 42;}\n')
        obj = self.folder / 'vendor.o'
        built = self.build('-std=c11', '-O2', '-c', source, '-o', obj)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        main = self.source('main.cpp', 'extern "C" int answer(void);int main(){return answer()==42?0:1;}\n')
        binary = self.folder / 'c11.exe'
        built = self.build('-std=c++17', '-O2', '-static', main, obj, '-o', binary)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)

    def test_compile_link_and_cli_errors_are_not_hidden(self):
        invalid = self.source('invalid.cpp', '#error intentional_compiler_failure\n')
        failed = self.build('-std=c++17', invalid, '-o', self.folder / 'invalid.exe')
        self.assertEqual(failed.returncode, 2, failed.stdout + failed.stderr)
        self.assertIn('intentional_compiler_failure', failed.stdout + failed.stderr)
        with self.assertRaises(subprocess.CalledProcessError) as raised:
            self.build('-std=c++17', invalid, '-o', self.folder / 'invalid.exe', check=True)
        self.assertEqual(raised.exception.returncode, failed.returncode)
        unresolved = self.source('unresolved.cpp', 'int missing();int main(){return missing();}\n')
        linked = self.build('-std=c++17', unresolved, '-o', self.folder / 'unresolved.exe')
        self.assertNotEqual(linked.returncode, 0)
        self.assertIn('LNK2019', linked.stdout + linked.stderr)
        cli = subprocess.run([sys.executable, str(ROOT / 'portable/native_compiler.py'),
                              DEFAULT_COMPILER, '-std=c++17', str(invalid), '-o', str(self.folder / 'cli.exe')],
                             capture_output=True, text=True, timeout=60)
        self.assertEqual(cli.returncode, failed.returncode)
        self.assertFalse((self.folder / 'cli.exe').exists())
        cli_link = subprocess.run([sys.executable, str(ROOT / 'portable/native_compiler.py'),
                                   DEFAULT_COMPILER, '-std=c++17', str(unresolved), '-o', str(self.folder / 'cli_link.exe')],
                                  capture_output=True, text=True, timeout=60)
        self.assertEqual(cli_link.returncode, linked.returncode)
        self.assertIn('LNK2019', cli_link.stdout + cli_link.stderr)
        self.assertFalse((self.folder / 'cli_link.exe').exists())

    def test_meaningful_unknown_flags_and_wrap_targets_are_rejected(self):
        source = self.source('main.cpp', 'int main(){return 0;}\n')
        for option in ('-ffast-math', '-Wl,--wrap=unreviewed_symbol', '/unreviewed-switch'):
            with self.subTest(option=option), self.assertRaisesRegex(ValueError, 'Unsupported'):
                self.build(option, source, '-o', self.folder / 'unsupported.exe')
        self.assertFalse((self.folder / 'unsupported.exe').exists())

    def test_linker_prefix_typos_and_missing_or_malformed_values_are_rejected(self):
        source = self.source('main.cpp', 'int main(){return 0;}\n')
        invalid = (
            '/NOLOGOTYPO', '/MACHINE:X64TYPO', '/INCREMENTAL:NOTYPO',
            '/DLLTYPO', '/DEBUGTYPO', '/DEBUG:FULLTYPO', '/NXCOMPATTYPO',
            '/DYNAMICBASETYPO', '/HIGHENTROPYVATYPO', '/BreproTYPO',
            '/OPT:REFTYPO', '/OPT:ICFTYPO', '/GUARD:CFTYPO',
            '/PDB:', '/PDBALTPATH:', '/MAP:', '/BASE:', '/BASE:garbage',
            '/STACK:garbage', '/STACK:1048576,garbage', '/VERSION:garbage',
            '/SUBSYSTEM:WINDOWSTYPO', '/ENTRY:', '/EXPORT:', '/LIBPATH:',
            '/DEF:', '/OUT:', '/IMPLIB:', '/NODEFAULTLIB:',
        )
        for index, option in enumerate(invalid):
            binary = self.folder / f'rejected-{index}.exe'
            with self.subTest(option=option), self.assertRaisesRegex(ValueError, 'Unsupported'):
                self.build(source, '-o', binary, '/link', option)
            self.assertFalse(binary.exists(), 'Rejected options must not emit a binary')

    def test_complete_supported_linker_settings_reach_real_linker(self):
        source = self.source('options.cpp', 'int main(){return 0;}\n')
        binary = self.folder / 'options.exe'
        built = self.build(source, '-o', binary, '/link', '/NOLOGO',
                           '/MACHINE:X64', '/INCREMENTAL:NO', '/NXCOMPAT',
                           '/DYNAMICBASE', '/HIGHENTROPYVA', '/Brepro',
                           '/OPT:REF', '/OPT:ICF', '/STACK:2097152,4096',
                           '/VERSION:1.2', '/SUBSYSTEM:CONSOLE,6.00')
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        self.assertNotIn('LNK4044', built.stdout + built.stderr)
        self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)

    def test_warning_as_error_is_still_enforced(self):
        source = self.source('warning.cpp', 'int main(){int unused;return 0;}\n')
        result = self.build('-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', source, '-o', self.folder / 'warning.exe')
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('C4101', result.stdout + result.stderr)

    def test_preprocessing_hlsl_as_cpp_uses_stdout_without_line_markers(self):
        source = self.source('fixture.hlsl', '#if FIXTURE\nfloat shader_token;\n#endif\n')
        result = self.build('-E', '-P', '-x', 'c++', '-DFIXTURE=1', source)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn('float shader_token;', result.stdout)
        self.assertNotIn('#line', result.stdout)

    def test_forced_include_and_read_process_memory_import_injection(self):
        hook = self.source('forced.hpp', '#define FORCED_VALUE 42\n')
        source = self.source('import_hook.cpp', '#include <windows.h>\n'
                             'using Read = BOOL(WINAPI*)(HANDLE,LPCVOID,LPVOID,SIZE_T,SIZE_T*);\n'
                             'BOOL WINAPI observed(HANDLE,LPCVOID,LPVOID,SIZE_T,SIZE_T*){return FORCED_VALUE;}\n'
                             'extern "C" {Read __wrap___imp_ReadProcessMemory=&observed;}\n'
                             'int main(){return ReadProcessMemory(nullptr,nullptr,nullptr,0,nullptr)==42?0:1;}\n')
        binary = self.folder / 'import_hook.exe'
        built = self.build('-std=c++17', '-O2', '-static', '-include', hook,
                           '-Wl,--wrap=__imp_ReadProcessMemory', source, '-o', binary)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)

    def test_shared_dll_definition_import_library_and_link_only_build(self):
        source = self.source('library.cpp', 'extern "C" int answer(){return 42;}\n')
        definition = self.source('library.def', 'LIBRARY fixture\nEXPORTS\n answer\n')
        binary = self.folder / 'fixture.dll'
        implib = self.folder / 'fixture.a'
        built = self.build('-std=c++17', '-O2', '-static', '-shared', '-fstack-protector-strong',
                           '-Wl,--image-base,0x190000000', '-Wl,--out-implib,' + str(implib),
                           source, definition, '-o', binary)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        import ctypes
        library = ctypes.WinDLL(str(binary))
        self.assertEqual(library.answer(), 42)
        # Release this test's handle before TemporaryDirectory cleanup.
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.FreeLibrary.argtypes = [ctypes.c_void_p]
        self.assertTrue(kernel.FreeLibrary(library._handle))
        main = self.source('main.cpp', 'extern "C" __declspec(dllimport) int answer();int main(){return answer()==42?0:1;}\n')
        obj = self.folder / 'main.obj'
        built = self.build('-std=c++17', '-O2', '-static', '-c', main, '-o', obj)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        executable = self.folder / 'linked.exe'
        built = self.build(obj, implib, '-o', executable)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        self.assertEqual(subprocess.run([str(executable)], timeout=10).returncode, 0)

    def test_native_security_and_private_debug_options_build_with_microsoft_tools(self):
        source = self.source('debug.cpp', 'int main(){return 0;}\n')
        binary, symbols, linkmap = (self.folder / name for name in ('debug.exe', 'private.pdb', 'private.map'))
        built = self.build('/std:c++17', '/O2', '/MT', '/W4', '/WX', '/GS', '/Brepro', '/Z7', source, '-o', binary,
                           '/link', '/DEBUG:FULL', '/PDB:' + str(symbols), '/PDBALTPATH:%_PDB%',
                           '/MAP:' + str(linkmap), '/BASE:0x140000000')
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        self.assertTrue(symbols.is_file())
        self.assertTrue(linkmap.is_file())
        self.assertEqual(subprocess.run([str(binary)], timeout=10).returncode, 0)

    def test_source_root_is_trimmed_without_disabling_header_assertions(self):
        build_root = self.folder / 'source with spaces'
        self.source('source with spaces/vendor/probe.hpp',
                    '#pragma once\n#include <cassert>\n'
                    'inline void require_value(bool value){assert(value);}\n'
                    'inline const char* narrow_file(){return __FILE__;}\n'
                    'inline const wchar_t* wide_file(){return _CRT_WIDE(__FILE__);}\n')
        self.source('source with spaces/src/main.cpp',
                    '#include "probe.hpp"\n#include <windows.h>\n'
                    '#include <cstdio>\n#include <cstdlib>\n#include <cwchar>\n'
                    'int main(int argc,char**){SetErrorMode(0x8003);_set_error_mode(_OUT_TO_STDERR);'
                    '_set_abort_behavior(0,_WRITE_ABORT_MSG|_CALL_REPORTFAULT);'
                    'std::printf("%s\\n",narrow_file());require_value(argc==1);'
                    'return std::wcsstr(wide_file(),L"vendor")?0:7;}\n')
        binary = self.folder / 'trim.exe'
        built = self.build('/W4', '/WX', '/Z7', '/Brepro',
                           '/d1trimfile:' + str(build_root) + '\\', '-I.\\vendor',
                           '.\\src\\main.cpp', '-o', binary, '/link', '/DEBUG:FULL',
                           '/PDB:' + str(self.folder / 'trim.pdb'), '/PDBALTPATH:%_PDB%',
                           cwd=build_root)
        self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        self.assertNotIn('D9002', built.stdout + built.stderr)
        data = binary.read_bytes()
        self.assertNotIn(str(build_root).encode(), data)
        self.assertNotIn(str(build_root).encode('utf-16le'), data)
        passed = subprocess.run([str(binary)], capture_output=True, text=True, timeout=10)
        self.assertEqual(passed.returncode, 0, passed.stdout + passed.stderr)
        self.assertIn('vendor\\probe.hpp', passed.stdout)
        failed = subprocess.run([str(binary), 'fail'], capture_output=True, text=True, timeout=10)
        self.assertEqual(failed.returncode, 3, failed.stdout + failed.stderr)
        self.assertIn('vendor\\probe.hpp', failed.stderr)
        self.assertNotIn(str(build_root), passed.stdout + failed.stderr)

    def test_real_dependency_trace_contains_all_sources_and_headers(self):
        from native_fixture_compiler import native_dependencies
        header = self.source('shared/header.hpp', '#define ANSWER 42\n')
        first = self.source('first.cpp', '#include "shared/header.hpp"\nint first(){return ANSWER;}\n')
        second = self.source('second.cpp', '#include "shared/header.hpp"\nint second(){return ANSWER;}\n')
        result, paths = native_dependencies([DEFAULT_COMPILER, '-std=c++17', str(first), str(second)], timeout=60)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertTrue({first.resolve(), second.resolve(), header.resolve()}.issubset(paths), paths)
        self.assertTrue(all(path.is_file() for path in paths))


if __name__ == '__main__':
    unittest.main()
