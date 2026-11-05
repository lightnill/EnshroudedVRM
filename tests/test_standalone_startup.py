"""Checked startup paths/import fixtures. Never attest a synthetic image globally."""
from native_fixture_compiler import default_compiler, run_compiler
from pathlib import Path
import ctypes
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class StandaloneStartupTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler=default_compiler()
        if os.name!='nt' or not cls.compiler:raise unittest.SkipTest('Windows compiler required')
        cls.temporary=tempfile.TemporaryDirectory(prefix='evrm-native-startup-',ignore_cleanup_errors=True)
        cls.addClassCleanup(cls.temporary.cleanup)
        cls.folder=Path(cls.temporary.name);cls.binary=cls.folder/'startup.exe'
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)

    def build_startup_fixture(self):
        if self.binary.exists():return
        self.assertTrue((ROOT/'src/standalone_startup.cpp').is_file(),'checked native startup evaluator missing')
        result=run_compiler([self.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-municode',
                               str(ROOT/'tests/standalone_startup.cpp'),
                               *[str(ROOT/'src'/name) for name in ['standalone_startup.cpp','standalone_settings.cpp','standalone_files.cpp','standalone_startup_environment.cpp','avatar_settings.cpp','mod_settings.cpp']],
                               '-o',str(self.binary)],capture_output=True,text=True,timeout=240)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)

    def test_context_and_atomic_imports(self):
        self.build_startup_fixture()
        for mode in ['context','migration','readonly']:
            with self.subTest(mode=mode):
                folder=self.folder/mode;folder.mkdir(exist_ok=True)
                result=subprocess.run([str(self.binary),str(folder),mode],capture_output=True,text=True,timeout=60)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                print(result.stdout.strip())

    def test_declined_supported_package_has_fresh_reason_without_feature_admission(self):
        self.build_startup_fixture()
        for mode in ['diagnostic-missing','diagnostic-mutated','diagnostic-unsupported','diagnostic-other','diagnostic-readonly']:
            with self.subTest(mode=mode):
                result=subprocess.run([str(self.binary),str(self.folder/mode),mode],capture_output=True,text=True,timeout=30)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                print(result.stdout.strip())

    def test_immutable_package_read_still_refuses_a_reparse_parent(self):
        self.build_startup_fixture()
        root=(self.folder/'reparse-package').resolve();root.mkdir()
        prepared=subprocess.run([str(self.binary),str(root),'prepare-reparse'],capture_output=True,text=True,timeout=30)
        self.assertEqual(prepared.returncode,0,prepared.stdout+prepared.stderr)
        source=root/'game/EnshroudedVRM/Compatibility';target=root/'owned-compatibility'
        self.assertTrue(source.resolve().is_relative_to(root))
        self.assertTrue(target.resolve().is_relative_to(root))
        self.assertFalse(target.exists())
        source.rename(target)
        command=Path(os.environ['SystemRoot'])/'System32/cmd.exe'
        linked=subprocess.run([str(command),'/d','/c','mklink','/J',str(source),str(target)],capture_output=True,text=True,timeout=15)
        self.assertEqual(linked.returncode,0,linked.stdout+linked.stderr)
        self.assertTrue(source.is_junction())
        original={p.name:p.read_bytes() for p in target.iterdir()}
        try:
            result=subprocess.run([str(self.binary),str(root),'check-reparse'],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertEqual({p.name:p.read_bytes() for p in target.iterdir()},original)
        finally:
            # Remove only the verified owned junction; its target is retained.
            self.assertTrue(source.is_junction())
            source.rmdir()

    def test_production_context_is_once_only_and_cannot_trust_environment(self):
        adapter=ROOT/'src/standalone_startup_runtime.cpp'
        self.assertTrue(adapter.exists(),'native process attestation adapter missing')
        binary=self.folder/'assets.exe'
        sources=['standalone_startup.cpp','standalone_startup_runtime.cpp','standalone_files.cpp','standalone_settings.cpp',
                 'avatar_settings.cpp','mod_settings.cpp','native_owner_process.cpp','native_owner.cpp','enshrouded_bridge.cpp']
        include=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if not include:self.skipTest('Vulkan headers required for checked compatibility adapter')
        result=run_compiler([self.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-municode',
                               str(ROOT/'tests/standalone_runtime_dormant.cpp'),*[str(ROOT/'src'/name) for name in sources],
                               '-I'+include,'-lshell32','-lole32','-luuid','-o',str(binary)],capture_output=True,text=True,timeout=240)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        for name in ['assets.exe','enshrouded.exe']:
            child=self.folder/name
            if child!=binary:shutil.copyfile(binary,child)
            result=subprocess.run([str(child)],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            print(name,result.stdout.strip())
        disk=os.environ.get('EVRM_TEST_GAME_EXE')
        if disk:
            result=subprocess.run([str(binary),disk],capture_output=True,text=True,timeout=45)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            print(result.stdout.strip())

    def test_storage_independent_readback_startup(self):
        source=(ROOT/'src/gpu_probe.cpp').read_text(encoding='utf-8')
        methods=source[source.index('GpuProbe::GpuProbe('):source.index('GpuProbe::~GpuProbe()')]
        methods+=source[source.index('bool GpuProbe::enabled()'):source.index('bool GpuProbe::player_anchor_capture_enabled()')]
        (self.folder/'standalone_probe_startup_methods.inc').write_text(methods,encoding='utf-8')
        binary=self.folder/'probe_startup.exe'
        built=run_compiler([self.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-I'+str(self.folder),
                              str(ROOT/'tests/standalone_probe_startup.cpp'),'-o',str(binary)],capture_output=True,text=True,timeout=90)
        self.assertEqual(built.returncode,0,built.stdout+built.stderr)
        run=subprocess.run([str(binary),str(self.folder)],capture_output=True,text=True,timeout=30)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_legacy_environment_is_cleared_and_startup_history_is_bounded(self):
        source=ROOT/'src/standalone_startup_environment.cpp'
        self.assertTrue(source.exists(),'checked startup environment adapter missing')
        binary=self.folder/'environment.exe'
        build=run_compiler([self.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-municode',
                              str(ROOT/'tests/standalone_environment.cpp'),str(source),str(ROOT/'src/standalone_files.cpp'),'-o',str(binary)],capture_output=True,text=True,timeout=90)
        self.assertEqual(build.returncode,0,build.stdout+build.stderr)
        for mode in ['declined','ready','readonly']:
            with self.subTest(mode=mode):
                folder=self.folder/('environment-'+mode)
                run=subprocess.run([str(binary),str(folder),mode],capture_output=True,text=True,timeout=30)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_services_require_native_admission_and_preserve_readonly_live_settings(self):
        binary=self.folder/'services.exe'
        sources=['avatar_runtime.cpp','mod_settings_service.cpp','mod_settings.cpp']
        built=run_compiler([self.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-municode',
                              '-ffunction-sections','-fdata-sections','-Wl,--gc-sections',str(ROOT/'tests/standalone_services.cpp'),
                              *[str(ROOT/'src'/name) for name in sources],'-o',str(binary)],capture_output=True,text=True,timeout=120)
        self.assertEqual(built.returncode,0,built.stdout+built.stderr)
        for mode in ['writable','readonly']:
            run=subprocess.run([str(binary),str(self.folder/('services-'+mode)),mode],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_final_log_open_cannot_truncate_a_replacement_link(self):
        source=(ROOT/'src/standalone_startup_environment.cpp').read_text(encoding='utf-8')
        (self.folder/'standalone_log_commit.inc').write_text(source[source.index('bool write_new('):source.index('void rotate_history(')],encoding='utf-8')
        binary=self.folder/'log_commit.exe'
        built=run_compiler([self.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-municode','-I'+str(self.folder),
                              str(ROOT/'tests/standalone_log_commit.cpp'),'-o',str(binary)],capture_output=True,text=True,timeout=90)
        self.assertEqual(built.returncode,0,built.stdout+built.stderr)
        run=subprocess.run([str(binary),str(self.folder)],capture_output=True,text=True,timeout=30)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_voice_startup_is_once_only_and_audio_remains_opt_in(self):
        binary=self.folder/'voice_admission.exe'
        built=run_compiler([self.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',
                              str(ROOT/'tests/standalone_voice_admission.cpp'),str(ROOT/'src/voice_inputs.cpp'),str(ROOT/'src/mod_settings.cpp'),
                              '-o',str(binary)],capture_output=True,text=True,timeout=90)
        self.assertEqual(built.returncode,0,built.stdout+built.stderr)
        run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)


if __name__=='__main__':unittest.main()
