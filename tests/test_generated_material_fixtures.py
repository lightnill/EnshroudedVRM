"""CPU-only tests. Native benchmark binaries are never executed here."""
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

ROOT=Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('fixture_generator',ROOT/'tests/generated_material_fixtures.py')
generator=importlib.util.module_from_spec(spec)
spec.loader.exec_module(generator)


class GeneratedMaterialFixtureTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:raise unittest.SkipTest('C++ compiler required for actual importer verification')
        if os.name=='nt':ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        cls.tmp=tempfile.TemporaryDirectory(prefix='evrm-generated-materials-',ignore_cleanup_errors=True)
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.folder=Path(cls.tmp.name)
        cls.exporter=cls.folder/'fixture-export.exe'
        result=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(ROOT/'tests/native_surface_fixture_export.cpp'),str(ROOT/'src/vrm_asset.cpp'),'-o',str(cls.exporter)],capture_output=True,text=True,timeout=180)
        if result.returncode:raise AssertionError(result.stdout+result.stderr)
        cls.manifest=generator.generate(cls.folder/'a')

    def test_deterministic_files_and_no_external_resources(self):
        other=generator.generate(self.folder/'b')
        self.assertEqual(self.manifest,other)
        for entry in self.manifest['cases']:
            a=(self.folder/'a'/(entry['name']+'.vrm')).read_bytes()
            b=(self.folder/'b'/(entry['name']+'.vrm')).read_bytes()
            self.assertEqual(a,b)
            self.assertEqual(hashlib.sha256(a).hexdigest(),entry['sha256'])
            doc=json.loads(a[20:20+int.from_bytes(a[12:16],'little')])
            self.assertTrue(all('uri' not in image for image in doc['images']))
            self.assertTrue(all('uri' not in buffer for buffer in doc['buffers']))

    def test_actual_import_and_inherited_policies(self):
        for entry in self.manifest['cases']:
            if entry.get('expected_import')=='reject':continue
            with self.subTest(case=entry['name']):
                name=entry['name'];folder=self.folder/'a';output=self.folder/'packets'/name
                result=subprocess.run([str(self.exporter),str(folder/(name+'.vrm')),str(folder/(name+'.reference.json')),str(output)],capture_output=True,text=True,timeout=30)
                self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                packet=json.loads((output/'packet.json').read_text())
                self.assertEqual(packet['policies']['0'][0],packet['features'])
                self.assertEqual(packet['policies']['2'][0],packet['features'])
                self.assertEqual(packet['policies']['3'][0],0)
                self.assertEqual((output/'constants.bin').stat().st_size,192)
                self.assertEqual((output/'inputs.bin').stat().st_size,16*96)
                self.assertFalse(packet['textures'][1]['srgb'])
                self.assertTrue(packet['textures'][2]['srgb'] and packet['textures'][3]['srgb'])

    def test_actual_import_rejects_invalid_metadata(self):
        folder=self.folder/'a'
        for entry in self.manifest['cases']:
            if entry.get('expected_import')!='reject':continue
            with self.subTest(case=entry['name']):
                result=subprocess.run([str(self.exporter),str(folder/(entry['name']+'.vrm')),str(folder/'vrm1-masked.reference.json'),str(self.folder/'invalid')],capture_output=True,text=True,timeout=30)
                self.assertNotEqual(result.returncode,0)
                self.assertNotIn('cannot open',result.stderr)
                self.assertTrue(result.stderr.strip())

    def test_independent_mask_oracle_and_alpha(self):
        _,legacy=generator.make_case(0,'masked')
        _,modern=generator.make_case(1,'masked')
        self.assertGreater(legacy['samples'][0]['unlit'][0],0)
        self.assertEqual(modern['samples'][0]['unlit'],[0,0,0])
        self.assertEqual(modern['samples'][0]['lit'],[0,0,0])
        self.assertLess(modern['samples'][2]['unlit'][0],modern['samples'][1]['unlit'][0]*.3)
        for version in (0,1):
            for kind in ('masked','normal','neutral','map_free','mixed_alpha'):
                _,reference=generator.make_case(version,kind)
                self.assertEqual([s['rgba'][3] for s in reference['samples'][:4]],[1,128/255,0,1])


if __name__=='__main__':unittest.main()
