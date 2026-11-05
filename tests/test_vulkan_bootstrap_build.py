"""Build/export checks only: never load or execute the candidate bootstrap."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import tempfile
import unittest
import sys
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'portable'))
from native_compiler import default_compiler


class VulkanBootstrapBuildTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = default_compiler()
        cls.bootstrap_compiler = os.environ.get('EVRM_TEST_BOOTSTRAP_COMPILER_DIRECTORY') or str(Path(cls.compiler).parent)
        if os.name != 'nt':
            raise unittest.SkipTest('Windows x64 compiler required')
        destination = os.environ.get('EVRM_VULKAN_BOOTSTRAP_BUILD_DIRECTORY')
        if destination:
            cls.folder = Path(destination).resolve()
            cls.folder.mkdir(parents=True, exist_ok=False)
        else:
            temporary = tempfile.TemporaryDirectory(prefix='evrm-vulkan-build-')
            cls.addClassCleanup(temporary.cleanup)
            cls.folder = Path(temporary.name)
        spec = importlib.util.spec_from_file_location('vulkan_bootstrap_builder', ROOT/'portable/bootstrap/build_bootstrap.py')
        cls.builder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.builder)

    def test_distributable_forwards_complete_vulkan_surface_reproducibly(self):
        provider = Path(os.environ['SystemRoot'])/'System32/vulkan-1.dll'
        # A filename or provider regression would prevent normal game loading.
        # The compiler-produced PE is inspected as data, never loaded here.
        try:
            first = self.builder.build_bootstrap(ROOT, self.folder/'bootstrap', Path(self.bootstrap_compiler), provider)
        except (ValueError, OSError) as error:
            self.fail('Microsoft production builder must accept the installed Vulkan provider: '+str(error))
        self.assertEqual(first['dll'].name, 'vulkan-1.dll')
        expected = self.builder._exports.export_inventory(provider)
        actual = self.builder._exports.export_inventory(first['dll'])
        shape = lambda inventory: [(x['ordinal'], x['names']) for x in inventory['exports']]
        self.assertEqual(shape(actual), shape(expected))
        names = {name for item in actual['exports'] for name in item['names']}
        self.assertTrue({'vkGetInstanceProcAddr', 'vkGetDeviceProcAddr', 'vkCreateInstance'} <= names)
        import ctypes
        directory = ctypes.create_unicode_buffer(32768)
        self.assertTrue(ctypes.windll.kernel32.GetSystemDirectoryW(directory, len(directory)))
        target = directory.value+'\\vulkan-1.'
        for entry in actual['exports']:
            symbol = entry['names'][0] if entry['names'] else '#'+str(entry['ordinal'])
            self.assertEqual(entry.get('forwarder'), target+symbol)
        second = self.builder.build_bootstrap(ROOT, self.folder/'bootstrap-repeat', Path(self.bootstrap_compiler), provider)
        self.assertEqual(first['dll'].read_bytes(), second['dll'].read_bytes())
        receipt = json.loads((first['dll'].parent/'toolchain.json').read_bytes())
        self.assertEqual(receipt['toolsetVersion'], '14.44.35207')
        self.assertEqual(receipt['windowsSdkVersion'], '10.0.28000.0')
        self.assertEqual(receipt['tools']['cl']['fileVersion'], '19.44.35229.0')
        for name in ('cl', 'link', 'lib', 'rc'):
            identity = receipt['tools'][name]
            self.assertEqual(identity['sha256'], hashlib.sha256(Path(identity['path']).read_bytes()).hexdigest())
        self.assertEqual(receipt['dllSha256'], hashlib.sha256(first['dll'].read_bytes()).hexdigest())
        for name, digest in receipt['sourceInputs'].items():
            self.assertEqual(digest, hashlib.sha256((ROOT/name).read_bytes()).hexdigest())
        self.assertEqual(receipt['exportInventory'], actual)
        import struct
        data = first['dll'].read_bytes()
        pe = struct.unpack_from('<I', data, 60)[0]
        self.assertEqual(struct.unpack_from('<II', data, pe+12), (0, 0), 'Microsoft production image must not retain COFF symbols')
        self.assertEqual(struct.unpack_from('<H', data, pe+24+70)[0] & 0x160, 0x160)
        self.assertNotIn(b'globalroot', data.lower())
        result = {'bootstrapSha256': hashlib.sha256(first['dll'].read_bytes()).hexdigest(),
                  'bootstrapBytes': first['dll'].stat().st_size, 'exportCount': len(actual['exports']),
                  'providerSha256': expected['providerSha256'], 'twoBuildsIdentical': True,
                  'scope': 'static build and metadata only; no bootstrap execution'}
        (self.folder/'static-build-receipt.json').write_text(json.dumps(result, indent=2)+'\n', encoding='utf-8')
        print(json.dumps(result), flush=True)

    def test_production_build_rejects_old_power_provider(self):
        with self.assertRaisesRegex(ValueError, 'inventory differs'):
            self.builder.build_bootstrap(ROOT, self.folder/'wrong-provider', Path(self.compiler).parent,
                                         Path(os.environ['SystemRoot'])/'System32/powrprof.dll')

    def test_matching_metadata_cannot_replace_the_system_reference(self):
        accepted = json.loads((ROOT/'portable/bootstrap/vulkan-exports.json').read_bytes())
        with mock.patch.object(self.builder._exports, 'export_inventory', return_value=accepted):
            with self.assertRaisesRegex(ValueError, 'installed system Vulkan DLL'):
                self.builder.build_bootstrap(ROOT, self.folder/'non-system-reference', Path(self.bootstrap_compiler),
                                             self.folder/'owned-not-system.dll')

    def test_rejects_a_non_sdk_resource_compiler(self):
        with mock.patch.dict(os.environ, {'EVRM_BOOTSTRAP_RESOURCE_COMPILER':str(Path(self.bootstrap_compiler)/'cl.exe')}):
            with self.assertRaisesRegex(ValueError, 'pinned Windows SDK'):
                self.builder.build_bootstrap(ROOT, self.folder/'wrong-resource-tool', Path(self.bootstrap_compiler),
                                             self.builder.system_provider())


if __name__ == '__main__':
    unittest.main()
