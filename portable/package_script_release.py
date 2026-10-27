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
LOCAL_MEMBERS = {
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
BASE_BUILD_FIELDS = (
    'version', 'author', 'originalCodeLicense', 'runtimeSha256',
    'correctedEquipmentBaselineSha256', 'preservedGripSources', 'sourceInputs',
    'acceptedShaderModules', 'automaticBufferExports', 'bundledAvatars',
    'menuPreviewTransparency',
)


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


def build_release(base_package, base_sha256, source_root, output, root_name=None):
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
    payload, accepted, original_manifest = read_base(base_package, base_sha256)
    local_inputs = []
    for member, relative in LOCAL_MEMBERS.items():
        source = source_root / relative
        regular_file(source, 'Local source', 2 * 1024 * 1024)
        data = source.read_bytes()
        payload[member] = data
        local_inputs.append({'path': relative, 'archivePath': member, 'sha256': sha(data)})
    info = {field: accepted[field] for field in BASE_BUILD_FIELDS}
    info.update({
        'publicationStatus': original_manifest['publication_status'],
        'revision': 'script-release-source-build',
        'launcherKind': 'readable CMD/Windows PowerShell 5.1',
        'basePackageSha256': base_sha256,
        'runtimeRebuilt': False,
        'scanStatus': 'not-scanned',
        'scriptSourceInputs': [{'path': 'portable/script/' + name, 'sha256': sha(payload[name])} for name in SCRIPT_NAMES],
        'releaseSourceInputs': sorted(local_inputs, key=lambda entry: entry['path']),
        'validation': {
            'runtime': 'The 17 base Runtime/Compatibility members were preserved and their release hashes verified; no rebuild',
            'launcher': 'No launcher execution or game session is performed by this release builder',
            'antivirus': 'Not scanned by this build; previous scan records apply only to their exact file and archive hashes',
        },
    })
    payload['BUILD-INFO.json'] = json_bytes(info)
    manifest = {field: original_manifest[field] for field in ('schema', 'version', 'publication_status', 'game_sha256')}
    manifest['files'] = [{'path': name, 'sha256': sha(data)} for name, data in sorted(payload.items())]
    payload['release-manifest.json'] = json_bytes(manifest)
    payload['SHA256SUMS.txt'] = ''.join(sha(data) + '  ' + name + '\n' for name, data in sorted(payload.items())).encode('utf-8')
    root_name = root_name or 'EnshroudedVRM-' + accepted['version'] + '-script'
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
        'files': files, 'preservedMembers': sorted(PRESERVED_MEMBERS),
        'runtimeRebuilt': False, 'customLauncherExeIncluded': False,
        'privateAssetsIncluded': False, 'scanStatus': 'not-scanned', 'published': False,
    }
    receipt_path.write_bytes(json_bytes(receipt))
    return {'archiveSha256': receipt['archiveSha256'], 'files': len(files),
            'preservedRuntimeCompatibilityFiles': len(PRESERVED_MEMBERS),
            'runtimeRebuilt': False, 'scanStatus': 'not-scanned', 'published': False}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--base-package', required=True, help='Existing local accepted portable ZIP; never downloaded')
    parser.add_argument('--base-sha256', required=True, help='Required SHA-256 of that exact base ZIP')
    parser.add_argument('--source-root', default=Path(__file__).resolve().parents[1], help='Source checkout containing the fixed local member paths')
    parser.add_argument('--output', required=True, help='New .zip output; an adjacent .receipt.json is also written')
    parser.add_argument('--root-name', help='Single root folder inside the ZIP; defaults to EnshroudedVRM-<version>-script')
    args = parser.parse_args()
    try:
        result = build_release(args.base_package, args.base_sha256, args.source_root, args.output, args.root_name)
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        print('Script release failed: ' + str(error), file=sys.stderr)
        return 1
    print(json.dumps(result, sort_keys=True))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
