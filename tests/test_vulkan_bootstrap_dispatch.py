"""Exact-approved-bootstrap dispatch/GPU fixtures. Never launches the game."""
from pathlib import Path
import ctypes
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class VulkanBootstrapDispatchTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        directory = os.environ.get('EVRM_TEST_BOOTSTRAP_COMPILER_DIRECTORY')
        include = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        approved = os.environ.get('EVRM_APPROVED_BOOTSTRAP_SHA256')
        if os.name != 'nt' or not directory or not include or not approved:
            raise unittest.SkipTest('Microsoft tools, Vulkan headers and exact approved bootstrap hash required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode() | 0x8003)
        temporary = tempfile.TemporaryDirectory(prefix='evrm-vulkan-dispatch-', ignore_cleanup_errors=True)
        cls.addClassCleanup(temporary.cleanup)
        cls.folder = Path(temporary.name)
        cls.include, cls.compiler = include, Path(directory)/'cl.exe'
        spec = importlib.util.spec_from_file_location('dispatch_bootstrap_builder', ROOT/'portable/bootstrap/build_bootstrap.py')
        cls.builder = importlib.util.module_from_spec(spec); spec.loader.exec_module(cls.builder)
        cls.built = cls.builder.build_bootstrap(ROOT, cls.folder/'bootstrap', directory, cls.builder.system_provider())
        cls.approved = approved
        if hashlib.sha256(cls.built['dll'].read_bytes()).hexdigest() != approved:
            raise AssertionError('Candidate differs from the explicitly scanned/approved bootstrap; do not execute it')
        inventory = cls.builder._exports.export_inventory(cls.built['dll'])
        cls.symbols = '\n'.join([name for entry in inventory['exports'] for name in entry['names']] +
                                ['#'+str(entry['ordinal']) for entry in inventory['exports']])+'\n'
        cls.common = [cls.compiler, '/nologo', '/std:c++17', '/O2', '/MT', '/EHsc', '/utf-8', '/W4', '/WX', '/I'+include]
        cls.compile([*cls.common, '/LD', ROOT/'tests/bootstrap_vulkan_attach.cpp', cls.built['import_library'],
                     '/Fo'+str(cls.folder/'attach.obj'), '/Fe'+str(cls.folder/'owned_dependency.dll'), '/link', '/INCREMENTAL:NO'])
        for name, extra in [('dynamic', []), ('direct', ['/DEVRM_DIRECT_IMPORT', cls.built['import_library']]),
                            ('dependent', ['/DEVRM_DIRECT_IMPORT', '/DEVRM_EARLY_DEPENDENCY', cls.built['import_library'], cls.folder/'owned_dependency.lib']),
                            ('native', ['/DEVRM_CHECK_NATIVE_DORMANT'])]:
            cls.compile([*cls.common, ROOT/'tests/bootstrap_vulkan_dispatch.cpp', *extra,
                         '/Fo'+str(cls.folder/(name+'.obj')), '/Fe'+str(cls.folder/(name+'.exe')), '/link', '/INCREMENTAL:NO'])
        probe = cls.folder/'probe.cpp'
        probe.write_text((ROOT/'tests/bootstrap_layer_probe.cpp').read_text().replace('__declspec(dllexport) ', ''))
        definition = cls.folder/'probe.def'
        definition.write_text('LIBRARY owned_probe_layer\nEXPORTS\n vkGetInstanceProcAddr\n vkGetDeviceProcAddr\n vkNegotiateLoaderLayerInterfaceVersion\n')
        cls.compile([*cls.common, '/LD', probe, '/Fo'+str(cls.folder/'probe.obj'), '/Fe'+str(cls.folder/'probe.dll'),
                     '/link', '/INCREMENTAL:NO', '/DEF:'+str(definition)])

    @classmethod
    def compile(cls, command):
        result = subprocess.run(list(map(str, command)), cwd=cls.folder, capture_output=True, text=True, timeout=90)
        if result.returncode:
            raise AssertionError(result.stdout+result.stderr)

    def run_case(self, leaf, kind='dynamic', manifest='valid', runtime='probe', changes=None, marker=b'ID', mode='gpu', expected_exit=0):
        folder = self.folder/leaf; folder.mkdir()
        proxy = folder/'vulkan-1.dll'; shutil.copyfile(self.built['dll'], proxy)
        self.assertEqual(hashlib.sha256(proxy.read_bytes()).hexdigest(), self.approved)
        child = folder/'enshrouded.exe'; shutil.copyfile(self.folder/(kind+'.exe'), child)
        if kind == 'dependent':
            shutil.copyfile(self.folder/'owned_dependency.dll', folder/'owned_dependency.dll')
        runtime_folder = folder/'EnshroudedVRM/Runtime'; runtime_folder.mkdir(parents=True)
        path = runtime_folder/'enshrouded_vrm_layer.json'
        if manifest == 'valid': path.write_bytes((ROOT/'portable/standalone_layer.json').read_bytes())
        elif manifest == 'invalid': path.write_bytes(b'{invalid}\n')
        elif manifest != 'missing': self.fail('unknown manifest state')
        native = runtime_folder/'enshrouded_vrm_runtime.dll'
        if runtime == 'probe': shutil.copyfile(self.folder/'probe.dll', native)
        elif runtime == 'invalid': native.write_bytes(b'Owned invalid optional-module fixture\n')
        elif runtime == 'native':
            source = Path(os.environ['EVRM_RUNTIME_TEST_DIRECTORY'])/'enshrouded_vrm_runtime.dll'
            shutil.copyfile(source, native)
        elif runtime != 'missing': self.fail('unknown runtime state')
        env = {k:v for k,v in os.environ.items() if not k.startswith(('VK_', 'ENSHROUDED_VRM_', 'EVRM_'))}
        env.update(changes(path) if callable(changes) else changes or {})
        initial = env.get('VK_ADD_IMPLICIT_LAYER_PATH', '')
        active = manifest == 'valid' and not env.get('ENSHROUDED_VRM_DISABLE') and 'VK_IMPLICIT_LAYER_PATH' not in env
        expected = initial
        if active and str(path).casefold() not in [x.rstrip('\\/').casefold() for x in initial.split(';')]:
            expected += (';' if initial and not initial.endswith(';') else '')+str(path)
        output_marker = folder/'marker.txt'
        env.update(EVRM_INITIAL_LAYER_PATH=initial, EVRM_EXPECT_LAYER_PATH=expected, EVRM_EXPECT_PROXY_PATH=str(proxy), EVRM_PROBE_MARKER=str(output_marker))
        result = subprocess.run([str(child), mode], cwd=self.folder, env=env, input=self.symbols,
                                capture_output=True, text=True, timeout=30)
        self.assertEqual(result.returncode, expected_exit, result.stdout+result.stderr)
        self.assertEqual(output_marker.read_bytes() if output_marker.exists() else b'', marker)
        if expected_exit == 0:
            self.assertIn('forwarding matched 530 names/ordinals', result.stdout)
            self.assertIn('2048 concurrent resolution/global-dispatch checks passed', result.stdout)
            self.assertIn('4096-byte GPU fill/readback passed', result.stdout)
        return result

    def test_bare_name_and_ordinary_import_real_dispatch(self):
        for kind in ('dynamic', 'direct'):
            with self.subTest(kind=kind): self.run_case('dispatch-'+kind, kind=kind)

    def test_optional_failures_and_user_controls_preserve_graphics(self):
        for leaf, changes in [
            ('missing-manifest', {'manifest':'missing'}), ('invalid-manifest', {'manifest':'invalid'}),
            ('missing-runtime', {'runtime':'missing'}), ('invalid-runtime', {'runtime':'invalid'}),
            ('disabled', {'changes':{'ENSHROUDED_VRM_DISABLE':'1'}}),
            ('search-override', {'changes':{'VK_IMPLICIT_LAYER_PATH':''}}),
            ('filter-disabled', {'changes':{'VK_LOADER_LAYERS_DISABLE':'*'}}),
        ]:
            with self.subTest(case=leaf): self.run_case(leaf, marker=b'', **changes)

    def test_dependency_duplicate_inheritance_and_unicode(self):
        result = self.run_case('dependency', kind='dependent')
        self.assertIn('environment and global Vulkan dispatch available in dependency attach', result.stdout)
        self.run_case('duplicate', changes=lambda path:{'VK_ADD_IMPLICIT_LAYER_PATH':str(path)+';'+str(path)})
        self.run_case('Unicode 日本語 %folder')

    def test_already_loaded_system_loader_is_explicit_bypass(self):
        result = self.run_case('preloaded', mode='preloaded', marker=b'', expected_exit=1)
        self.assertIn('bare-name load bypassed proxy', result.stderr)

    def test_native_runtime_declines_unattested_owner(self):
        if not os.environ.get('EVRM_RUNTIME_TEST_DIRECTORY'):
            self.skipTest('exact scanned native runtime directory required')
        result = self.run_case('native', kind='native', runtime='native', marker=b'')
        self.assertIn('native runtime loaded, declined fixture owner, no device interception or optional lip-sync load', result.stdout)


if __name__ == '__main__': unittest.main()
