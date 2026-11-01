"""Developer-only Microsoft bootstrap build; never a player launcher."""
from pathlib import Path
import ctypes
import importlib.util
import json
import os
import shutil
import subprocess

_spec = importlib.util.spec_from_file_location('bootstrap_exports', Path(__file__).with_name('generate_exports.py'))
_exports = importlib.util.module_from_spec(_spec); _spec.loader.exec_module(_exports)


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
    if not all(path.is_file() for path in tools.values()):
        raise ValueError('Microsoft x64 C++ tools (cl, link, lib) are required for the bootstrap')
    resource_compiler = os.environ.get('EVRM_BOOTSTRAP_RESOURCE_COMPILER') or shutil.which('rc.exe')
    if not resource_compiler or not Path(resource_compiler).is_file():
        raise ValueError('Windows SDK resource compiler required (rc.exe)')
    if not os.environ.get('INCLUDE') or not os.environ.get('LIB'):
        raise ValueError('Microsoft developer INCLUDE and LIB paths must be configured')
    output.mkdir(parents=True, exist_ok=True)
    definition = output/'vulkan-1.def'
    definition.write_text(_exports.definition(inventory, provider_module), encoding='utf-8', newline='\n')
    dll, library, metadata = output/'vulkan-1.dll', output/'vulkan-1.lib', output/'exports.json'
    metadata.write_text(json.dumps(inventory, indent=2, sort_keys=True)+'\n', encoding='utf-8')
    (output/'standalone_bootstrap_manifest.hpp').write_text(
        '#pragma once\nstatic constexpr unsigned char expected_manifest[] = {'+', '.join(str(x) for x in manifest)+'};\n',encoding='ascii')
    env = dict(os.environ, PATH=str(compiler)+os.pathsep+os.environ.get('PATH',''))
    commands = []
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
    run([tools['lib'], '/NOLOGO', '/MACHINE:X64', '/Brepro', '/DEF:'+str(definition), '/OUT:'+str(library)])
    run([tools['link'], '/NOLOGO', '/DLL', '/MACHINE:X64', '/INCREMENTAL:NO', '/OPT:REF', '/OPT:ICF',
         '/DYNAMICBASE', '/HIGHENTROPYVA', '/NXCOMPAT', '/BASE:0x180000000', '/Brepro',
         '/OUT:'+str(dll), '/MAP:'+str(output/'vulkan-1.map'), *objects, output/'vulkan-1.exp', resource, 'kernel32.lib'])
    actual = _exports.export_inventory(dll)
    expected = [(entry['ordinal'], entry['names'], provider_module+'.'+(entry['names'][0] if entry['names'] else '#'+str(entry['ordinal']))) for entry in inventory['exports']]
    if [(entry['ordinal'], entry['names'], entry.get('forwarder')) for entry in actual['exports']] != expected:
        raise ValueError('emitted bootstrap export names, ordinals or provider targets differ')
    (output/'toolchain.json').write_text(json.dumps({'compilerFamily':'Microsoft', 'providerModule':provider_module,
        'resourceCompiler':str(resource_compiler), 'commands':commands},indent=2)+'\n',encoding='utf-8')
    return {'dll':dll, 'import_library':library, 'inventory':metadata}
