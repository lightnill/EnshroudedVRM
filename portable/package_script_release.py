"""Build a script release from explicit local sources and a hash-pinned base ZIP.

Only the fixed Runtime/Compatibility files are read from the base. No workspace
walks, downloads, avatar assets, State, or previous scan receipts are used.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import sys
import tempfile
import zipfile

from package_portable import safe_archive_member, write_archive


SHADER_NAMES = (
    'avatar.frag.spv', 'avatar.vert.spv', 'avatar_native.pull.gbuffer.spv',
    'avatar_native.pull.oit.spv', 'avatar_native.pull.vert.spv',
    'avatar_native.pull.visibility.spv', 'avatar_native.shadow.frag.spv',
    'avatar_native.shadow.vert.spv', 'native_menu_prepare.comp.spv',
    'native_prepare.comp.spv', 'native_skinning.comp.spv',
    'native_skinning.finalize.spv', 'native_volume_snapshot.comp.spv',
)
PRESERVED_MEMBERS = (
    'Runtime/enshrouded_vrm_runtime.dll', 'Runtime/enshrouded_vrm_layer.json',
    'Compatibility/profile.json', 'Compatibility/catalog.json',
) + tuple('Runtime/shaders/' + name for name in SHADER_NAMES)
SCRIPT_NAMES = ('Launch-EnshroudedVRM.cmd', 'Launch-EnshroudedVRM.ps1', 'launcher_core.psm1')
VOICE_RUNTIME_INPUTS = (
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
    'src/microphone_source.hpp', 'src/microphone_source.cpp',
) + tuple('vendor/minhook/' + name for name in (
    'PROVENANCE.json', 'LICENSE.txt', 'include/MinHook.h', 'src/buffer.c', 'src/buffer.h',
    'src/hook.c', 'src/trampoline.c', 'src/trampoline.h', 'src/hde/hde64.c',
    'src/hde/hde64.h', 'src/hde/pstdint.h', 'src/hde/table64.h',
)) + tuple('vendor/oculus-lipsync/' + name for name in (
    'PROVENANCE.json', 'LICENSE.txt', 'OVRLipSync.h', 'OVRLipSync.cs',
    'THIRD_PARTY_NOTICES.txt', 'sdk-license.txt', 'audio-license.txt',
))
MOUTH_RUNTIME_INPUTS = (
    'src/fallback_diagnostics.hpp',
    'src/mouth_data.hpp', 'src/import_mouth.inl', 'src/import_meshes.inl',
    'src/vrm_asset.hpp', 'src/vrm_asset.cpp',
    'src/mouth_deformation.hpp', 'src/mouth_deformation.cpp', 'src/native_mouth.inl',
    'src/mouth_publication.hpp', 'src/native_publication.hpp',
    'src/native_skinning.hpp', 'src/native_skinning.cpp', 'src/native_skinning_gpu.hpp', 'src/native_skinning_gpu.cpp',
    'src/avatar_renderer.cpp', 'src/native_backend.cpp', 'shaders/native_skinning.hlsl',
)
MENU_RUNTIME_INPUTS = (
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
REBUILT_SHADER_NAMES = frozenset(('native_skinning.comp.spv', 'native_skinning.finalize.spv'))
LOCAL_MEMBERS = {
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
BASE_BUILD_FIELDS = (
    'version', 'author', 'originalCodeLicense', 'runtimeSha256',
    'correctedEquipmentBaselineSha256', 'preservedGripSources', 'sourceInputs',
    'acceptedShaderModules', 'automaticBufferExports', 'bundledAvatars',
    'menuPreviewTransparency',
)
VOICE_SDK_SHA256 = 'f20e7bcb50f1591e1c4fdf5fb16ceda876ec599c1731fd857779b977b4e585b7'
VOICE_LOCAL_MEMBERS = {
    'Runtime/OVRLipSync.dll': 'vendor/oculus-lipsync/Win64/OVRLipSync.dll',
    'notices/Oculus-LipSync.txt': 'portable/notices/Oculus-LipSync.txt',
}


def sha(data):
    return hashlib.sha256(data).hexdigest()


def json_bytes(value):
    return (json.dumps(value, indent=2, ensure_ascii=True) + '\n').encode('utf-8')


def local_path(value, label):
    if str(value).startswith(('\\\\', '//')):
        raise ValueError(label + ' must be an explicit local path')
    return Path(os.path.abspath(value))


def regular_file(path, label, limit):
    try:
        info = path.lstat()
    except OSError as error:
        raise ValueError(label + ' is missing or unreadable: ' + str(path)) from error
    if path.is_symlink() or not stat.S_ISREG(info.st_mode) or getattr(info, 'st_file_attributes', 0) & 0x400:
        raise ValueError(label + ' must be a regular, unlinked file: ' + str(path))
    if info.st_size > limit:
        raise ValueError(label + ' exceeds the size limit: ' + str(path))


def runtime_member(name):
    return name.split('/', 1)[0].casefold() in ('runtime', 'compatibility')


def read_base(path, expected_hash):
    regular_file(path, 'Base package', 512 * 1024 * 1024)
    with path.open('rb') as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b''):
            digest.update(block)
        if digest.hexdigest() != expected_hash:
            raise ValueError('Base SHA-256 does not match the required digest')
        stream.seek(0)
        with zipfile.ZipFile(stream) as archive:
            entries = archive.infolist()
            if not entries or len(entries) > 4096:
                raise ValueError('Base package must have a bounded file list')
            by_name = {}
            seen = set()
            for entry in entries:
                key = entry.filename.casefold()
                if key in seen:
                    raise ValueError('Base package has a duplicate or case-alias member')
                seen.add(key)
                by_name[entry.filename] = entry
            roots = [name.split('/')[0] for name in by_name
                     if name.count('/') == 1 and name.endswith('/release-manifest.json')]
            if len(roots) != 1 or not safe_archive_member(roots[0] + '.txt'):
                raise ValueError('Base package must contain one portable package root')
            prefix = roots[0] + '/'
            required = set(PRESERVED_MEMBERS) | {'BUILD-INFO.json', 'release-manifest.json'}
            missing = sorted(name for name in required if prefix + name not in by_name)
            if missing:
                raise ValueError('Required base member is missing: ' + ', '.join(missing))
            runtime_names = {name[len(prefix):] for name, entry in by_name.items()
                             if name.startswith(prefix) and not entry.is_dir()
                             and runtime_member(name[len(prefix):])}
            if runtime_names != set(PRESERVED_MEMBERS):
                raise ValueError('Base Runtime/Compatibility member set is unsupported')
            selected = {}
            total = 0
            for name in sorted(required):
                entry = by_name[prefix + name]
                limit = 2 * 1024 * 1024 if name in ('BUILD-INFO.json', 'release-manifest.json') else 128 * 1024 * 1024
                kind = stat.S_IFMT(entry.external_attr >> 16)
                if entry.is_dir() or kind not in (0, stat.S_IFREG) or entry.flag_bits & 1:
                    raise ValueError('Required base member must be a regular, unencrypted file: ' + name)
                total += entry.file_size
                if entry.file_size > limit or total > 512 * 1024 * 1024:
                    raise ValueError('Required base members exceed the size limit')
                selected[name] = archive.read(entry)
    accepted = json.loads(selected.pop('BUILD-INFO.json'))
    manifest = json.loads(selected.pop('release-manifest.json'))
    if not isinstance(accepted, dict) or not isinstance(manifest, dict):
        raise ValueError('Base build metadata and manifest must be JSON objects')
    if manifest.get('schema') != 'enshrouded-vrm-portable-v1':
        raise ValueError('Base package manifest schema is unsupported')
    if manifest.get('publication_status') not in ('preview', 'local-test-only'):
        raise ValueError('Base publication status is unsupported')
    if not isinstance(manifest.get('game_sha256'), str) or not re.fullmatch('[0-9a-f]{64}', manifest['game_sha256']):
        raise ValueError('Base game SHA-256 is invalid')
    version = accepted.get('version')
    if not isinstance(version, str) or not re.fullmatch('[A-Za-z0-9][A-Za-z0-9._-]{0,63}', version) or manifest.get('version') != version:
        raise ValueError('Base build metadata and manifest versions are invalid or inconsistent')
    files = manifest.get('files')
    if not isinstance(files, list) or not 1 <= len(files) <= 128:
        raise ValueError('Base manifest must have a bounded file list')
    hashes = {}
    seen = set()
    for entry in files:
        if not isinstance(entry, dict) or not isinstance(entry.get('path'), str) or not isinstance(entry.get('sha256'), str):
            raise ValueError('Base manifest file entry is invalid')
        name = entry['path']
        if name.casefold() in seen:
            raise ValueError('Base manifest has a duplicate or case-alias member')
        seen.add(name.casefold())
        if not re.fullmatch('[0-9a-f]{64}', entry['sha256']):
            raise ValueError('Base manifest file SHA-256 is invalid')
        hashes[name] = entry['sha256']
    if {name for name in hashes if runtime_member(name)} != set(PRESERVED_MEMBERS):
        raise ValueError('Base manifest Runtime/Compatibility member set is unsupported')
    for name, data in selected.items():
        if hashes.get(name) != sha(data):
            raise ValueError('Required base member differs from its release hash: ' + name)
    for field in BASE_BUILD_FIELDS:
        if field not in accepted:
            raise ValueError('Base build metadata is missing field: ' + field)
    if not isinstance(accepted['runtimeSha256'], str) or accepted['runtimeSha256'].lower() != sha(selected['Runtime/enshrouded_vrm_runtime.dll']):
        raise ValueError('Base runtime SHA-256 is inconsistent with its build metadata')
    shaders = accepted['acceptedShaderModules']
    if not isinstance(shaders, dict) or set(shaders) != set(SHADER_NAMES):
        raise ValueError('Base shader build metadata is incomplete')
    for name in SHADER_NAMES:
        if not isinstance(shaders[name], str) or shaders[name].lower() != sha(selected['Runtime/shaders/' + name]):
            raise ValueError('Base shader SHA-256 is inconsistent with its build metadata: ' + name)
    if accepted['automaticBufferExports'] is not False or accepted['bundledAvatars'] != 0:
        raise ValueError('Base build metadata does not describe an asset-free, no-export release')
    return selected, accepted, manifest


def read_rebuilt_shaders(manifest_path):
    path = local_path(manifest_path, 'Shader manifest')
    regular_file(path, 'Shader manifest', 64 * 1024)
    try:
        manifest = json.loads(path.read_bytes())
    except RecursionError as error:
        raise ValueError('Shader manifest nesting is excessive') from error
    if (not isinstance(manifest, dict) or set(manifest) != {'schema', 'modules'} or
            manifest['schema'] != 'enshrouded-vrm-rebuilt-shaders-v1' or
            not isinstance(manifest['modules'], list) or len(manifest['modules']) != 2):
        raise ValueError('Shader manifest must name the exact rebuilt skinning pair')
    result = {}
    for item in manifest['modules']:
        if (not isinstance(item, dict) or set(item) != {'name', 'path', 'sha256'} or
                not isinstance(item['name'], str) or item['name'] not in REBUILT_SHADER_NAMES or
                item['name'] in result or not isinstance(item['path'], str) or
                not Path(item['path']).is_absolute() or not isinstance(item['sha256'], str) or
                not re.fullmatch('[0-9a-fA-F]{64}', item['sha256'])):
            raise ValueError('Shader manifest name, absolute local path or SHA-256 is invalid')
        source = local_path(item['path'], 'Rebuilt shader')
        regular_file(source, 'Rebuilt shader', 8 * 1024 * 1024)
        data = source.read_bytes()
        if sha(data) != item['sha256'].lower():
            raise ValueError('Rebuilt shader SHA-256 does not match: ' + item['name'])
        if len(data) < 20 or len(data) % 4 or data[:4] != b'\x03\x02\x23\x07':
            raise ValueError('Rebuilt shader must be a SPIR-V module: ' + item['name'])
        result[item['name']] = data
    if set(result) != REBUILT_SHADER_NAMES:
        raise ValueError('Shader manifest must name the exact rebuilt skinning pair')
    return result


def build_release(base_package, base_sha256, source_root, output, root_name=None,
                  runtime_dll=None, runtime_sha256=None, version=None, shader_manifest=None):
    if not isinstance(base_sha256, str) or not re.fullmatch('[0-9a-fA-F]{64}', base_sha256):
        raise ValueError('A complete base SHA-256 digest is required')
    base_sha256 = base_sha256.lower()
    base_package = local_path(base_package, 'Base package')
    source_root = local_path(source_root, 'Source root')
    output = local_path(output, 'Output archive')
    if output.suffix.lower() != '.zip':
        raise ValueError('Output archive must have a .zip suffix')
    receipt_path = output.with_suffix('.receipt.json')
    if output.exists() or receipt_path.exists():
        raise ValueError('Output archive or receipt already exists; choose a new output path')
    if (runtime_dll is None) != (runtime_sha256 is None):
        raise ValueError('Supply runtime DLL and runtime SHA-256 together')
    if runtime_dll is not None and shader_manifest is None:
        raise ValueError('A rebuilt runtime requires a matching shader manifest')
    if runtime_dll is None and shader_manifest is not None:
        raise ValueError('Rebuilt shaders require a rebuilt runtime DLL')
    if version is not None and (not isinstance(version, str) or
                               not re.fullmatch('[A-Za-z0-9][A-Za-z0-9._-]{0,63}', version)):
        raise ValueError('Release version is invalid')
    payload, accepted, original_manifest = read_base(base_package, base_sha256)
    rebuilt = runtime_dll is not None
    preserved = set(PRESERVED_MEMBERS)
    source_inputs = accepted['sourceInputs']
    if rebuilt:
        if not isinstance(runtime_sha256, str) or not re.fullmatch('[0-9a-fA-F]{64}', runtime_sha256):
            raise ValueError('Runtime SHA-256 is invalid')
        runtime_dll = local_path(runtime_dll, 'Runtime DLL')
        regular_file(runtime_dll, 'Runtime DLL', 128 * 1024 * 1024)
        runtime_bytes = runtime_dll.read_bytes()
        if sha(runtime_bytes) != runtime_sha256.lower():
            raise ValueError('Runtime SHA-256 does not match the supplied DLL')
        if not isinstance(source_inputs, list) or not 1 <= len(source_inputs) <= 4096:
            raise ValueError('Runtime source pins must be a bounded list')
        names = set()
        for entry in source_inputs:
            name = entry.get('path') if isinstance(entry, dict) else None
            if (not isinstance(name, str) or not safe_archive_member(name) or
                    name.casefold() in names):
                raise ValueError('Runtime source pin path is unsafe or duplicated')
            names.add(name.casefold())
        paths = [entry['path'] for entry in source_inputs]
        if 'portable/runtime.rc' not in names:
            paths.append('portable/runtime.rc')
        for name in VOICE_RUNTIME_INPUTS + MOUTH_RUNTIME_INPUTS + MENU_RUNTIME_INPUTS:
            if name.casefold() not in names:
                paths.append(name)
                names.add(name.casefold())
        source_inputs = []
        for name in sorted(paths):
            source = source_root / name
            regular_file(source, 'Runtime source pin', 8 * 1024 * 1024)
            source_inputs.append({'path': name, 'sha256': sha(source.read_bytes())})
        payload['Runtime/enshrouded_vrm_runtime.dll'] = runtime_bytes
        preserved.remove('Runtime/enshrouded_vrm_runtime.dll')
        for name, data in read_rebuilt_shaders(shader_manifest).items():
            member = 'Runtime/shaders/' + name
            payload[member] = data
            preserved.remove(member)
    local_inputs = []
    local_members = dict(LOCAL_MEMBERS)
    if rebuilt:
        local_members.update(VOICE_LOCAL_MEMBERS)
    for member, relative in local_members.items():
        source = source_root / relative
        regular_file(source, 'Local source', (64 if member == 'Runtime/OVRLipSync.dll' else 2) * 1024 * 1024)
        data = source.read_bytes()
        if member == 'Runtime/OVRLipSync.dll' and sha(data) != VOICE_SDK_SHA256:
            raise ValueError('Official Oculus LipSync DLL SHA-256 does not match')
        payload[member] = data
        local_inputs.append({'path': relative, 'archivePath': member, 'sha256': sha(data)})
    info = {field: accepted[field] for field in BASE_BUILD_FIELDS}
    info.update({
        'version': version or accepted['version'],
        'publicationStatus': original_manifest['publication_status'],
        'revision': 'script-release-source-build',
        'launcherKind': 'readable CMD/Windows PowerShell 5.1',
        'basePackageSha256': base_sha256,
        'runtimeRebuilt': rebuilt,
        'runtimeSha256': sha(payload['Runtime/enshrouded_vrm_runtime.dll']),
        'sourceInputs': source_inputs,
        'acceptedShaderModules': {name: sha(payload['Runtime/shaders/' + name]) for name in SHADER_NAMES},
        'rebuiltShaderModules': {name: sha(payload['Runtime/shaders/' + name]) for name in SHADER_NAMES
                                 if 'Runtime/shaders/' + name not in preserved},
        'scanStatus': 'not-scanned',
        'scriptSourceInputs': [{'path': 'portable/script/' + name, 'sha256': sha(payload[name])} for name in SCRIPT_NAMES],
        'releaseSourceInputs': sorted(local_inputs, key=lambda entry: entry['path']),
        'validation': {
            'runtime': ('A separately rebuilt DLL was supplied and its explicit SHA-256 verified; '
                        f'{len(preserved)} base shader/compatibility members preserved; explicit skinning shader pair verified. Source pins describe the supplied checkout; '
                        'this builder does not compile or test the DLL') if rebuilt else
                       'The 17 base Runtime/Compatibility members were preserved and their release hashes verified; no rebuild',
            'launcher': 'No launcher execution or game session is performed by this release builder',
            'antivirus': 'Not scanned by this build; previous scan records apply only to their exact file and archive hashes',
        },
    })
    if rebuilt:
        info['automaticVoiceDetector'] = {'name': 'Oculus LipSync', 'version': '1.61.0',
            'provider': 'Enhanced', 'hardwareAcceleration': False, 'sha256': VOICE_SDK_SHA256}
    payload['BUILD-INFO.json'] = json_bytes(info)
    manifest = {field: original_manifest[field] for field in ('schema', 'version', 'publication_status', 'game_sha256')}
    manifest['version'] = info['version']
    manifest['files'] = [{'path': name, 'sha256': sha(data)} for name, data in sorted(payload.items())]
    payload['release-manifest.json'] = json_bytes(manifest)
    payload['SHA256SUMS.txt'] = ''.join(sha(data) + '  ' + name + '\n' for name, data in sorted(payload.items())).encode('utf-8')
    root_name = root_name or 'EnshroudedVRM-' + info['version'] + '-script'
    with tempfile.TemporaryDirectory(prefix='enshrouded-vrm-script-release-') as folder:
        stage = Path(folder)
        members = {}
        for name, data in payload.items():
            path = stage / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)
            members[name] = path
        files = write_archive(output, root_name, members)
    receipt = {
        'archiveSha256': sha(output.read_bytes()), 'basePackageSha256': base_sha256,
        'files': files, 'preservedMembers': sorted(preserved),
        'runtimeRebuilt': rebuilt, 'customLauncherExeIncluded': False,
        'privateAssetsIncluded': False, 'scanStatus': 'not-scanned', 'published': False,
    }
    receipt_path.write_bytes(json_bytes(receipt))
    return {'archiveSha256': receipt['archiveSha256'], 'files': len(files),
            'preservedRuntimeCompatibilityFiles': len(preserved),
            'runtimeRebuilt': rebuilt, 'scanStatus': 'not-scanned', 'published': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-package', required=True, help='Existing local accepted portable ZIP; never downloaded')
    parser.add_argument('--base-sha256', required=True, help='Required SHA-256 of that exact base ZIP')
    parser.add_argument('--source-root', default=Path(__file__).resolve().parents[1], help='Source checkout containing the fixed local member paths')
    parser.add_argument('--output', required=True, help='New .zip output; an adjacent .receipt.json is also written')
    parser.add_argument('--root-name', help='Single root folder inside the ZIP; defaults to EnshroudedVRM-<version>-script')
    parser.add_argument('--runtime-dll', help='Optional separately rebuilt local DLL; requires --runtime-sha256')
    parser.add_argument('--runtime-sha256', help='Required digest of the separately rebuilt DLL')
    parser.add_argument('--version', help='Optional new release version')
    parser.add_argument('--shader-manifest', help='Required with rebuilt runtime: exact skinning shader pair, absolute paths and SHA-256 digests')
    args = parser.parse_args()
    try:
        result = build_release(args.base_package, args.base_sha256, args.source_root, args.output, args.root_name,
                               args.runtime_dll, args.runtime_sha256, args.version, args.shader_manifest)
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        print('Script release failed: ' + str(error), file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
