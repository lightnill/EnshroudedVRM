"""Original input bootstrap: static PE checks and bounded owned native fixtures."""
from pathlib import Path
import hashlib
import importlib.util
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'portable'))
from native_compiler import default_compiler, prepare_msvc_environment, run_compiler

INPUT_EXPORTS = [(1, ['DirectInput8Create']), (2, ['DllCanUnloadNow']),
                 (3, ['DllGetClassObject']), (4, ['DllRegisterServer']),
                 (5, ['DllUnregisterServer']), (6, ['GetdfDIJoystick'])]


def imports(path):
    data = Path(path).read_bytes()
    read = lambda fmt, offset: struct.unpack_from('<' + fmt, data, offset)
    pe = read('I', 60)[0]
    optional = pe + 24
    table = optional + read('H', pe + 20)[0]
    sections = [read('IIII', table + 40 * i + 8)
                for i in range(read('H', pe + 6)[0])]
    def offset(rva):
        for _, address, size, raw in sections:
            if address <= rva < address + size:
                return raw + rva - address
        raise ValueError('import RVA outside sections')
    def string(rva):
        start = offset(rva)
        return data[start:data.index(b'\0', start)].decode('ascii')
    directory = offset(read('I', optional + 120)[0])
    result = {}
    while True:
        lookup, _, _, name, address = read('IIIII', directory)
        if not name:
            return result
        names = []
        at = 0
        while True:
            value = read('Q', offset((lookup or address) + at * 8))[0]
            if not value:
                break
            names.append('#' + str(value & 65535) if value >> 63 else string(value + 2))
            at += 1
        result[string(name)] = names
        directory += 20


def trigger_profile(path):
    data=Path(path).read_bytes()
    read=lambda fmt,at:struct.unpack_from('<'+fmt,data,at)
    pe=read('I',60)[0];optional=pe+24;table=optional+read('H',pe+20)[0]
    sections=[read('IIII',table+40*i+8) for i in range(read('H',pe+6)[0])]
    def offset(rva):
        for _,address,size,raw in sections:
            if address<=rva<address+size:return raw+rva-address
        raise ValueError('fixture RVA outside sections')
    def text(rva):
        start=offset(rva);return data[start:data.index(b'\0',start)].decode('ascii')
    directory=offset(read('I',optional+120)[0]);slot=None
    while True:
        lookup,_,_,name,address=read('IIIII',directory)
        if not name:break
        index=0
        while True:
            item=read('Q',offset((lookup or address)+8*index))[0]
            if not item:break
            if not item>>63 and text(item+2)=='GetCommandLineW':slot=address+8*index
            index+=1
        directory+=20
    assert slot is not None
    exports=offset(read('I',optional+112)[0])
    count,functions,names,ordinals=read('IIII',exports+24)
    function=None
    for index in range(count):
        if text(read('I',offset(names+4*index))[0])=='FixtureTrustedStartup':
            ordinal=read('H',offset(ordinals+2*index))[0]
            function=read('I',offset(functions+4*ordinal))[0]
    assert function is not None
    call=None;start=offset(function)
    for index in range(96):
        if data[start+index:start+index+2]==b'\xff\x15' and function+index+6+read('i',start+index+2)[0]==slot:
            call=function+index;break
    assert call is not None
    return {'timestamp':read('I',pe+8)[0],'image_size':read('I',optional+56)[0],
            'iat_rva':slot,'return_rva':call+6,'game_sha256':hashlib.sha256(data).hexdigest()}


class InputBootstrapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if os.name != 'nt':
            raise unittest.SkipTest('Windows native fixtures')
        cls.compiler = default_compiler()
        cls.environment = prepare_msvc_environment(cls.compiler)
        parent = ROOT.parent / 'private/bootstrap-rebuild'
        parent.mkdir(parents=True, exist_ok=True)
        cls.folder = Path(tempfile.mkdtemp(prefix='input-fixtures-', dir=parent))
        spec = importlib.util.spec_from_file_location('input_fixture_exports', ROOT / 'portable/bootstrap/generate_exports.py')
        cls.exports = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(cls.exports)

    def builder(self):
        path = ROOT / 'portable/bootstrap/build_input_bootstrap.py'
        self.assertTrue(path.is_file(), 'dinput8 production builder is missing')
        spec = importlib.util.spec_from_file_location('input_bootstrap_builder', path)
        result = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(result)
        return result

    def test_input_bootstrap_build_preserves_six_named_ordinal_exports(self):
        builder = self.builder()
        first = builder.build_input_bootstrap(ROOT, self.folder / 'static', Path(self.compiler).parent)
        self.assertEqual(first['dll'].name, 'dinput8.dll')
        inventory = self.exports.export_inventory(first['dll'])
        self.assertEqual([(x['ordinal'], x['names']) for x in inventory['exports']], INPUT_EXPORTS)
        dependencies = imports(first['dll'])
        provider = r'C:\Windows\System32\dinput8.dll'
        matching = [names for name, names in dependencies.items() if name.casefold() == provider.casefold()]
        self.assertEqual(len(matching), 1, dependencies)
        self.assertEqual(set(matching[0]), {names[0] for _, names in INPUT_EXPORTS})
        self.assertNotIn('dinput8.dll', {name.casefold() for name in dependencies})
        second = builder.build_input_bootstrap(ROOT, self.folder / 'static-repeat', Path(self.compiler).parent)
        self.assertEqual(first['dll'].read_bytes(), second['dll'].read_bytes())
        receipt = json.loads((first['dll'].parent / 'toolchain.json').read_bytes())
        self.assertEqual(receipt['toolsetVersion'], '14.44.35207')
        self.assertEqual(receipt['dllSha256'], hashlib.sha256(first['dll'].read_bytes()).hexdigest())
        self.assertFalse(receipt.get('fixture', False))

    def test_input_bootstrap_avoids_unneeded_crt_startup(self):
        result = self.builder().build_input_bootstrap(ROOT, self.folder / 'bounded-entry', Path(self.compiler).parent)
        data = result['dll'].read_bytes()
        dependencies = {name for names in imports(result['dll']).values() for name in names}
        self.assertFalse(dependencies & {'FlsAlloc', 'FlsFree', 'GetEnvironmentStringsW',
                                         'LCMapStringW', 'HeapAlloc', 'GetCommandLineA'}, dependencies)
        self.assertLess(len(data), 64 * 1024, 'bounded startup must avoid general CRT setup and embedded workspace padding')
        pe = struct.unpack_from('<I', data, 60)[0]
        self.assertEqual(struct.unpack_from('<II', data, pe + 24 + 112 + 9 * 8), (0, 0),
                         'bootstrap must not require CRT TLS initialization')
        receipt = json.loads((result['dll'].parent / 'toolchain.json').read_bytes())
        self.assertEqual(receipt['entryPoint'], 'EvrmInputEntry')

    def test_input_bootstrap_rejects_runtime_initializers(self):
        builder = self.builder()
        reference = builder.build_input_bootstrap(ROOT, self.folder / 'constant-source', Path(self.compiler).parent)
        receipt = json.loads((reference['dll'].parent / 'toolchain.json').read_bytes())
        snapshot = self.folder / 'initializer-source'
        for name in receipt['sourceInputs']:
            target = snapshot / name
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(ROOT / name, target)
        source = snapshot / 'src/input_proxy.cpp'
        with source.open('a', encoding='utf-8') as stream:
            stream.write('\nnamespace { volatile DWORD ctor_tick=0; struct NeedsInit { NeedsInit() noexcept { ctor_tick=GetTickCount(); } }; NeedsInit unsupported_initializer; }\n')
        with self.assertRaisesRegex(ValueError, 'dynamic initialization'):
            builder.build_input_bootstrap(snapshot, self.folder / 'initializer-refused', Path(self.compiler).parent)

    def test_dinput_owner_and_layer_path_policy(self):
        target = self.folder / 'input_policy.exe'
        command = [self.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                   '-I' + str(ROOT / 'src'), ROOT / 'tests/input_policy.cpp',
                   ROOT / 'src/standalone_bootstrap_policy.cpp', '-o', target]
        result = run_compiler(command, cwd=ROOT, env=self.environment, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
        result = subprocess.run([str(target)], capture_output=True, text=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def compile(self, sources, target, *extra):
        command = [self.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                   '-I' + str(ROOT / 'src'), *extra, *sources, '-o', target]
        result = run_compiler(command, cwd=ROOT, env=self.environment, capture_output=True, text=True, timeout=90)
        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def clean_env(self):
        return {key: value for key, value in os.environ.items()
                if not key.startswith(('VK_', 'ENSHROUDED_VRM_', 'EVRM_EXPECT_', 'EVRM_PROBE_', 'EVRM_CHAIN_'))}

    def run_host(self, game, mode='system', provider=None, extra_env=None):
        flags = ['-municode']
        if mode == 'vulkan':
            include = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
            self.assertTrue(include, 'Explicit Vulkan headers needed for implicit-layer proof')
            flags.extend(['-DEVRM_HOST_VULKAN', '-I' + include])
        self.compile([ROOT / 'tests/input_host.cpp'], game / 'enshrouded.exe', *flags)
        command = [str(game / 'enshrouded.exe'), str(game / 'dinput8.dll'), mode]
        if provider is not None:
            command.append(str(provider))
        env = self.clean_env()
        env['LOCALAPPDATA'] = str(game / 'owned-state')
        env.update(extra_env or {})
        result = subprocess.run(command, env=env, cwd=game, capture_output=True, text=True, timeout=20)
        self.assertEqual(result.returncode, 0, result.stdout[-1500:] + result.stderr[-1500:])
        return result.stdout

    def test_real_system_imports_execute_without_recursion(self):
        builder = self.builder()
        game = self.folder / 'real-system'
        builder.build_input_bootstrap(ROOT, game, Path(self.compiler).parent)
        runtime = game / 'EnshroudedVRM/Runtime'
        runtime.mkdir(parents=True)
        (runtime / 'enshrouded_vrm_layer.json').write_bytes((ROOT / 'portable/standalone_layer.json').read_bytes())
        self.run_host(game)

    def test_layer_setup_when_system_vulkan_is_already_mapped(self):
        builder = self.builder()
        game = self.folder / 'vulkan-preloaded'
        builder.build_input_bootstrap(ROOT, game, Path(self.compiler).parent)
        runtime = game / 'EnshroudedVRM/Runtime'
        runtime.mkdir(parents=True)
        (runtime / 'enshrouded_vrm_layer.json').write_bytes((ROOT / 'portable/standalone_layer.json').read_bytes())
        self.run_host(game, 'preload')

    def test_system_input_first_characterizes_basename_and_static_import_resolution(self):
        game=self.folder/'input-first'
        self.builder().build_input_bootstrap(ROOT,game,Path(self.compiler).parent)
        runtime=game/'EnshroudedVRM/Runtime';runtime.mkdir(parents=True)
        (runtime/'enshrouded_vrm_layer.json').write_bytes((ROOT/'portable/standalone_layer.json').read_bytes())
        client=game/'input_import_client.dll'
        self.compile([ROOT/'tests/input_import_client.cpp',game/'dinput8.lib'],client,'-shared')
        output=self.run_host(game,'input-first')
        self.assertIn('system-input-first; basename/system-static-client bypass characterized; explicit root activates',output)

    def test_exact_immutable_manifest_may_be_hardlinked_but_changed_bytes_are_refused(self):
        game=self.folder/'linked-manifest'
        self.builder().build_input_bootstrap(ROOT,game,Path(self.compiler).parent)
        runtime=game/'EnshroudedVRM/Runtime';runtime.mkdir(parents=True)
        manifest=runtime/'enshrouded_vrm_layer.json'
        manifest.write_bytes((ROOT/'portable/standalone_layer.json').read_bytes())
        staging=self.folder/'manifest-staging.json';os.link(manifest,staging)
        self.run_host(game,'preload')
        # The host's decline mode checks that a missing/incorrect manifest has
        # not changed Vulkan search state, while input remains usable.
        staging.write_bytes(b'{}\n')
        self.run_host(game,'decline')

    def test_all_six_calls_and_last_error_with_owned_provider(self):
        builder = self.builder()
        game = self.folder / 'owned-provider-game'
        provider = self.folder / 'owned_input.dll'
        definition = self.folder / 'owned_input.def'
        definition.write_text('LIBRARY owned_input\nEXPORTS\n' + ''.join(
            f' {names[0]} @{ordinal}\n' for ordinal, names in INPUT_EXPORTS))
        self.compile([ROOT / 'tests/input_provider.cpp', definition], provider, '-shared')
        builder.build_input_bootstrap(ROOT, game, Path(self.compiler).parent, provider, fixture_profile={})
        runtime = game / 'EnshroudedVRM/Runtime'
        runtime.mkdir(parents=True)
        (runtime / 'enshrouded_vrm_layer.json').write_bytes((ROOT / 'portable/standalone_layer.json').read_bytes())
        self.run_host(game, 'owned', provider)

    def test_real_implicit_layer_discovery_after_system_vulkan_preload(self):
        include = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if not include:
            self.skipTest('Explicit Vulkan headers required')
        game = self.folder / 'implicit-layer'
        manifest = (json.dumps({'file_format_version': '1.2.0', 'layer': {
            'name': 'VK_LAYER_EVRM_bootstrap_probe', 'type': 'GLOBAL',
            'library_path': '.\\probe_layer.dll', 'api_version': '1.3.0',
            'implementation_version': '1', 'description': 'Owned input fixture',
            'disable_environment': {'ENSHROUDED_VRM_DISABLE': '1'}}}, indent=2) + '\n').encode('ascii')
        self.builder().build_input_bootstrap(ROOT, game, Path(self.compiler).parent,
            fixture_manifest=manifest, fixture_profile={})
        runtime = game / 'EnshroudedVRM/Runtime'
        runtime.mkdir(parents=True)
        (runtime / 'enshrouded_vrm_layer.json').write_bytes(manifest)
        definition = self.folder / 'probe.def'
        definition.write_text('LIBRARY probe_layer\nEXPORTS\n vkGetInstanceProcAddr\n vkGetDeviceProcAddr\n vkNegotiateLoaderLayerInterfaceVersion\n')
        self.compile([ROOT / 'tests/bootstrap_layer_probe.cpp', definition], runtime / 'probe_layer.dll',
                     '-shared', '-I' + include)
        marker = game / 'instance-marker.txt'
        self.run_host(game, 'vulkan', extra_env={'EVRM_PROBE_MARKER': str(marker)})
        self.assertEqual(marker.read_bytes(), b'I')

    def test_small_worker_stack_attach_and_thread_notifications(self):
        game = self.folder / 'small-stack'
        self.builder().build_input_bootstrap(ROOT, game, Path(self.compiler).parent)
        runtime = game / 'EnshroudedVRM/Runtime'
        runtime.mkdir(parents=True)
        (runtime / 'enshrouded_vrm_layer.json').write_bytes((ROOT / 'portable/standalone_layer.json').read_bytes())
        host = game / 'enshrouded.exe'
        self.compile([ROOT / 'tests/bootstrap_small_stack.cpp'], host, '-municode')
        for mode in ('load', 'notify'):
            result = subprocess.run([str(host), mode, str(game / 'dinput8.dll'), '65536', '4', 'DirectInput8Create'],
                env=self.clean_env(), capture_output=True, text=True, timeout=25)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)

    def chain_fixture(self, name, with_chain=True, *, export_names=None, provider_flags=(), profile_change=None):
        game=self.folder/name;game.mkdir()
        builder=self.builder()
        if not hasattr(self.__class__, 'chain_seed'):
            self.__class__.chain_seed=builder.build_input_bootstrap(ROOT,self.folder/'chain-seed',Path(self.compiler).parent)
        seed=self.__class__.chain_seed
        dependency=game/'input_dependency.dll'
        self.compile([ROOT/'tests/input_dependency.cpp',seed['import_library']],dependency,'-shared')
        host=game/'enshrouded.exe'
        self.compile([ROOT/'tests/input_chain_host.cpp',seed['import_library'],dependency.with_suffix('.lib')],host,'-municode')
        profile=trigger_profile(host)
        if profile_change:profile.update(profile_change)
        builder.build_input_bootstrap(ROOT,game,Path(self.compiler).parent,fixture_profile=profile)
        runtime=game/'EnshroudedVRM/Runtime';runtime.mkdir(parents=True)
        (runtime/'enshrouded_vrm_layer.json').write_bytes((ROOT/'portable/standalone_layer.json').read_bytes())
        if with_chain:
            definition=game/'chain.def'
            names=export_names or [names[0] for _,names in INPUT_EXPORTS]
            definition.write_text('LIBRARY input_chain\nEXPORTS\n'+''.join(f' {name} @{index+1}\n' for index,name in enumerate(names)))
            self.compile([ROOT/'tests/input_provider.cpp',definition],game/'dinput8.chainload.dll','-shared','-DEVRM_CHAIN_PROVIDER',*provider_flags)
        return game,host

    def run_chain(self, game, host, mode='chain', *, extra_env=None):
        marker=game/'chain-marker.txt';env=self.clean_env()
        env['EVRM_CHAIN_MARKER']=str(marker);env['LOCALAPPDATA']=str(game/'owned-state')
        env.update(extra_env or {})
        result=subprocess.run([str(host),mode],cwd=game,env=env,capture_output=True,text=True,timeout=25)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        logs=list((game/'owned-state').rglob('input-bootstrap-*.log'))
        return marker.read_bytes() if marker.exists() else b'', [path.read_text() for path in logs]

    def test_chain_loads_at_trusted_startup_outside_dependent_initialization(self):
        game,host=self.chain_fixture('deferred-chain')
        marker,logs=self.run_chain(game,host)
        self.assertEqual(marker,b'TELR')
        self.assertEqual(len(logs),1)
        self.assertIn('chain_active\nwin32=0',logs[0])

    def test_absent_chain_keeps_windows_input_and_other_initializers_working(self):
        game,host=self.chain_fixture('absent-chain',False)
        marker,logs=self.run_chain(game,host,'absent')
        self.assertEqual(marker,b'TE')
        self.assertEqual(len(logs),1)
        self.assertIn('bootstrap=ready',logs[0])
        self.assertIn('chain_absent',logs[0])

    def test_chain_missing_optional_exports_uses_system_fallback(self):
        game,host=self.chain_fixture('partial-chain',export_names=['DirectInput8Create'])
        marker,logs=self.run_chain(game,host,'partial')
        self.assertEqual(marker,b'TELR')
        self.assertIn('chain_active',logs[0])

    def test_recursive_downstream_calls_and_concurrent_input_preserve_routing(self):
        game,host=self.chain_fixture('recursive-chain',provider_flags=('-DEVRM_CHAIN_REENTER',))
        marker,logs=self.run_chain(game,host)
        self.assertEqual(marker,b'TELR')
        self.assertIn('chain_active',logs[0])

    def test_invalid_chain_image_and_wrong_architecture_keep_windows_input(self):
        for kind in ('malformed','wrong-architecture'):
            with self.subTest(kind=kind):
                game,host=self.chain_fixture(kind)
                target=game/'dinput8.chainload.dll'
                data=bytearray(target.read_bytes())
                if kind=='malformed':data[:2]=b'NO'
                else:struct.pack_into('<H',data,struct.unpack_from('<I',data,60)[0]+4,0x14c)
                target.write_bytes(data)
                marker,logs=self.run_chain(game,host,'absent')
                self.assertEqual(marker,b'TE')
                self.assertIn('chain_image_refused',logs[0])

    def test_chain_without_create_routes_other_exports_and_keeps_native_create(self):
        game,host=self.chain_fixture('missing-create',export_names=['DllCanUnloadNow','DllGetClassObject','GetdfDIJoystick'])
        marker,logs=self.run_chain(game,host,'non-create')
        self.assertEqual(marker,b'TELR')
        self.assertIn('chain_active',logs[0])

    def test_manifest_failures_are_reported_without_a_chain(self):
        for kind in ('missing','corrupt'):
            with self.subTest(kind=kind):
                game,host=self.chain_fixture('manifest-'+kind,False)
                manifest=game/'EnshroudedVRM/Runtime/enshrouded_vrm_layer.json'
                if kind=='missing':manifest.unlink()
                else:manifest.write_bytes(b'{}\n')
                marker,logs=self.run_chain(game,host,'absent')
                self.assertEqual(marker,b'TE');self.assertEqual(len(logs),1)
                self.assertIn('bootstrap=manifest_refused',logs[0])
                self.assertIn('bootstrap_win32='+('2' if kind=='missing' else '13'),logs[0])
                self.assertIn('chain_absent',logs[0])

    def test_manifest_failure_is_independent_of_successful_chaining(self):
        game,host=self.chain_fixture('manifest-bad-chain')
        (game/'EnshroudedVRM/Runtime/enshrouded_vrm_layer.json').write_bytes(b'{}\n')
        marker,logs=self.run_chain(game,host)
        self.assertEqual(marker,b'TELR');self.assertEqual(len(logs),1)
        self.assertIn('chain_active',logs[0]);self.assertIn('bootstrap=manifest_refused',logs[0])

    def test_manifest_attach_result_survives_repair_before_safe_trigger(self):
        game,host=self.chain_fixture('manifest-repaired',False)
        repair=game/'expected-manifest.json';repair.write_bytes((ROOT/'portable/standalone_layer.json').read_bytes())
        manifest=game/'EnshroudedVRM/Runtime/enshrouded_vrm_layer.json';manifest.unlink()
        marker,logs=self.run_chain(game,host,'absent',extra_env={'EVRM_CHAIN_REPAIR_MANIFEST':str(repair)})
        self.assertEqual(manifest.read_bytes(),repair.read_bytes())
        self.assertEqual(marker,b'TE');self.assertEqual(len(logs),1)
        self.assertIn('bootstrap=manifest_refused',logs[0]);self.assertIn('bootstrap_win32=2',logs[0])

    def test_game_hash_mismatch_refuses_chain_before_loading(self):
        game,host=self.chain_fixture('wrong-game-hash',profile_change={'game_sha256':'00'*32})
        marker,logs=self.run_chain(game,host,'absent')
        self.assertEqual(marker,b'TE')
        self.assertIn('game_hash_mismatch',logs[0])

    def test_unsupported_game_metadata_declines_gate(self):
        game,host=self.chain_fixture('wrong-game-metadata',profile_change={'timestamp':1})
        marker,logs=self.run_chain(game,host,'absent')
        self.assertEqual(marker,b'TE')
        self.assertEqual(logs,[])

    def test_disabled_or_missing_vrm_data_does_not_swallow_chain(self):
        for mode in ('disabled','missing'):
            with self.subTest(mode=mode):
                game,host=self.chain_fixture('vrm-'+mode)
                if mode=='missing':(game/'EnshroudedVRM/Runtime/enshrouded_vrm_layer.json').unlink()
                marker,logs=self.run_chain(game,host,extra_env={'ENSHROUDED_VRM_DISABLE':'1'} if mode=='disabled' else {})
                self.assertEqual(marker,b'TELR')
                self.assertIn('chain_active',logs[0])

    def test_bootstrap_status_never_truncates_a_linked_destination(self):
        game,host=self.chain_fixture('linked-log')
        private=game/'owned-private.txt';private.write_bytes(b'owned file must remain unchanged')
        marker,logs=self.run_chain(game,host,extra_env={'EVRM_CHAIN_LOG_ALIAS':str(private)})
        self.assertEqual(marker,b'TELR')
        self.assertEqual(private.read_bytes(),b'owned file must remain unchanged')
        self.assertEqual(logs,['owned file must remain unchanged'])

    def test_stale_pid_only_status_cannot_suppress_current_run_diagnostics(self):
        game,host=self.chain_fixture('stale-pid-status',False)
        marker,logs=self.run_chain(game,host,'absent',extra_env={'EVRM_CHAIN_STALE_PID_LOG':'1'})
        self.assertEqual(marker,b'TE')
        self.assertEqual(len(logs),2,'a previous process with this PID must not hide the new run')
        self.assertIn('input-bootstrap v1\nchain_active\nwin32=0\n',logs)
        fresh=next(log for log in logs if 'bootstrap=ready' in log)
        self.assertIn('chain_absent',fresh)

    def test_chain_missing_dependency_reports_windows_failure(self):
        game,host=self.chain_fixture('missing-chain-dependency',False)
        dependency=game/'owned_missing.dll';source=game/'missing.cpp'
        source.write_text('extern "C" __declspec(dllexport) int OwnedDependency(){return 23;}\n')
        self.compile([source],dependency,'-shared')
        source.write_text('#include <windows.h>\nextern "C" __declspec(dllimport) int OwnedDependency();\n'
                          'BOOL WINAPI DllMain(HINSTANCE,DWORD reason,void*){return reason!=DLL_PROCESS_ATTACH||OwnedDependency()==23;}\n'
                          'extern "C" __declspec(dllexport) HRESULT WINAPI DirectInput8Create(){return 0x11223344;}\n')
        self.compile([source,dependency.with_suffix('.lib')],game/'dinput8.chainload.dll','-shared')
        dependency.unlink()
        marker,logs=self.run_chain(game,host,'absent')
        self.assertEqual(marker,b'TE')
        self.assertIn('chain_load_failed\nwin32=126',logs[0])

    def test_chain_cannot_be_an_alias_of_the_root_bootstrap(self):
        game,host=self.chain_fixture('same-file-chain',False)
        os.link(game/'dinput8.dll',game/'dinput8.chainload.dll')
        marker,logs=self.run_chain(game,host,'absent')
        self.assertEqual(marker,b'TE')
        self.assertIn('chain_is_bootstrap',logs[0])

    def test_forwarding_chain_back_to_root_is_refused(self):
        game,host=self.chain_fixture('self-forwarding-chain',False)
        definition=game/'self.def'
        definition.write_text('LIBRARY owned_self\nEXPORTS\n DirectInput8Create=dinput8.DirectInput8Create\n')
        # Create ordinary PE forwarder data first, as in the legacy forwarder
        # builder. LINK with implementation objects otherwise seeks a local
        # DirectInput8Create definition on this pinned toolchain.
        for command in ([str(Path(self.compiler).with_name('lib.exe')),'/NOLOGO','/MACHINE:X64',
                         '/DEF:'+str(definition),'/OUT:'+str(game/'self.lib')],
                        [str(Path(self.compiler).with_name('link.exe')),'/NOLOGO','/DLL','/NOENTRY',
                         '/MACHINE:X64','/OUT:'+str(game/'dinput8.chainload.dll'),str(game/'self.exp')]):
            result=subprocess.run(command,env=self.environment,capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
        marker,logs=self.run_chain(game,host,'declined')
        self.assertEqual(marker,b'TE')
        self.assertIn('chain_self_reference',logs[0])


if __name__ == '__main__':
    unittest.main()
