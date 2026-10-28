"""Synthetic release-builder integration tests; no game or private inputs."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile


CLI = Path(__file__).resolve().parents[1] / 'portable/package_script_release.py'
SHADERS = (
    'avatar.frag.spv', 'avatar.vert.spv', 'avatar_native.pull.gbuffer.spv',
    'avatar_native.pull.oit.spv', 'avatar_native.pull.vert.spv',
    'avatar_native.pull.visibility.spv', 'avatar_native.shadow.frag.spv',
    'avatar_native.shadow.vert.spv', 'native_menu_prepare.comp.spv',
    'native_prepare.comp.spv', 'native_skinning.comp.spv',
    'native_skinning.finalize.spv', 'native_volume_snapshot.comp.spv',
)
SCRIPT_NAMES = ('Launch-EnshroudedVRM.cmd', 'Launch-EnshroudedVRM.ps1', 'launcher_core.psm1')
LOCAL_FILES = {
    **{name: 'portable/script/' + name for name in SCRIPT_NAMES},
    'README.txt': 'portable/docs/SCRIPT-LAUNCHER.txt',
    'Avatars/README.txt': 'portable/docs/Avatars-README.txt',
    'KNOWN-ISSUES.txt': 'portable/docs/KNOWN-ISSUES.txt',
    'RELEASE-NOTES.md': 'portable/docs/RELEASE-NOTES.md',
    'SECURITY.md': 'portable/docs/SECURITY.md',
    'THIRD_PARTY_NOTICES.md': 'portable/docs/THIRD_PARTY_NOTICES.md',
    'LICENSE': 'LICENSE',
    **{'notices/' + name: 'portable/notices/' + name for name in (
        'GCC-RUNTIME-EXCEPTION.txt', 'GPL-3.0.txt', 'LGPL-2.1.txt',
        'MinGW-w64-runtime.txt', 'ShroudForge-MIT.txt', 'TinyGLTF-MIT.txt',
        'Vulkan-Headers-MIT.txt', 'nlohmann-json-MIT.txt', 'stb-image-MIT.txt',
    )},
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')


class ScriptReleaseTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.folder = Path(self.temp.name)
        self.source = self.folder / 'source'
        self.source.mkdir()
        self.local = {}
        for member, relative in LOCAL_FILES.items():
            path = self.source / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            self.local[member] = ('Local synthetic source: ' + relative + '\n').encode('utf-8')
            path.write_bytes(self.local[member])
        (self.source / 'unlisted-private.txt').write_bytes(b'must not enter the release')
        self.preserved = {
            'Runtime/enshrouded_vrm_runtime.dll': b'synthetic frozen runtime',
            'Runtime/enshrouded_vrm_layer.json': b'{"synthetic":"layer"}',
            'Compatibility/profile.json': b'{"synthetic":"profile"}',
            'Compatibility/catalog.json': b'{"synthetic":"catalog"}',
            **{'Runtime/shaders/' + name: ('frozen shader ' + name).encode('utf-8') for name in SHADERS},
        }
        self.base = self.folder / 'accepted.zip'
        self.output = self.folder / 'script.zip'
        self.write_base()

    def write_base(self, missing=None, tampered=None, extras=(), metadata=None):
        manifest = {
            'schema': 'enshrouded-vrm-portable-v1', 'version': '0.1.0-preview',
            'publication_status': 'preview', 'game_sha256': 'd' * 64,
            'files': [{'path': name, 'sha256': sha(data)} for name, data in self.preserved.items()],
        }
        info = {
            'version': '0.1.0-preview', 'author': 'lightnill', 'originalCodeLicense': 'MIT',
            'runtimeSha256': sha(self.preserved['Runtime/enshrouded_vrm_runtime.dll']),
            'correctedEquipmentBaselineSha256': 'c' * 64,
            'preservedGripSources': {'tool_grip.cpp': 'a' * 64},
            'sourceInputs': [{'path': 'src/runtime_bridge.cpp', 'sha256': 'b' * 64}],
            'acceptedShaderModules': {name: sha(self.preserved['Runtime/shaders/' + name]) for name in SHADERS},
            'automaticBufferExports': False, 'bundledAvatars': 0,
            'menuPreviewTransparency': 'preserved synthetic behavior',
            'launcherSha256': 'e' * 64, 'launcherBuild': {'compiler': 'retired native launcher'},
            'validation': {'antivirus': 'Old frozen package scan; must not be inherited'},
        }
        info.update(metadata or {})
        with zipfile.ZipFile(self.base, 'w') as archive:
            for name, data in self.preserved.items():
                if name != missing:
                    archive.writestr('Accepted/' + name, b'tampered' if name == tampered else data)
            archive.writestr('Accepted/BUILD-INFO.json', encoded(info))
            archive.writestr('Accepted/release-manifest.json', encoded(manifest))
            archive.writestr('Accepted/EnshroudedVRM.exe', b'retired synthetic launcher')
            archive.writestr('Accepted/README.txt', b'obsolete base instructions')
            for name, data in extras:
                archive.writestr('Accepted/' + name, data)

    def command(self, output=None, base_hash=None):
        return [sys.executable, str(CLI), '--base-package', str(self.base),
                '--base-sha256', base_hash if base_hash is not None else sha(self.base.read_bytes()),
                '--source-root', str(self.source), '--output', str(output or self.output),
                '--root-name', 'Script-release']

    def run_cli(self, output=None, base_hash=None, extra=()):
        return subprocess.run(self.command(output, base_hash) + list(extra), capture_output=True,
                              text=True, encoding='utf-8', timeout=20)

    def rebuilt_inputs(self):
        runtime = self.folder / 'rebuilt.dll'
        runtime.write_bytes(b'new synthetic native runtime')
        source = self.source / 'src/runtime_bridge.cpp'
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_bytes(b'new synthetic runtime source\n')
        resource = self.source / 'portable/runtime.rc'
        resource.write_bytes(b'new synthetic version resource\n')
        return runtime, ['--runtime-dll', str(runtime), '--runtime-sha256', sha(runtime.read_bytes()),
                         '--version', '0.1.1-preview']

    def test_rebuilt_runtime_uses_fresh_hashes_version_and_source_pins(self):
        runtime, extra = self.rebuilt_inputs()
        result = self.run_cli(extra=extra)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(archive.read('Script-release/Runtime/enshrouded_vrm_runtime.dll'), runtime.read_bytes())
            info = json.loads(archive.read('Script-release/BUILD-INFO.json'))
            manifest = json.loads(archive.read('Script-release/release-manifest.json'))
            self.assertEqual(info['version'], '0.1.1-preview')
            self.assertEqual(manifest['version'], '0.1.1-preview')
            self.assertTrue(info['runtimeRebuilt'])
            self.assertEqual(info['runtimeSha256'], sha(runtime.read_bytes()))
            self.assertEqual(info['scanStatus'], 'not-scanned')
            pins = {entry['path']: entry['sha256'] for entry in info['sourceInputs']}
            self.assertEqual(pins['src/runtime_bridge.cpp'], sha(b'new synthetic runtime source\n'))
            self.assertEqual(pins['portable/runtime.rc'], sha(b'new synthetic version resource\n'))
            for name, data in self.preserved.items():
                if not name.endswith('.dll'):
                    self.assertEqual(archive.read('Script-release/' + name), data)
        receipt = json.loads(self.output.with_suffix('.receipt.json').read_bytes())
        self.assertTrue(receipt['runtimeRebuilt'])
        self.assertNotIn('Runtime/enshrouded_vrm_runtime.dll', receipt['preservedMembers'])
        self.assertEqual(len(receipt['preservedMembers']), 16)

    def test_rebuilt_runtime_requires_matching_digest(self):
        _, extra = self.rebuilt_inputs()
        extra[3] = '0' * 64
        result = self.run_cli(extra=extra)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('Runtime SHA-256', result.stderr)
        self.assertFalse(self.output.exists())

    def test_rebuilt_runtime_requires_both_file_and_digest(self):
        runtime, _ = self.rebuilt_inputs()
        for extra in (['--runtime-dll', str(runtime)], ['--runtime-sha256', 'a' * 64]):
            with self.subTest(extra=extra):
                result = self.run_cli(extra=extra)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn('together', result.stderr)
                self.assertFalse(self.output.exists())

    def test_rebuilt_runtime_rejects_unsafe_source_pin(self):
        _, extra = self.rebuilt_inputs()
        self.write_base(metadata={'sourceInputs': [{'path': '../private.txt', 'sha256': 'b' * 64}]})
        result = self.run_cli(extra=extra)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('source pin', result.stderr)
        self.assertFalse(self.output.exists())

    def test_new_version_rejects_path_characters(self):
        result = self.run_cli(extra=['--version', '../private'])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('version', result.stderr)
        self.assertFalse(self.output.exists())

    def assert_rejected(self, fragment, base_hash=None):
        result = self.run_cli(base_hash=base_hash)
        self.assertNotEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertIn(fragment.lower(), result.stderr.lower())
        self.assertFalse(self.output.exists())
        self.assertFalse(self.output.with_suffix('.receipt.json').exists())

    def test_complete_layout_uses_local_sources_and_frozen_runtime(self):
        result = self.run_cli()
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        with zipfile.ZipFile(self.output) as archive:
            payload = {name.removeprefix('Script-release/'): archive.read(name) for name in archive.namelist()}
            self.assertTrue(all(name.startswith('Script-release/') for name in archive.namelist()))
        expected = set(self.preserved) | set(self.local) | {'BUILD-INFO.json', 'release-manifest.json', 'SHA256SUMS.txt'}
        self.assertEqual(set(payload), expected)
        self.assertEqual(len(payload), 39)
        self.assertFalse(any(name.lower().endswith('.exe') for name in payload))
        for name, data in {**self.preserved, **self.local}.items():
            self.assertEqual(payload[name], data, name)
        manifest = json.loads(payload['release-manifest.json'])
        self.assertEqual(manifest['game_sha256'], 'd' * 64)
        self.assertEqual(manifest['schema'], 'enshrouded-vrm-portable-v1')
        self.assertEqual(manifest['publication_status'], 'preview')
        listed = {entry['path']: entry['sha256'] for entry in manifest['files']}
        self.assertEqual(listed, {name: sha(data) for name, data in payload.items()
                                  if name not in ('release-manifest.json', 'SHA256SUMS.txt')})
        sums = dict((line.split('  ', 1)[1], line.split('  ', 1)[0])
                    for line in payload['SHA256SUMS.txt'].decode('utf-8').splitlines())
        self.assertEqual(sums, {name: sha(data) for name, data in payload.items() if name != 'SHA256SUMS.txt'})
        info = json.loads(payload['BUILD-INFO.json'])
        self.assertEqual(info['basePackageSha256'], sha(self.base.read_bytes()))
        self.assertFalse(info['runtimeRebuilt'])
        self.assertEqual(info['scanStatus'], 'not-scanned')
        self.assertNotIn('launcherSha256', info)
        self.assertNotIn('launcherBuild', info)
        self.assertNotIn('Old frozen package scan', payload['BUILD-INFO.json'].decode('utf-8'))
        self.assertEqual({entry['path']: entry['sha256'] for entry in info['scriptSourceInputs']},
                         {'portable/script/' + name: sha(self.local[name]) for name in SCRIPT_NAMES})
        receipt = json.loads(self.output.with_suffix('.receipt.json').read_bytes())
        self.assertEqual(receipt['archiveSha256'], sha(self.output.read_bytes()))
        self.assertEqual(receipt['scanStatus'], 'not-scanned')
        self.assertFalse(receipt['published'])
        self.assertEqual(set(receipt['preservedMembers']), set(self.preserved))
        self.assertEqual(json.loads(result.stdout)['files'], 39)

    def test_required_explicit_base_hash_cannot_be_omitted(self):
        result = subprocess.run([sys.executable, str(CLI), '--base-package', str(self.base),
                                 '--output', str(self.output)], capture_output=True, text=True,
                                encoding='utf-8', timeout=20)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('--base-sha256', result.stderr)
        self.assertFalse(self.output.exists())

    def test_wrong_base_hash_fails_before_output(self):
        self.assert_rejected('base SHA-256', base_hash='0' * 64)

    def test_malformed_base_hash_fails_before_output(self):
        self.assert_rejected('SHA-256', base_hash='not-a-digest')

    def test_missing_fixed_runtime_member_rejected(self):
        self.write_base(missing='Runtime/shaders/native_skinning.comp.spv')
        self.assert_rejected('required base member')

    def test_manifest_runtime_hash_mismatch_rejected(self):
        self.write_base(tampered='Runtime/enshrouded_vrm_runtime.dll')
        self.assert_rejected('release hash')

    def test_invalid_runtime_metadata_digest_has_a_controlled_error(self):
        for value in (None, 42, 'not-a-digest'):
            with self.subTest(value=value):
                self.write_base(metadata={'runtimeSha256': value})
                result = self.run_cli()
                self.assertNotEqual(result.returncode, 0)
                self.assertNotIn('Traceback', result.stderr)
                self.assertIn('runtime SHA-256', result.stderr)
                self.assertFalse(self.output.exists())

    def test_missing_local_source_never_falls_back_to_base(self):
        (self.source / LOCAL_FILES['README.txt']).unlink()
        self.assert_rejected('local source')

    def test_unexpected_runtime_component_is_not_silently_dropped(self):
        self.write_base(extras=[('Runtime/unknown.spv', b'unsupported runtime')])
        self.assert_rejected('runtime/compatibility')

    def test_case_alias_duplicate_base_entries_rejected(self):
        self.write_base(extras=[('runtime/enshrouded_vrm_runtime.dll', b'case alias')])
        self.assert_rejected('duplicate')

    def test_repeated_builds_are_byte_identical(self):
        first = self.run_cli()
        self.assertEqual(first.returncode, 0, first.stdout + first.stderr)
        other = self.folder / 'second.zip'
        second = self.run_cli(output=other)
        self.assertEqual(second.returncode, 0, second.stdout + second.stderr)
        self.assertEqual(self.output.read_bytes(), other.read_bytes())

    def test_existing_output_is_not_overwritten(self):
        self.output.write_bytes(b'existing verified artifact')
        result = self.run_cli()
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('already exists', result.stderr)
        self.assertEqual(self.output.read_bytes(), b'existing verified artifact')


if __name__ == '__main__':
    unittest.main()
