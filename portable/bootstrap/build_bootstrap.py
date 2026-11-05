"""Developer-only Microsoft bootstrap build; never a player launcher."""
from pathlib import Path
import ctypes
from ctypes import wintypes
import hashlib
import importlib.util
import json
import os
import subprocess
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from native_compiler import MSVC_ROOT, SDK_ROOT, SDK_VERSION, prepare_msvc_environment

_spec = importlib.util.spec_from_file_location('bootstrap_exports', Path(__file__).with_name('generate_exports.py'))
_exports = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(_exports)


def _file_version(path):
    """Read the actual tool PE version without executing a PATH-selected tool."""
    version = ctypes.WinDLL('version.dll', use_last_error=True)
    version.GetFileVersionInfoSizeW.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(wintypes.DWORD)]
    version.GetFileVersionInfoSizeW.restype = wintypes.DWORD
    version.GetFileVersionInfoW.argtypes = [wintypes.LPCWSTR, wintypes.DWORD, wintypes.DWORD, wintypes.LPVOID]
    version.GetFileVersionInfoW.restype = wintypes.BOOL
    version.VerQueryValueW.argtypes = [wintypes.LPCVOID, wintypes.LPCWSTR, ctypes.POINTER(wintypes.LPVOID), ctypes.POINTER(wintypes.UINT)]
    version.VerQueryValueW.restype = wintypes.BOOL
    ignored = wintypes.DWORD()
    size = version.GetFileVersionInfoSizeW(str(path), ctypes.byref(ignored))
    if not size:
        raise ValueError('tool has no file-version resource: '+str(path))
    data = ctypes.create_string_buffer(size)
    pointer, length = wintypes.LPVOID(), wintypes.UINT()
    if not version.GetFileVersionInfoW(str(path), 0, size, data) or not version.VerQueryValueW(data, '\\', ctypes.byref(pointer), ctypes.byref(length)) or length.value < 52:
        raise ValueError('tool file-version resource unavailable: '+str(path))
    fixed = ctypes.cast(pointer, ctypes.POINTER(wintypes.DWORD))
    return '.'.join(map(str, (fixed[2] >> 16, fixed[2] & 65535, fixed[3] >> 16, fixed[3] & 65535)))


def _tool_identity(path):
    return {'path': str(path), 'fileVersion': _file_version(path),
            'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}


def system_provider():
    """Resolve the build host's trusted system path; no candidate is loaded."""
    directory = ctypes.create_unicode_buffer(32768)
    size = ctypes.windll.kernel32.GetSystemDirectoryW(directory, len(directory))
    if not size or size >= len(directory):
        raise ValueError('system directory unavailable')
    return Path(directory.value)/'vulkan-1.dll'


def build_bootstrap(source_root, output_directory, compiler_directory, reference_dll, provider_module=None, fixture_manifest=None):
    root, output, compiler = Path(source_root).resolve(), Path(output_directory).resolve(), Path(compiler_directory).resolve()
    reference = Path(reference_dll).resolve()
    inventory = _exports.export_inventory(reference)
    if provider_module is None:
        if fixture_manifest is not None:
            raise ValueError('manifest override is only for owned provider fixtures')
        accepted = json.loads((root/'portable/bootstrap/vulkan-exports.json').read_bytes())
        if inventory != accepted:
            raise ValueError('system export inventory differs from accepted metadata')
        trusted = system_provider()
        if str(reference).casefold() != str(trusted.resolve()).casefold():
            raise ValueError('production reference must be the installed system Vulkan DLL')
        provider_module = str(trusted.with_suffix(''))
    manifest = fixture_manifest if fixture_manifest is not None else (root/'portable/standalone_layer.json').read_bytes()
    if not isinstance(manifest, bytes) or not 1 <= len(manifest) <= 2048 or b'\0' in manifest:
        raise ValueError('manifest must be bounded literal bytes')
    parsed = json.loads(manifest)
    if parsed['layer']['disable_environment'] != {'ENSHROUDED_VRM_DISABLE':'1'} or not isinstance(parsed['layer']['implementation_version'],str):
        raise ValueError('invalid standalone implicit manifest')
    tools = {name:compiler/(name+'.exe') for name in ('cl','link','lib')}
    env = prepare_msvc_environment(tools['cl'])
    resource_compiler = Path(os.environ.get('EVRM_BOOTSTRAP_RESOURCE_COMPILER') or SDK_ROOT/'bin'/SDK_VERSION/'x64/rc.exe').resolve()
    if resource_compiler != (SDK_ROOT/'bin'/SDK_VERSION/'x64/rc.exe').resolve() or not resource_compiler.is_file():
        raise ValueError('pinned Windows SDK '+SDK_VERSION+' x64 resource compiler required (rc.exe)')
    tools['rc'] = resource_compiler
    identities = {name:_tool_identity(path) for name,path in tools.items()}
    if identities['cl']['fileVersion'] != '19.44.35229.0' or any(identities[name]['fileVersion'] != '14.44.35229.0' for name in ('link', 'lib')):
        raise ValueError('bootstrap requires MSVC 14.44.35207 / compiler 19.44.35229.0')
    if not identities['rc']['fileVersion'].startswith(SDK_VERSION.rsplit('.', 1)[0]+'.'):
        raise ValueError('resource compiler version differs from pinned SDK')
    output.mkdir(parents=True, exist_ok=True)
    definition = output/'vulkan-1.def'
    definition.write_text(_exports.definition(inventory, provider_module), encoding='utf-8', newline='\n')
    dll, library, metadata = output/'vulkan-1.dll', output/'vulkan-1.lib', output/'exports.json'
    metadata.write_text(json.dumps(inventory, indent=2, sort_keys=True)+'\n', encoding='utf-8')
    (output/'standalone_bootstrap_manifest.hpp').write_text(
        '#pragma once\nstatic constexpr unsigned char expected_manifest[] = {'+', '.join(str(x) for x in manifest)+'};\n',encoding='ascii')
    commands = []
    (output/'build.log').write_text('', encoding='utf-8')
    def run(command):
        command = list(map(str, command)); commands.append(command)
        result = subprocess.run(command, cwd=root, env=env, capture_output=True, text=True, timeout=120)
        with (output/'build.log').open('a', encoding='utf-8') as stream:
            stream.write('COMMAND '+json.dumps(command)+'\n'+result.stdout+result.stderr+'\n')
        if result.returncode:
            raise RuntimeError('Microsoft bootstrap build failed; see '+str(output/'build.log')+'\n'+(result.stdout+result.stderr)[-3000:])
    resource = output/'bootstrap.res'
    run([resource_compiler, '/nologo', '/fo', resource, root/'portable/bootstrap/bootstrap.rc'])
    objects = []
    for name in ('standalone_bootstrap_policy', 'standalone_bootstrap_main'):
        obj = output/(name+'.obj')
        run([tools['cl'], '/nologo', '/c', '/std:c++17', '/O2', '/MT', '/utf-8', '/W4', '/WX',
             '/GS', '/GR-', '/EHs-c-', '/D_HAS_EXCEPTIONS=0', '/DNDEBUG', '/D_ITERATOR_DEBUG_LEVEL=0',
             '/permissive-', '/Zc:__cplusplus', '/Brepro', '/I'+str(output), '/Fo'+str(obj), 'src/'+name+'.cpp'])
        objects.append(obj)
    # Prepare forwarders without implementation objects; older Microsoft tools
    # otherwise try to find local definitions for the forwarded symbols.
    # LIB does not retain whitespace inside a quoted .def forwarder target.
    # Separate /EXPORT arguments retain those owned-fixture paths exactly.
    exports = (['/DEF', '/NAME:vulkan-1.dll', *_exports.microsoft_export_arguments(inventory, provider_module)]
               if any(char.isspace() for char in provider_module) else ['/DEF:'+str(definition)])
    run([tools['lib'], '/NOLOGO', '/MACHINE:X64', '/Brepro', *exports, '/OUT:'+str(library)])
    run([tools['link'], '/NOLOGO', '/DLL', '/MACHINE:X64', '/INCREMENTAL:NO', '/OPT:REF', '/OPT:ICF',
         '/DYNAMICBASE', '/HIGHENTROPYVA', '/NXCOMPAT', '/BASE:0x180000000', '/Brepro',
         '/OUT:'+str(dll), '/MAP:'+str(output/'vulkan-1.map'), *objects, output/'vulkan-1.exp', resource, 'kernel32.lib'])
    actual = _exports.export_inventory(dll)
    expected = [(entry['ordinal'], entry['names'], provider_module+'.'+(entry['names'][0] if entry['names'] else '#'+str(entry['ordinal']))) for entry in inventory['exports']]
    if [(entry['ordinal'], entry['names'], entry.get('forwarder')) for entry in actual['exports']] != expected:
        raise ValueError('emitted bootstrap export names, ordinals or provider targets differ')
    source_names = ('portable/bootstrap/build_bootstrap.py', 'portable/bootstrap/generate_exports.py',
                    'portable/bootstrap/bootstrap.rc', 'portable/bootstrap/vulkan-exports.json',
                    'portable/native_compiler.py', 'portable/standalone_layer.json',
                    'src/standalone_bootstrap_policy.hpp', 'src/standalone_bootstrap_policy.cpp',
                    'src/standalone_bootstrap_main.cpp')
    receipt = {'schema':'enshrouded-vrm-bootstrap-build-v1', 'compilerFamily':'Microsoft',
        'toolsetVersion':MSVC_ROOT.name, 'windowsSdkVersion':SDK_VERSION, 'architecture':'x64', 'runtimeLibrary':'MT',
        'tools':identities, 'providerModule':provider_module, 'providerSha256':inventory['providerSha256'],
        'resourceCompiler':str(resource_compiler), 'commands':commands,
        'sourceInputs':{name:hashlib.sha256((root/name).read_bytes()).hexdigest() for name in source_names},
        'manifestSha256':hashlib.sha256(manifest).hexdigest(), 'exportInventory':actual,
        'dllSha256':hashlib.sha256(dll.read_bytes()).hexdigest()}
    (output/'toolchain.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
    return {'dll':dll, 'import_library':library, 'inventory':metadata}
