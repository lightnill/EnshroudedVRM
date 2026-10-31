"""Synthetic release-builder integration tests; no game or private inputs."""
import hashlib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import struct
import re
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
VOICE_INPUTS = (
    'build.ps1', 'src/local_voice_observation.cpp', 'src/local_voice_observation.hpp',
    'src/local_voice_copy.hpp', 'src/local_voice_copy.cpp',
    'src/local_voice_observer.cpp', 'src/local_voice_observer.hpp',
    'src/local_voice_pe_identity.hpp',
    'src/mouth_types.hpp', 'src/voice_clock.hpp', 'src/voice_clock.cpp',
    'src/voice_stream.hpp', 'src/voice_stream.cpp',
    'src/voice_features.hpp', 'src/voice_features.cpp',
    'src/voice_profiles.hpp', 'src/voice_profiles.cpp',
    'src/voice_analyser.hpp', 'src/voice_analyser.cpp',
    'src/voice_automatic.hpp', 'src/voice_automatic.cpp',
    'src/voice_worker.hpp', 'src/voice_worker.cpp',
    'src/voice_profile_io.hpp', 'src/voice_profile_io.cpp',
    'src/voice_input_types.hpp', 'src/voice_inputs.hpp', 'src/voice_inputs.cpp',
    'src/microphone_audio.hpp', 'src/microphone_backend.hpp', 'src/microphone_backend.cpp',
    'src/microphone_source.hpp', 'src/microphone_source.cpp', 'src/fallback_diagnostics.hpp',
    'src/mouth_data.hpp', 'src/import_mouth.inl', 'src/import_meshes.inl',
    'src/vrm_asset.hpp', 'src/vrm_asset.cpp',
    'src/mouth_deformation.hpp', 'src/mouth_deformation.cpp', 'src/native_mouth.inl',
    'src/mouth_publication.hpp', 'src/native_publication.hpp',
    'src/native_skinning.hpp', 'src/native_skinning.cpp', 'src/native_skinning_gpu.hpp', 'src/native_skinning_gpu.cpp',
    'src/avatar_renderer.cpp', 'src/native_backend.cpp', 'shaders/native_skinning.hlsl',
) + tuple('vendor/minhook/' + name for name in (
    'PROVENANCE.json', 'LICENSE.txt', 'include/MinHook.h', 'src/buffer.c', 'src/buffer.h',
    'src/hook.c', 'src/trampoline.c', 'src/trampoline.h', 'src/hde/hde64.c',
    'src/hde/hde64.h', 'src/hde/pstdint.h', 'src/hde/table64.h',
)) + tuple('vendor/oculus-lipsync/' + name for name in (
    'PROVENANCE.json', 'LICENSE.txt', 'OVRLipSync.h', 'OVRLipSync.cs',
    'THIRD_PARTY_NOTICES.txt', 'sdk-license.txt', 'audio-license.txt',
))
MENU_INPUTS = (
    'src/avatar_device_request.hpp',
    'src/avatar_runtime.hpp', 'src/avatar_runtime.cpp', 'src/avatar_switch_transaction.hpp',
    'src/native_avatar_assets.hpp', 'src/native_avatar_assets.cpp', 'src/native_avatar_material.hpp',
    'src/avatar_appearance.hpp', 'src/avatar_settings.hpp', 'src/avatar_settings.cpp',
    'src/avatar_model.hpp', 'src/avatar_model.cpp', 'src/avatar_service_types.hpp', 'src/avatar_service.hpp', 'src/avatar_service.cpp',
    'src/queue_dispatch.hpp',
    'src/runtime_hooks.inl', 'src/graphics_hooks.inl', 'src/graphics_state.hpp', 'src/graphics_state.cpp',
    'src/avatar_renderer.hpp', 'src/native_backend.hpp',
    'src/mod_settings.hpp', 'src/mod_settings.cpp', 'src/mod_settings_service.hpp', 'src/mod_settings_service.cpp',
    'src/voice_setup.hpp', 'src/voice_setup.cpp',
    'src/mod_input_state.hpp', 'src/mod_input_state.cpp', 'src/mod_input_win32.hpp', 'src/mod_input_win32.cpp',
    'src/mod_menu.hpp', 'src/mod_menu.cpp', 'src/mod_menu_font.generated.inl', 'src/mod_menu_status.hpp',
    'src/mod_menu_gpu.hpp', 'src/mod_menu_gpu.cpp', 'src/mod_overlay.hpp', 'src/mod_overlay.cpp', 'src/mod_present_lease.hpp',
) + tuple('vendor/imgui/' + name for name in (
    'README.md', 'SOURCE-PINS.json', 'BINARY-NOTICES.txt', 'LICENSE.txt',
    'imgui.cpp', 'imgui_draw.cpp', 'imgui_tables.cpp', 'imgui_widgets.cpp', 'imgui.h', 'imgui_internal.h', 'imconfig.h',
    'imstb_rectpack.h', 'imstb_textedit.h', 'imstb_truetype.h',
    'backends/imgui_impl_vulkan.cpp', 'backends/imgui_impl_vulkan.h',
    'misc/fonts/Roboto-Medium.ttf', 'misc/fonts/Apache-2.0.txt',
))
LOCAL_FILES = {
    **{name: 'portable/script/' + name for name in SCRIPT_NAMES},
    'README.txt': 'portable/docs/SCRIPT-LAUNCHER.txt',
    'Avatars/README.txt': 'portable/docs/Avatars-README.txt',
    'KNOWN-ISSUES.txt': 'portable/docs/KNOWN-ISSUES.txt',
    'RELEASE-NOTES.md': 'portable/docs/RELEASE-NOTES.md',
    'SECURITY.md': 'portable/docs/SECURITY.md',
    'THIRD_PARTY_NOTICES.md': 'portable/docs/THIRD_PARTY_NOTICES.md',
    'LICENSE': 'LICENSE',
    'MOD-SETTINGS.txt': 'portable/docs/VOICE-MENU-TEST.txt',
    'notices/Dear-ImGui-fonts.txt': 'vendor/imgui/BINARY-NOTICES.txt',
    **{'notices/' + name: 'portable/notices/' + name for name in (
        'GCC-RUNTIME-EXCEPTION.txt', 'GPL-3.0.txt', 'LGPL-2.1.txt',
        'MinGW-w64-runtime.txt', 'ShroudForge-MIT.txt', 'TinyGLTF-MIT.txt',
        'Vulkan-Headers-MIT.txt', 'nlohmann-json-MIT.txt', 'stb-image-MIT.txt', 'MinHook-BSD.txt',
    )},
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def encoded(value):
    return (json.dumps(value, indent=2) + '\n').encode('utf-8')


class ScriptReleaseTests(unittest.TestCase):
    def test_real_ui_source_font_provenance_and_binary_notices(self):
        root = CLI.parents[1]
        vendor = root / 'vendor/imgui'
        pins = json.loads((vendor / 'SOURCE-PINS.json').read_bytes())
        self.assertEqual(pins['revision'], 'f5befd2d29e66809cd1110a152e375a7f1981f06')
        self.assertEqual(len(pins['sha256']), 15)
        for name, digest in pins['sha256'].items():
            self.assertEqual(sha((vendor / name).read_bytes()), digest, name)
        font = (vendor / 'misc/fonts/Roboto-Medium.ttf').read_bytes()
        generated = (root / 'src/mod_menu_font.generated.inl').read_text()
        self.assertEqual(bytes(int(value, 16) for value in re.findall(r'0x([0-9a-f]{2})', generated)), font)
        notices = (vendor / 'BINARY-NOTICES.txt').read_text()
        for label in ('Omar Cornut', 'Tristan Grimmer', 'Sean Barrett', 'Google Inc.', 'Apache License'):
            self.assertIn(label, notices)
        self.assertIn((vendor / 'LICENSE.txt').read_text().strip(), notices)
        self.assertIn((vendor / 'misc/fonts/Apache-2.0.txt').read_text().strip(), notices)

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
        for name in VOICE_INPUTS + MENU_INPUTS:
            path = self.source / name
            path.parent.mkdir(parents=True, exist_ok=True)
            if not path.exists():
                path.write_bytes(('synthetic voice build input ' + name).encode())
        runtime = self.folder / 'rebuilt.dll'
        runtime.write_bytes(b'new synthetic native runtime')
        source = self.source / 'src/runtime_bridge.cpp'
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_bytes(b'new synthetic runtime source\n')
        resource = self.source / 'portable/runtime.rc'
        resource.write_bytes(b'new synthetic version resource\n')
        self.shader_bytes = {name: struct.pack('<6I', 0x07230203, 0x00010000, 0, 1, 0, n)
                             for n, name in enumerate(('native_skinning.comp.spv', 'native_skinning.finalize.spv'))}
        modules = []
        for name, data in self.shader_bytes.items():
            path = self.folder / name
            path.write_bytes(data)
            modules.append({'name': name, 'path': str(path), 'sha256': sha(data)})
        self.shader_manifest = self.folder / 'rebuilt-shaders.json'
        self.shader_manifest.write_bytes(encoded({'schema': 'enshrouded-vrm-rebuilt-shaders-v1', 'modules': modules}))
        dependency=self.source/'vendor/oculus-lipsync/Win64';dependency.mkdir(parents=True,exist_ok=True)
        from native_voice_fixture import stage_voice_library
        stage_voice_library(dependency)
        notice=self.source/'portable/notices/Oculus-LipSync.txt'
        notice.write_bytes((CLI.parents[1]/'portable/notices/Oculus-LipSync.txt').read_bytes())
        return runtime, ['--runtime-dll', str(runtime), '--runtime-sha256', sha(runtime.read_bytes()),
                         '--version', '0.1.1-preview', '--shader-manifest', str(self.shader_manifest)]

    def test_rebuilt_runtime_carries_pinned_automatic_voice_dependency(self):
        _,extra=self.rebuilt_inputs();result=self.run_cli(extra=extra)
        self.assertEqual(result.returncode,0,result.stderr)
        with zipfile.ZipFile(self.output) as archive:
            self.assertIn('Script-release/Runtime/OVRLipSync.dll',archive.namelist())
            self.assertEqual(sha(archive.read('Script-release/Runtime/OVRLipSync.dll')),'f20e7bcb50f1591e1c4fdf5fb16ceda876ec599c1731fd857779b977b4e585b7')
            self.assertIn('Script-release/notices/Oculus-LipSync.txt',archive.namelist())

    def test_modified_voice_dependency_is_refused(self):
        _,extra=self.rebuilt_inputs();dependency=self.source/'vendor/oculus-lipsync/Win64/OVRLipSync.dll'
        dependency.unlink();dependency.write_bytes(b'changed fixture dependency')
        result=self.run_cli(extra=extra)
        self.assertNotEqual(result.returncode,0)
        self.assertIn('Oculus LipSync DLL SHA-256',result.stderr)
        self.assertFalse(self.output.exists())

    def test_rebuilt_runtime_requires_matching_shader_pair(self):
        _, extra = self.rebuilt_inputs()
        result = self.run_cli(extra=extra[:-2])
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('shader manifest', result.stderr)
        self.assertFalse(self.output.exists())

    def test_rebuilt_shader_pair_requires_exact_names_hashes_and_format(self):
        _, extra = self.rebuilt_inputs()
        original = json.loads(self.shader_manifest.read_bytes())
        cases = []
        wrong = json.loads(encoded(original)); wrong['modules'][0]['sha256'] = '0' * 64; cases.append(wrong)
        wrong = json.loads(encoded(original)); del wrong['modules'][0]['sha256']; cases.append(wrong)
        wrong = json.loads(encoded(original)); wrong['modules'][0]['name'] = 'avatar.vert.spv'; cases.append(wrong)
        wrong = json.loads(encoded(original)); wrong['modules'] = wrong['modules'][:1]; cases.append(wrong)
        wrong = json.loads(encoded(original)); wrong['modules'][1] = wrong['modules'][0]; cases.append(wrong)
        wrong = json.loads(encoded(original)); wrong['modules'][0]['path'] = 'relative.spv'; cases.append(wrong)
        for wrong in cases:
            with self.subTest(manifest=wrong):
                self.shader_manifest.write_bytes(encoded(wrong))
                result = self.run_cli(extra=extra)
                self.assertNotEqual(result.returncode, 0, result.stdout)
                self.assertNotIn('Traceback', result.stderr)
                self.assertFalse(self.output.exists())
        bad_path = self.folder / original['modules'][0]['name']; bad_path.write_bytes(b'not a shader')
        original['modules'][0]['sha256'] = sha(bad_path.read_bytes()); self.shader_manifest.write_bytes(encoded(original))
        result = self.run_cli(extra=extra)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn('SPIR-V', result.stderr)

    def test_rebuilt_shader_pair_is_deterministic_and_omits_local_paths(self):
        _, extra = self.rebuilt_inputs()
        result = self.run_cli(extra=extra)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        second = self.folder / 'second-rebuilt.zip'
        result = self.run_cli(output=second, extra=extra)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        self.assertEqual(self.output.read_bytes(), second.read_bytes())
        with zipfile.ZipFile(self.output) as archive:
            info = archive.read('Script-release/BUILD-INFO.json')
            self.assertNotIn(str(self.folder).encode(), info)
            self.assertNotIn(str(self.folder).replace('\\', '\\\\').encode(), info)

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
            self.assertEqual(info['rebuiltShaderModules'], {name: sha(data) for name, data in self.shader_bytes.items()})
            for name, data in self.shader_bytes.items():
                self.assertEqual(archive.read('Script-release/Runtime/shaders/' + name), data)
                self.assertEqual(info['acceptedShaderModules'][name], sha(data))
            pins = {entry['path']: entry['sha256'] for entry in info['sourceInputs']}
            self.assertEqual(pins['src/runtime_bridge.cpp'], sha(b'new synthetic runtime source\n'))
            self.assertEqual(pins['portable/runtime.rc'], sha(b'new synthetic version resource\n'))
            for name in VOICE_INPUTS + MENU_INPUTS:
                self.assertEqual(pins.get(name), sha((self.source / name).read_bytes()), name)
            for name, data in self.preserved.items():
                if not name.endswith('.dll') and name.rsplit('/', 1)[-1] not in self.shader_bytes:
                    self.assertEqual(archive.read('Script-release/' + name), data)
        receipt = json.loads(self.output.with_suffix('.receipt.json').read_bytes())
        self.assertTrue(receipt['runtimeRebuilt'])
        self.assertNotIn('Runtime/enshrouded_vrm_runtime.dll', receipt['preservedMembers'])
        self.assertEqual(len(receipt['preservedMembers']), 14)

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
        self.assertEqual(len(payload), 42)
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
        self.assertEqual(json.loads(result.stdout)['files'], 42)

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
