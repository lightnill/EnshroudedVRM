"""Asset-free synthetic standalone packaging; no network, game or antivirus bypass."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import stat
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import zipfile

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'portable'))


def sha(data):return hashlib.sha256(data).hexdigest()
def encoded(value):return (json.dumps(value,indent=2,sort_keys=True)+'\n').encode()
def pe(label):
    data=bytearray(512);data[:2]=b'MZ';struct.pack_into('<I',data,0x3c,128);data[128:132]=b'PE\0\0'
    struct.pack_into('<HH',data,132,0x8664,1);struct.pack_into('<H',data,148,240);struct.pack_into('<H',data,150,0x2022);struct.pack_into('<H',data,152,0x20b)
    return bytes(data)+label.encode('utf-16le')


class StandalonePackageTests(unittest.TestCase):
    def setUp(self):
        path=ROOT/'portable/package_standalone_release.py'
        self.assertTrue(path.exists(),'standalone package builder missing')
        spec=importlib.util.spec_from_file_location('standalone_package',path);self.module=importlib.util.module_from_spec(spec);spec.loader.exec_module(self.module)
        self.temporary=tempfile.TemporaryDirectory(prefix='evrm-standalone-package-',ignore_cleanup_errors=True);self.addCleanup(self.temporary.cleanup)
        self.folder=Path(self.temporary.name);self.source=self.folder/'source';self.source.mkdir()
        self.base=self.folder/'base.zip';self.output=self.folder/'out.zip'
        self.sdk=pe('owned fake voice SDK');self.addCleanup(patch.stopall)
        patch.object(self.module,'OWNED_PREFIX_RULES',()).start()
        patch.object(self.module,'VOICE_SDK_SHA256',sha(self.sdk)).start()
        self.base_payload={name.removeprefix('EnshroudedVRM/'):('accepted '+name).encode() for name in self.module.BASE_TARGET_MEMBERS if name!='vulkan-1.dll'}
        self.base_payload['Runtime/enshrouded_vrm_runtime.dll']=pe('old runtime')
        self.base_payload['Runtime/OVRLipSync.dll']=self.sdk
        self.base_payload['Compatibility/profile.json']=encoded({'schema':'enshrouded-native-rig-profile-v1','game':{'executable_sha256':'d'*64},'hierarchy_guid':'owned-rig','joint_count':2,'source_rest_global':[[1,0,0,0]],'semantic_to_joint':{'hips':0}})
        self.base_payload['Compatibility/catalog.json']=encoded({'schema':'enshrouded-player-catalog-v2','game':{'executable_sha256':'d'*64},'hierarchy_guid':'owned-rig','joint_count':2,'descriptor_signatures':[{'owned':'palette'}]})
        for name in self.module.BASE_SHADER_NAMES:self.base_payload['Runtime/shaders/'+name]=struct.pack('<IIIII',0x07230203,0x10000,0,1,0)
        for name in self.module.SCRIPT_NAMES:self.base_payload[name]=b'retired player script'
        self.info={'version':'0.2.0','runtimeSha256':sha(self.base_payload['Runtime/enshrouded_vrm_runtime.dll']),
                   'acceptedShaderModules':{name:sha(self.base_payload['Runtime/shaders/'+name]) for name in self.module.BASE_SHADER_NAMES},
                   'sourceInputs':[{'path':'src/runtime_bridge.cpp','sha256':'a'*64}], 'automaticBufferExports':False,'bundledAvatars':0,
                   'validation':{'antivirus':'old scan must not be inherited'}}
        self.base_payload['BUILD-INFO.json']=encoded(self.info)
        patch.object(self.module,'OWNED_SHADER_SHA256',{'native_volume_snapshot.comp.spv':self.info['acceptedShaderModules']['native_volume_snapshot.comp.spv']},create=True).start()
        self.write_base()
        for name in self.module.required_source_inputs(self.info):
            path=self.source/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(('owned source '+name).encode())
        for name,relative in self.module.LOCAL_MEMBERS.items():
            path=self.source/relative;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(('new standalone '+name).encode())
        (self.source/'portable/standalone_layer.json').write_bytes((ROOT/'portable/standalone_layer.json').read_bytes())
        document=self.source/'portable/standalone_docs/LEIA-ME-PT-BR.txt'
        document.parent.mkdir(parents=True,exist_ok=True);document.write_text('Guia de instalaÃƒÂ§ÃƒÂ£o em portuguÃƒÂªs: Steam e F8.\n',encoding='utf-8')
        self.runtime=self.folder/'runtime.dll';self.runtime.write_bytes(pe('owned new runtime'))
        self.bootstrap=self.folder/'vulkan-1.dll';self.bootstrap.write_bytes(pe('EnshroudedVRM standalone bootstrap'))
        self.shaders=self.folder/'shaders';self.shaders.mkdir()
        modules=[]
        for name in self.module.SHADER_NAMES:
            path=self.shaders/name;data=self.base_payload.get('Runtime/shaders/'+name,struct.pack('<IIIII',0x07230203,0x10000,0,1,0));path.write_bytes(data)
            modules.append({'name':name,'path':str(path),'sha256':sha(data)})
        self.shader_manifest=self.folder/'shaders.json';self.shader_manifest.write_bytes(encoded({'schema':'enshrouded-vrm-shaders-v1','modules':modules}))
        self.pins=self.folder/'source-pins.json';self.refresh_pins()
        self.sample=b'glTF'+b'owned authorized model fixture'
        patch.object(self.module,'SAMPLE_SHA256',sha(self.sample)).start()
        patch.object(self.module,'SAMPLE_BYTES',len(self.sample)).start()
        sample=self.source/self.module.SAMPLE_SOURCE;sample.parent.mkdir(parents=True,exist_ok=True);sample.write_bytes(self.sample)

    def write_base(self,extra=None):
        manifest={'schema':'enshrouded-vrm-portable-v1','version':'0.2.0','publication_status':'preview','game_sha256':'d'*64,
                  'files':[{'path':name,'sha256':sha(data)} for name,data in self.base_payload.items()]}
        payload={**self.base_payload,'release-manifest.json':encoded(manifest)}
        checksums=''.join(sha(data)+'  '+name+'\n' for name,data in sorted(payload.items())).encode()
        with zipfile.ZipFile(self.base,'w') as archive:
            for name,data in {**payload,'SHA256SUMS.txt':checksums}.items():archive.writestr('Accepted/'+name,data)
            if extra:archive.writestr(extra[0],extra[1])
        self.base_hash=sha(self.base.read_bytes());self.module.ACCEPTED_BASE_SHA256=self.base_hash

    def refresh_pins(self):
        inputs=[{'path':name,'sha256':sha((self.source/name).read_bytes())} for name in self.module.required_source_inputs(self.info)]
        self.pins.write_bytes(encoded({'schema':'enshrouded-vrm-source-pins-v1','inputs':inputs}))

    def build(self,**changes):
        arguments=dict(base_package=self.base,base_sha256=self.base_hash,source_root=self.source,output=self.output,
                       runtime_dll=self.runtime,runtime_sha256=sha(self.runtime.read_bytes()),bootstrap_dll=self.bootstrap,
                       bootstrap_sha256=sha(self.bootstrap.read_bytes()),shader_manifest=self.shader_manifest,source_pins=self.pins)
        arguments.update(changes);return self.module.build_release(**arguments)

    def test_owned_prefix_annotations_require_exact_catalog_rows(self):
        row=json.loads(self.base_payload['Compatibility/catalog.json'])['descriptor_signatures'][0]
        rule={'palette_joint_ids':[16,17],'policy':'owned-prefix-v1'}
        self.module.OWNED_PREFIX_CATALOG_COUNT=1
        self.module.OWNED_PREFIX_RULES=((0,sha(self.module.encoded(row)),rule),)
        self.build()
        with zipfile.ZipFile(self.output) as z:
            value=json.loads(z.read('EnshroudedVRM/Compatibility/catalog.json'))
            self.assertEqual(value['descriptor_signatures'][0]['owner_partial_palette'],rule)
        self.module.OWNED_PREFIX_RULES=((0,'0'*64,rule),)
        with self.assertRaises(ValueError):self.build(output=self.folder/'changed-row.zip')

    def test_geometry_importer_is_independently_bound(self):
        dependency='src/import_surface_geometry.inl'
        self.assertIn(dependency,self.module.required_source_inputs(self.info))
        path=self.source/dependency
        original=path.read_bytes()
        path.write_bytes(original+b' changed geometry implementation')
        with self.assertRaises(ValueError):self.build()
        path.write_bytes(original)
        path.unlink()
        with self.assertRaises((ValueError,OSError)):self.build()
        path.write_bytes(original)
        pins=json.loads(self.pins.read_bytes())
        pins['inputs']=[item for item in pins['inputs'] if item['path']!=dependency]
        self.pins.write_bytes(encoded(pins))
        with self.assertRaises(ValueError):self.build()

    def test_exact_layout_manifest_receipt_and_determinism(self):
        self.build();other=self.folder/'other.zip';self.build(output=other)
        self.assertEqual(self.output.read_bytes(),other.read_bytes())
        with zipfile.ZipFile(self.output) as archive:
            names=set(archive.namelist());self.assertEqual(names,set(self.module.TARGET_MEMBERS)|{'EnshroudedVRM/release-manifest.json','LEIA-ME-PT-BR.txt'})
            self.assertEqual({name.split('/')[0] for name in names},{'vulkan-1.dll','EnshroudedVRM','LEIA-ME-PT-BR.txt'})
            self.assertNotIn('powrprof.dll', names)
            self.assertEqual({name for name in names if name.lower().endswith('.vrm')},{'EnshroudedVRM/Avatars/vroid-sample.vrm'})
            self.assertFalse(any(name.lower().endswith(('.cmd','.bat','.ps1','.psm1','.exe','.wav')) or '/State/' in name for name in names))
            manifest=json.loads(archive.read('EnshroudedVRM/release-manifest.json'))
            self.assertEqual(manifest['schema'],'enshrouded-vrm-standalone-v1');self.assertEqual(manifest['publication_status'],'private-development')
            self.assertEqual({x['path']:x['sha256'] for x in manifest['files']},{name:sha(archive.read(name)) for name in self.module.TARGET_MEMBERS})
            info=json.loads(archive.read('EnshroudedVRM/BUILD-INFO.json'))
            self.assertEqual(info['basePackageSha256'],self.base_hash);self.assertNotIn('validation',info)
            self.assertEqual(info['bootstrapSha256'],sha(self.bootstrap.read_bytes()));self.assertTrue(info['sourceInputs'])

    def test_new_game_hash_is_consistent_and_capture_data_preserved(self):
        self.build()
        with zipfile.ZipFile(self.output) as archive:
            manifest=json.loads(archive.read('EnshroudedVRM/release-manifest.json'))
            expected='ce6fdba63e27744943b695921383a29133285d359e07671f6ca33a47e2147618'
            self.assertEqual(manifest['game_sha256'],expected)
            for kind,capture in [('profile','rig_capture_game'),('catalog','catalog_capture_game')]:
                before=json.loads(self.base_payload['Compatibility/'+kind+'.json'])
                after=json.loads(archive.read('EnshroudedVRM/Compatibility/'+kind+'.json'))
                self.assertEqual(after.pop(capture),before.pop('game'))
                self.assertEqual(after.pop('game')['executable_sha256'],expected)
                self.assertEqual(after,before)

    def test_portuguese_guide_is_bundled_and_hash_attributed(self):
        self.build()
        with zipfile.ZipFile(self.output) as archive:
            self.assertIn('LEIA-ME-PT-BR.txt',archive.namelist())
            content=archive.read('LEIA-ME-PT-BR.txt')
            self.assertEqual(content,(self.source/'portable/standalone_docs/LEIA-ME-PT-BR.txt').read_bytes())
            self.assertIn('portuguÃƒÂªs',content.decode('utf-8'))
            info=json.loads(archive.read('EnshroudedVRM/BUILD-INFO.json'))
            self.assertEqual(info['supplementalDocuments'],{'LEIA-ME-PT-BR.txt':sha(content)})

    def test_mixed_base_compatibility_metadata_is_declined(self):
        catalog=json.loads(self.base_payload['Compatibility/catalog.json'])
        catalog['game']['executable_sha256']='e'*64
        self.base_payload['Compatibility/catalog.json']=encoded(catalog);self.write_base()
        with self.assertRaises(ValueError):self.build()
        self.assertFalse(self.output.exists())

    def test_wrong_baseline_and_input_hashes_declined(self):
        for change in [{'base_sha256':'0'*64},{'runtime_sha256':'0'*64},{'bootstrap_sha256':'0'*64}]:
            with self.subTest(change=change),self.assertRaises(ValueError):self.build(**change)

    def test_authorized_sample_is_bundled_and_attributed(self):
        self.build()
        with zipfile.ZipFile(self.output) as archive:
            self.assertIn('EnshroudedVRM/Avatars/vroid-sample.vrm',archive.namelist(),'authorized sample missing from model catalogue')
            self.assertEqual(archive.read('EnshroudedVRM/Avatars/vroid-sample.vrm'),self.sample)
            info=json.loads(archive.read('EnshroudedVRM/BUILD-INFO.json'))
            self.assertEqual(info['bundledAvatars'],1)
            self.assertEqual(info['bundledAvatar']['author'],'Lightnill')
            self.assertEqual(info['bundledAvatar']['sha256'],sha(archive.read('EnshroudedVRM/Avatars/vroid-sample.vrm')))

    def test_modified_or_missing_sample_is_declined(self):
        sample=self.source/self.module.SAMPLE_SOURCE;sample.write_bytes(self.sample+b'changed')
        with self.assertRaises(ValueError):self.build()
        sample.unlink()
        with self.assertRaises(OSError):self.build()
        self.assertFalse(self.output.exists())

    def test_explicit_release_version_metadata(self):
        self.build(version='0.3.0',publication_status='preview')
        with zipfile.ZipFile(self.output) as archive:
            for name in ['EnshroudedVRM/BUILD-INFO.json','EnshroudedVRM/release-manifest.json']:
                metadata=json.loads(archive.read(name))
                self.assertEqual(metadata['version'],'0.3.0')
                self.assertEqual(metadata['publication_status'],'preview')
            self.assertEqual(json.loads(archive.read('EnshroudedVRM/BUILD-INFO.json'))['nativeMetadataVersion'],'0.4.0')
            self.assertEqual(json.loads(archive.read('EnshroudedVRM/BUILD-INFO.json'))['bootstrapMetadataVersion'],'0.2.0-standalone-dev')

    def test_unknown_publication_status_is_declined(self):
        with self.assertRaises(ValueError):self.build(publication_status='unreviewed-publication')
        self.assertFalse(self.output.exists())

    def test_mutated_missing_or_extra_source_pin_declined(self):
        original=self.pins.read_bytes();pins=json.loads(original)
        for change in [dict(pins,inputs=pins['inputs'][:-1]),dict(pins,inputs=pins['inputs']+[{'path':'private.wav','sha256':'0'*64}]),
                       dict(pins,inputs=pins['inputs']+[pins['inputs'][0]])]:
            self.pins.write_bytes(encoded(change))
            with self.assertRaises(ValueError):self.build()
        self.pins.write_bytes(original)
        source=self.source/self.module.required_source_inputs(self.info)[0];source.write_bytes(b'mutated')
        with self.assertRaises(ValueError):self.build()

    def test_base_traversal_extra_private_and_link_members_declined(self):
        for name in ['Accepted/../outside.txt','Accepted/private.wav','Accepted/Runtime/kernel32.dll']:
            self.write_base((name,b'not allowed'))
            with self.assertRaises(ValueError):self.build()
        self.write_base()
        with zipfile.ZipFile(self.base,'a') as archive:
            entry=zipfile.ZipInfo('Accepted/notices/link.txt');entry.create_system=3;entry.external_attr=(stat.S_IFLNK|0o777)<<16;archive.writestr(entry,b'../private')
        self.base_hash=sha(self.base.read_bytes());self.module.ACCEPTED_BASE_SHA256=self.base_hash
        with self.assertRaises(ValueError):self.build()

    def test_source_or_binary_hardlink_declined(self):
        link=self.folder/'linked.dll';os.link(self.runtime,link)
        try:
            with self.assertRaises(ValueError):self.build(runtime_dll=link)
        finally:link.unlink()

    def test_partial_shader_set_or_mutation_declined(self):
        value=json.loads(self.shader_manifest.read_bytes());original=encoded(value)
        value['modules'].pop();self.shader_manifest.write_bytes(encoded(value))
        with self.assertRaises(ValueError):self.build()
        self.shader_manifest.write_bytes(original);(self.shaders/self.module.SHADER_NAMES[0]).write_bytes(b'bad shader')
        with self.assertRaises(ValueError):self.build()

    def test_current_fog_pin_is_required_and_old_shader_declined(self):
        name='native_volume_snapshot.comp.spv';original=self.base_payload['Runtime/shaders/'+name]
        current=original+b'\0\0\0\0'
        self.module.OWNED_SHADER_SHA256={name:sha(current)}
        value=json.loads(self.shader_manifest.read_bytes())
        row=next(r for r in value['modules'] if r['name']==name)
        (self.shaders/name).write_bytes(current);row['sha256']=sha(current)
        self.shader_manifest.write_bytes(encoded(value));self.build()
        self.output.unlink()
        (self.shaders/name).write_bytes(original);row['sha256']=sha(original)
        self.shader_manifest.write_bytes(encoded(value))
        with self.assertRaises(ValueError):self.build()

    def test_system_dll_or_executable_cannot_be_bootstrap(self):
        self.bootstrap.write_bytes(pe('Microsoft Windows Power Profile'))
        with self.assertRaises(ValueError):self.build()
        self.bootstrap.write_bytes(b'not a DLL')
        with self.assertRaises(ValueError):self.build()

    def test_existing_output_preserved_and_private_source_not_walked(self):
        self.output.write_bytes(b'existing')
        with self.assertRaises(FileExistsError):self.build()
        self.assertEqual(self.output.read_bytes(),b'existing');self.output.unlink()
        (self.source/'private.wav').write_bytes(b'never read or include')
        self.build()
        with zipfile.ZipFile(self.output) as archive:self.assertFalse(any('private' in name for name in archive.namelist()))

    def test_real_native_and_python_member_contracts_match(self):
        import re
        names=set(re.findall(r'^\s*"([^"\r\n]+)"', (ROOT/'src/standalone_package_members.hpp').read_text(),re.MULTILINE))
        self.assertEqual(names,set(self.module.TARGET_MEMBERS));self.assertEqual(len(names),50)

    def test_baseline_checksum_sidecar_is_verified(self):
        with zipfile.ZipFile(self.base) as archive:payload={name:archive.read(name) for name in archive.namelist()}
        key='Accepted/SHA256SUMS.txt';payload[key]=b'0'*64+payload[key][64:]
        with zipfile.ZipFile(self.base,'w') as archive:
            for name,data in payload.items():archive.writestr(name,data)
        self.base_hash=sha(self.base.read_bytes());self.module.ACCEPTED_BASE_SHA256=self.base_hash
        with self.assertRaises(ValueError):self.build()


if __name__=='__main__':unittest.main()
