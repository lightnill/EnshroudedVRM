"""Original x64 input proxy builder; no game launch, system DLL copy or resolver."""
from pathlib import Path
import ctypes
import hashlib
import importlib.util
import json
import os
import struct
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_compiler import MSVC_ROOT, SDK_ROOT, SDK_VERSION, prepare_msvc_environment

_spec = importlib.util.spec_from_file_location('input_build_tools', Path(__file__).with_name('build_bootstrap.py'))
_tools = importlib.util.module_from_spec(_spec)
_spec.loader.exec_module(_tools)
_exports = _tools._exports

EXPORTS = ((1, 'DirectInput8Create'), (2, 'DllCanUnloadNow'),
           (3, 'DllGetClassObject'), (4, 'DllRegisterServer'),
           (5, 'DllUnregisterServer'), (6, 'GetdfDIJoystick'))


def system_import_object(path, provider):
    """Emit original COFF import data with a full DLL path, not provider code.

    LIB canonicalizes a .def library name to its basename. A normal COFF
    .idata descriptor avoids that self-import hazard. Windows resolves these
    named imports before our attach; the proxy has no manual/lazy resolver.
    Layout and AMD64_ADDR32NB relocations follow Microsoft's PE/COFF contract.
    """
    provider_bytes = str(provider).encode('ascii') + b'\0'
    if len(provider_bytes) > 32768:
        raise ValueError('input provider path is too long')
    strings = bytearray(b'\0\0\0\0')
    symbols = []
    def symbol(name, section, value=0):
        encoded = name.encode('ascii')
        if len(encoded) <= 8:
            field = encoded.ljust(8, b'\0')
        else:
            field = struct.pack('<II', 0, len(strings))
            strings.extend(encoded + b'\0')
        index = len(symbols)
        symbols.append(struct.pack('<8sIhHBB', field, value, section, 0, 2, 0))
        return index
    descriptor = bytearray(20)
    lookup = bytearray(8 * (len(EXPORTS) + 1))
    addresses = bytearray(len(lookup))
    names = bytearray(provider_bytes)
    symbol('__IMPORT_DESCRIPTOR_evrm_system_input', 1)
    lookup_symbol = symbol('evrm_system_input_lookup', 3)
    address_symbol = symbol('evrm_system_input_addresses', 4)
    name_symbol = symbol('evrm_system_input_name', 5)
    descriptor_relocations = [(0, lookup_symbol), (12, name_symbol), (16, address_symbol)]
    lookup_relocations, address_relocations = [], []
    for index, (_, name) in enumerate(EXPORTS):
        if len(names) % 2:
            names.append(0)
        hint = symbol('evrm_system_input_hint_' + str(index), 5, len(names))
        names.extend(b'\0\0' + name.encode('ascii') + b'\0')
        symbol('__imp_' + name, 4, index * 8)
        lookup_relocations.append((index * 8, hint))
        address_relocations.append((index * 8, hint))
    sections = [('.idata$2', descriptor, descriptor_relocations, 4),
                ('.idata$3', bytes(20), [], 4),
                ('.idata$4', lookup, lookup_relocations, 8),
                ('.idata$5', addresses, address_relocations, 8),
                ('.idata$6', names, [], 2)]
    position = 20 + 40 * len(sections)
    headers, payload = bytearray(), bytearray()
    for name, data, relocations, alignment in sections:
        raw_position = position
        payload.extend(data)
        position += len(data)
        relocation_position = position if relocations else 0
        for offset, target in relocations:
            payload.extend(struct.pack('<IIH', offset, target, 3))
            position += 10
        headers.extend(struct.pack('<8sIIIIIIHHI', name.encode('ascii'), 0, 0, len(data),
            raw_position, relocation_position, 0, len(relocations), 0,
            0xc0000040 | {2: 0x200000, 4: 0x300000, 8: 0x400000}[alignment]))
    struct.pack_into('<I', strings, 0, len(strings))
    header = struct.pack('<HHIIIHH', 0x8664, len(sections), 0, position, len(symbols), 0, 0)
    Path(path).write_bytes(header + headers + payload + b''.join(symbols) + strings)


def system_provider():
    directory = ctypes.create_unicode_buffer(32768)
    size = ctypes.windll.kernel32.GetSystemDirectoryW(directory, len(directory))
    if not size or size >= len(directory):
        raise ValueError('system directory unavailable')
    return Path(directory.value) / 'dinput8.dll'


def constant_object(path):
    data = Path(path).read_bytes()
    if len(data) < 20:
        raise ValueError('invalid bootstrap COFF object')
    machine, count = struct.unpack_from('<HH', data)
    optional = struct.unpack_from('<H', data, 16)[0]
    if machine != 0x8664 or optional or 20 + count * 40 > len(data):
        raise ValueError('expected bounded x64 bootstrap COFF object')
    for index in range(count):
        name = data[20 + index * 40:28 + index * 40].rstrip(b'\0')
        if name.startswith((b'.CRT$X', b'.tls')):
            raise ValueError('bootstrap dynamic initialization/TLS is unsupported: ' + str(path))


def build_input_bootstrap(source_root, output_directory, compiler_directory, reference_dll=None,
                          *, fixture_manifest=None, fixture_profile=None):
    root, output = Path(source_root).resolve(), Path(output_directory).resolve()
    compiler = Path(compiler_directory).resolve()
    reference = Path(reference_dll or system_provider()).resolve()
    fixture = fixture_profile is not None
    if not fixture and (fixture_manifest is not None or reference != system_provider().resolve()):
        raise ValueError('production reference must be the installed system input DLL')
    inventory = _exports.export_inventory(reference)
    shape = [(x['ordinal'], x['names']) for x in inventory['exports']]
    if shape != [(ordinal, [name]) for ordinal, name in EXPORTS]:
        raise ValueError('input provider must have the supported six-name/ordinal ABI')
    manifest = fixture_manifest if fixture_manifest is not None else (root / 'portable/standalone_layer.json').read_bytes()
    profile = fixture_profile if fixture else json.loads((root / 'portable/bootstrap/input-trigger.json').read_bytes())
    if profile:
        if set(profile) != {'timestamp', 'image_size', 'iat_rva', 'return_rva', 'game_sha256'}:
            raise ValueError('invalid input startup profile')
        if any(type(profile[key]) is not int or not 0 < profile[key] <= 0xffffffff
               for key in ('timestamp', 'image_size', 'iat_rva', 'return_rva')):
            raise ValueError('invalid input startup profile integer')
        if profile['image_size'] < 4096 or profile['iat_rva'] > profile['image_size'] - 8 or not 6 <= profile['return_rva'] < profile['image_size']:
            raise ValueError('input startup profile RVA outside image')
        digest = bytes.fromhex(profile['game_sha256'])
        if len(digest) != 32:
            raise ValueError('invalid game SHA-256')
    else:
        digest = bytes(32)
    if not isinstance(manifest, bytes) or not 1 <= len(manifest) <= 2048 or b'\0' in manifest:
        raise ValueError('manifest must be bounded literal bytes')
    parsed = json.loads(manifest)
    if parsed['layer']['disable_environment'] != {'ENSHROUDED_VRM_DISABLE': '1'}:
        raise ValueError('invalid standalone implicit manifest')
    tools = {name: compiler / (name + '.exe') for name in ('cl', 'link', 'lib')}
    environment = prepare_msvc_environment(tools['cl'])
    tools['rc'] = SDK_ROOT / 'bin' / SDK_VERSION / 'x64/rc.exe'
    identities = {name: _tools._tool_identity(path) for name, path in tools.items()}
    output.mkdir(parents=True, exist_ok=True)
    commands = []
    (output / 'build.log').write_text('', encoding='utf-8')
    def run(command):
        command = list(map(str, command))
        commands.append(command)
        result = subprocess.run(command, cwd=root, env=environment, capture_output=True, text=True, timeout=120)
        with (output / 'build.log').open('a', encoding='utf-8') as stream:
            stream.write('COMMAND ' + json.dumps(command) + '\n' + result.stdout + result.stderr + '\n')
        if result.returncode:
            raise RuntimeError('input bootstrap build failed: ' + (result.stdout + result.stderr)[-3000:])
    (output / 'standalone_bootstrap_manifest.hpp').write_text(
        '#pragma once\nstatic constexpr unsigned char expected_manifest[] = {' +
        ', '.join(map(str, manifest)) + '};\n', encoding='ascii')
    (output / 'input_trigger_profile.hpp').write_text(
        '#pragma once\n#include <cstdint>\nnamespace enshrouded::vrm::input_trigger {\n' +
        f'inline constexpr bool enabled={str(bool(profile)).lower()};\n' +
        ''.join(f'inline constexpr std::uint32_t {key}={profile.get(key, 0)}u;\n'
                for key in ('timestamp', 'image_size', 'iat_rva', 'return_rva')) +
        'inline constexpr unsigned char game_sha256[32]={' + ','.join(map(str, digest)) + '};\n}\n', encoding='ascii')
    provider_library = output / 'system-input.obj'
    system_import_object(provider_library, reference)
    definition = output / 'dinput8.def'
    definition.write_text('LIBRARY dinput8\nEXPORTS\n' +
        ''.join(f' {name}=evrm_{name} @{ordinal}\n' for ordinal, name in EXPORTS), encoding='ascii')
    resource = output / 'input-bootstrap.res'
    run([tools['rc'], '/nologo', '/fo', resource, root / 'portable/bootstrap/input-bootstrap.rc'])
    objects = []
    for name in ('standalone_bootstrap_policy', 'standalone_bootstrap_main', 'input_proxy', 'input_proxy_entry', 'input_startup'):
        obj = output / (name + '.obj')
        run([tools['cl'], '/nologo', '/c', '/std:c++17', '/O2', '/MT', '/utf-8', '/W4', '/WX',
             '/GS', '/GR-', '/EHs-c-', '/D_HAS_EXCEPTIONS=0', '/DEVRM_INPUT_PROXY',
             '/DNDEBUG', '/D_ITERATOR_DEBUG_LEVEL=0', '/permissive-', '/Zc:__cplusplus', '/Brepro',
             '/I' + str(output), '/Fo' + str(obj), 'src/' + name + '.cpp'])
        constant_object(obj)
        objects.append(obj)
    dll, library = output / 'dinput8.dll', output / 'dinput8.lib'
    run([tools['link'], '/NOLOGO', '/DLL', '/MACHINE:X64', '/INCREMENTAL:NO', '/OPT:REF', '/OPT:ICF',
         '/DYNAMICBASE', '/HIGHENTROPYVA', '/NXCOMPAT', '/Brepro', '/ENTRY:EvrmInputEntry', '/DEF:' + str(definition),
         '/OUT:' + str(dll), '/IMPLIB:' + str(library), '/MAP:' + str(output / 'dinput8.map'),
         *objects, provider_library, resource, 'kernel32.lib', 'libucrt.lib', 'libvcruntime.lib'])
    actual = _exports.export_inventory(dll)
    if [(x['ordinal'], x['names']) for x in actual['exports']] != shape:
        raise ValueError('emitted input export ABI differs')
    metadata = output / 'exports.json'
    metadata.write_text(json.dumps(actual, indent=2) + '\n', encoding='utf-8')
    names = ['portable/bootstrap/build_input_bootstrap.py', 'portable/bootstrap/input-bootstrap.rc',
             'portable/bootstrap/build_bootstrap.py', 'portable/bootstrap/generate_exports.py',
             'portable/native_compiler.py', 'portable/standalone_layer.json',
             'src/standalone_bootstrap_main.cpp', 'src/standalone_bootstrap_policy.cpp',
             'src/standalone_bootstrap_policy.hpp', 'src/standalone_bootstrap_view.hpp',
             'src/input_proxy.cpp', 'src/input_proxy.hpp', 'src/input_proxy_entry.cpp',
             'src/input_startup.cpp', 'src/input_startup.hpp', 'src/sha256.hpp', 'portable/bootstrap/input-trigger.json']
    receipt = {'schema': 'enshrouded-vrm-input-build-v1', 'fixture': fixture,
        'toolsetVersion': MSVC_ROOT.name, 'windowsSdkVersion': SDK_VERSION, 'entryPoint': 'EvrmInputEntry',
        'tools': identities, 'commands': commands, 'providerModule': str(reference),
        'providerSha256': inventory['providerSha256'], 'manifestSha256': hashlib.sha256(manifest).hexdigest(),
        'startupProfile': profile,
        'sourceInputs': {name: hashlib.sha256((root / name).read_bytes()).hexdigest() for name in names},
        'exportInventory': actual, 'dllSha256': hashlib.sha256(dll.read_bytes()).hexdigest()}
    (output / 'toolchain.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    return {'dll': dll, 'import_library': library, 'inventory': metadata}
