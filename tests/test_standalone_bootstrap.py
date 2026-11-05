"""Isolated loader fixtures; no game, microphone, real power changes or registry writes."""
from pathlib import Path
import ctypes
import hashlib
import importlib.util
import json
import os
import shutil
import subprocess
import statistics
import sys
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'portable'))
from native_compiler import default_compiler, prepare_msvc_environment, run_compiler


class StandaloneBootstrapTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = os.environ.get('EVRM_TEST_COMPILER') or default_compiler()
        cls.bootstrap_compiler = os.environ.get('EVRM_TEST_BOOTSTRAP_COMPILER_DIRECTORY') or str(Path(cls.compiler).parent)
        if os.name != 'nt':
            raise unittest.SkipTest('Windows x64 compiler required')
        cls.compiler_environment = prepare_msvc_environment(cls.compiler)
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode() | 0x8003)
        destination = os.environ.get('EVRM_BOOTSTRAP_FIXTURE_DIRECTORY')
        if destination:
            cls.folder = Path(destination).resolve()
            cls.folder.mkdir(parents=True, exist_ok=False)
        else:
            cls.temporary = tempfile.TemporaryDirectory(prefix='evrm-bootstrap-', ignore_cleanup_errors=True)
            cls.addClassCleanup(cls.temporary.cleanup)
            cls.folder = Path(cls.temporary.name)

    def run_checked(self, command, **kwargs):
        command = [str(x) for x in command]
        compiling = Path(command[0]) == Path(self.compiler)
        kwargs.setdefault('env', self.compiler_environment)
        runner = run_compiler if compiling else subprocess.run
        result = runner(command, capture_output=True, text=True, timeout=90, **kwargs)
        if compiling:
            with (self.folder/'fixture-build.log').open('a', encoding='utf-8') as stream:
                stream.write('COMMAND '+json.dumps(result.args)+'\n'+result.stdout+result.stderr+'\n')
        self.assertEqual(result.returncode, 0, f'{command[0]} exit={result.returncode}\n{result.stdout}{result.stderr}')
        return result

    def prepare(self):
        builder = ROOT / 'portable/bootstrap/build_bootstrap.py'
        self.assertTrue(builder.is_file(), 'standalone bootstrap builder is missing')
        spec = importlib.util.spec_from_file_location('bootstrap_builder', builder)
        module = importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        provider = self.folder / 'owned_provider.dll'
        definition = self.folder / 'provider.def'
        definition.write_text('LIBRARY owned_provider\nEXPORTS\n vkEnumerateInstanceVersion @1\n vkGetInstanceProcAddr @2\n ProbeMix @9\n ProbeOrdinal @37 NONAME\n')
        self.run_checked([self.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', '-shared',
                          ROOT/'tests/bootstrap_provider.cpp', definition, '-o', provider])
        output = self.folder / 'game'
        result = module.build_bootstrap(ROOT, output, Path(self.bootstrap_compiler), provider, str(provider.with_suffix('')),
                                        fixture_manifest=self.probe_manifest())
        actual=module._exports.export_inventory(result['dll'])
        reference=module._exports.export_inventory(provider)
        self.assertEqual([(x['ordinal'],x['names']) for x in actual['exports']],[(x['ordinal'],x['names']) for x in reference['exports']])
        runtime=output/'EnshroudedVRM/Runtime';runtime.mkdir(parents=True,exist_ok=True)
        (runtime/'enshrouded_vrm_layer.json').write_bytes(self.probe_manifest())
        return module, result, output

    @staticmethod
    def probe_manifest():
        return (json.dumps({'file_format_version':'1.2.0','layer':{'name':'VK_LAYER_EVRM_bootstrap_probe','type':'GLOBAL',
                'library_path':'.\\probe_layer.dll','api_version':'1.3.0','implementation_version':'1',
                'description':'Owned standalone fixture','disable_environment':{'ENSHROUDED_VRM_DISABLE':'1'}}},indent=2)+'\n').encode('ascii')

    def vulkan_child(self, game, library):
        include = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if not include:
            self.skipTest('Vulkan headers required')
        self.run_checked([self.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', '-municode',
                          '-I'+include, ROOT/'tests/bootstrap_vulkan_child.cpp', library, '-o', game/'enshrouded.exe'])
        return game/'enshrouded.exe'

    def clean_env(self):
        return {k:v for k,v in os.environ.items() if not k.startswith(('VK_', 'ENSHROUDED_VRM_', 'EVRM_EXPECT_'))}

    def test_bounded_path_policy(self):
        self.assertTrue((ROOT/'src/standalone_bootstrap_policy.cpp').exists(), 'bootstrap policy missing')
        binary=self.folder/'policy.exe'
        self.run_checked([self.compiler, '-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',
                          ROOT/'tests/bootstrap_policy.cpp',ROOT/'src/standalone_bootstrap_policy.cpp','-o',binary])
        self.run_checked([binary])

    def test_real_system_vulkan_export_resolution(self):
        builder=ROOT/'portable/bootstrap/build_bootstrap.py'
        self.assertTrue(builder.exists(), 'bootstrap builder missing')
        spec=importlib.util.spec_from_file_location('real_bootstrap_builder',builder)
        module=importlib.util.module_from_spec(spec); spec.loader.exec_module(module)
        game=self.folder/'real'; game.mkdir(exist_ok=True)
        built=module.build_bootstrap(ROOT,game,Path(self.bootstrap_compiler),Path(os.environ['SystemRoot'])/'System32/vulkan-1.dll')
        child=self.vulkan_child(game,built['import_library'])
        inventory=json.loads(built['inventory'].read_bytes())
        names=[name for entry in inventory['exports'] for name in entry['names']]
        names += ['#'+str(entry['ordinal']) for entry in inventory['exports']]
        self.run_checked([child,'resolve'],input='\n'.join(names)+'\n',env=self.clean_env())

    def test_actual_implicit_vulkan_discovery_and_optional_failures(self):
        _,built,game=self.prepare()
        child=self.vulkan_child(game,built['import_library'])
        runtime=game/'EnshroudedVRM/Runtime';runtime.mkdir(parents=True,exist_ok=True)
        include=os.environ['EVRM_VULKAN_INCLUDE_DIRECTORY']
        layer=runtime/'probe_layer.dll'
        definition=self.folder/'probe.def'
        definition.write_text('LIBRARY probe_layer\nEXPORTS\n vkGetInstanceProcAddr\n vkGetDeviceProcAddr\n vkNegotiateLoaderLayerInterfaceVersion\n')
        self.run_checked([self.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-shared','-I'+include,
                          ROOT/'tests/bootstrap_layer_probe.cpp',definition,'-o',layer])
        manifest=json.loads(self.probe_manifest())
        path=runtime/'enshrouded_vrm_layer.json';marker=game/'marker.txt'
        env=self.clean_env();env['EVRM_PROBE_MARKER']=str(marker);env['VK_LOADER_DEBUG']='all'
        def run(expected,changes=None):
            marker.unlink(missing_ok=True)
            self.run_checked([child],env=dict(env,**(changes or {})),cwd=self.folder)
            observed=marker.read_bytes() if marker.exists() else b''
            self.assertEqual(observed,expected)
        # Missing optional manifest, then valid activation without any power API call.
        path.unlink();run(b'')
        path.write_bytes(self.probe_manifest()); run(b'ID')
        # Verify the loader can discover one named manifest, avoiding a directory
        # scan of unrelated JSON files in the runtime folder.
        dormant_child=game/'single_manifest_control.exe';shutil.copyfile(child,dormant_child)
        marker.unlink(missing_ok=True)
        self.run_checked([dormant_child],env=dict(env,VK_ADD_IMPLICIT_LAYER_PATH=str(path)))
        self.assertEqual(marker.read_bytes(),b'ID','loader does not support an individual manifest path')
        run(b'ID',{'VK_ADD_IMPLICIT_LAYER_PATH':str(path)+';'+str(path)})
        run(b'',{'ENSHROUDED_VRM_DISABLE':'1'})
        run(b'',{'VK_LOADER_LAYERS_DISABLE':'VK_LAYER_EVRM_bootstrap_probe'})
        override=game/'override';override.mkdir(exist_ok=True)
        run(b'',{'VK_IMPLICIT_LAYER_PATH':str(override)})
        original=path.read_bytes()
        invalid_cases = {
            'broken-json': b'{broken',
            'missing-dll': json.dumps(dict(manifest,layer=dict(manifest['layer'],library_path=str(runtime/'missing.dll')))).encode(),
            'invalid-dll': json.dumps(dict(manifest,layer=dict(manifest['layer'],library_path=str(runtime/'bad.dll')))).encode(),
            'wrong-required-field-type': json.dumps(dict(manifest,layer=dict(manifest['layer'],implementation_version=1))).encode(),
            'missing-required-field': json.dumps(dict(manifest,layer={k:v for k,v in manifest['layer'].items() if k != 'implementation_version'})).encode(),
        }
        for label, data in invalid_cases.items():
            with self.subTest(optional_failure=label):
                (runtime/'bad.dll').write_bytes(b'not a PE file');path.write_bytes(data);run(b'')
        path.write_bytes(original)
        # Missing or invalid runtime DLL, with the exact unmodified manifest.
        original_layer=layer.read_bytes();layer.unlink();run(b'')
        layer.write_bytes(b'invalid PE');run(b'');layer.write_bytes(original_layer)
        # An unrelated broken JSON beside the exact manifest is never exposed.
        (runtime/'unrelated.json').write_text(json.dumps(dict(manifest,layer=dict(manifest['layer'],implementation_version=1))))
        run(b'ID')
        # Inherited other implicit layer remains enabled, alongside ours exactly once.
        other=game/'other';other.mkdir(exist_ok=True)
        shutil.copyfile(layer,other/'second_layer.dll')
        (other/'second.json').write_text(json.dumps(dict(manifest,layer=dict(manifest['layer'],name='VK_LAYER_EVRM_other_probe',library_path=str(other/'second_layer.dll')))))
        run(b'IIDD',{'VK_ADD_IMPLICIT_LAYER_PATH':str(other)})

    def test_export_metadata_bounds(self):
        module, built, _ = self.prepare()
        source=Path(built['dll']).read_bytes();bad=self.folder/'bad_pe.dll'
        for value in [b'',b'MZ'+b'\0'*300,source[:128],source[:1024],source[:0x3c]+b'\xff'*4+source[0x40:]]:
            bad.write_bytes(value)
            with self.assertRaises(ValueError):module._exports.export_inventory(bad)
        for target in ['powrprof','relative/owned','C:\\test\\provider.dll','C:\\test\\bad\nprovider']:
            with self.assertRaises(ValueError):module._exports.definition({'exports':[]},target)
            with self.assertRaises(ValueError):module._exports.microsoft_export_arguments({'exports':[]},target)
        with self.assertRaises(ValueError):
            module._exports.microsoft_export_arguments({'exports':[]},'C:\\test\\comma,provider')

    def test_owned_provider_whitespace_forwarder_targets_are_exact_and_callable(self):
        module, _, _ = self.prepare()
        directory = self.folder/'owned provider with spaces'; directory.mkdir()
        provider = directory/'owned_provider.dll'
        shutil.copyfile(self.folder/'owned_provider.dll', provider)
        built = module.build_bootstrap(ROOT, self.folder/'whitespace-forwarders', Path(self.bootstrap_compiler),
                                       provider, str(provider.with_suffix('')), fixture_manifest=self.probe_manifest())
        inventory = module._exports.export_inventory(built['dll'])
        kernel = ctypes.WinDLL('kernel32', use_last_error=True)
        kernel.LoadLibraryW.argtypes = [ctypes.c_wchar_p]; kernel.LoadLibraryW.restype = ctypes.c_void_p
        kernel.FreeLibrary.argtypes = [ctypes.c_void_p]; kernel.FreeLibrary.restype = ctypes.c_int
        kernel.GetProcAddress.argtypes = [ctypes.c_void_p, ctypes.c_void_p]; kernel.GetProcAddress.restype = ctypes.c_void_p
        owned = kernel.LoadLibraryW(str(provider)); self.assertTrue(owned)
        proxy = kernel.LoadLibraryW(str(built['dll']))
        try:
            self.assertTrue(proxy)
            for entry in inventory['exports']:
                symbol = entry['names'][0] if entry['names'] else '#'+str(entry['ordinal'])
                self.assertEqual(entry['forwarder'], str(provider.with_suffix(''))+'.'+symbol)
                for name in [*entry['names'], entry['ordinal']]:
                    argument = name if isinstance(name, int) else ctypes.cast(ctypes.c_char_p(name.encode('ascii')), ctypes.c_void_p)
                    expected = kernel.GetProcAddress(owned, argument)
                    self.assertTrue(expected)
                    self.assertEqual(kernel.GetProcAddress(proxy, argument), expected)
            address = kernel.GetProcAddress(proxy, ctypes.cast(ctypes.c_char_p(b'vkEnumerateInstanceVersion'), ctypes.c_void_p))
            call = ctypes.WINFUNCTYPE(ctypes.c_ulong, ctypes.POINTER(ctypes.c_uint32), use_last_error=True)(address)
            value = ctypes.c_uint32()
            self.assertEqual(call(ctypes.byref(value)), 0xA321)
            self.assertEqual(value.value, 0x12345678)
            self.assertEqual(ctypes.get_last_error(), 0xA123)
        finally:
            if proxy: self.assertTrue(kernel.FreeLibrary(proxy))
            self.assertTrue(kernel.FreeLibrary(owned))

    def test_startup_measurement(self):
        """Paired isolated startup cost, never a game-FPS/memory measurement."""
        builder=ROOT/'portable/bootstrap/build_bootstrap.py'
        self.assertTrue(builder.exists())
        spec=importlib.util.spec_from_file_location('timed_bootstrap_builder',builder)
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        candidate=self.folder/'timed_candidate';baseline=self.folder/'timed_baseline'
        candidate.mkdir(exist_ok=True);baseline.mkdir(exist_ok=True)
        built=module.build_bootstrap(ROOT,candidate,Path(self.bootstrap_compiler),Path(os.environ['SystemRoot'])/'System32/vulkan-1.dll')
        runtime=candidate/'EnshroudedVRM/Runtime';runtime.mkdir(parents=True,exist_ok=True)
        (runtime/'enshrouded_vrm_layer.json').write_bytes((ROOT/'portable/standalone_layer.json').read_bytes())
        with_proxy=self.vulkan_child(candidate,built['import_library'])
        without_proxy=self.vulkan_child(baseline,built['import_library'])
        samples={'system_vulkan_import':[],'standalone_bootstrap':[]}
        env=self.clean_env()
        for index in range(14):
            for name,child in [('system_vulkan_import',without_proxy),('standalone_bootstrap',with_proxy)]:
                begin=time.perf_counter_ns()
                process=subprocess.Popen([str(child),'startup'],env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
                try:
                    self.assertEqual(process.stdout.readline().strip(),'startup reached')
                    elapsed=(time.perf_counter_ns()-begin)/1_000_000
                finally:
                    stdout,stderr=process.communicate('\n',timeout=15)
                self.assertEqual(process.returncode,0,stdout+stderr)
                if index >= 2:samples[name].append(elapsed)
        receipt={'schema':1,'sample_count':12,'unit':'ms','scope':'paired child creation to main/stdout; no game or runtime loaded',
                 'platform':os.sys.getwindowsversion()[:3],'vulkan_version':'1.4.350.0 (separate loader tests)',
                 'samples':samples,'medians':{name:statistics.median(values) for name,values in samples.items()},
                 'bootstrap_size_bytes':built['dll'].stat().st_size}
        receipt['median_difference_ms']=receipt['medians']['standalone_bootstrap']-receipt['medians']['system_vulkan_import']
        destination=os.environ.get('EVRM_BOOTSTRAP_EVIDENCE_DIRECTORY')
        if destination:
            Path(destination).mkdir(parents=True,exist_ok=True)
            (Path(destination)/'bootstrap-startup-measurement.json').write_text(json.dumps(receipt,indent=2)+'\n',encoding='utf-8')
        print('paired startup medians (ms):',receipt['medians'],'difference:',round(receipt['median_difference_ms'],3),flush=True)

    def test_system_dependency_surface_before_first_vulkan_use(self):
        spec=importlib.util.spec_from_file_location('surface_bootstrap',ROOT/'portable/bootstrap/build_bootstrap.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        game=self.folder/'system_surface'
        built=module.build_bootstrap(ROOT,game,Path(self.bootstrap_compiler),Path(os.environ['SystemRoot'])/'System32/vulkan-1.dll')
        runtime=game/'EnshroudedVRM/Runtime';runtime.mkdir(parents=True,exist_ok=True)
        manifest=runtime/'enshrouded_vrm_layer.json';manifest.write_bytes((ROOT/'portable/standalone_layer.json').read_bytes())
        child=game/'enshrouded.exe'
        self.run_checked([self.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-municode',
                          '-I'+os.environ['EVRM_VULKAN_INCLUDE_DIRECTORY'],
                          ROOT/'tests/bootstrap_dependency_surface.cpp',built['import_library'],
                          '-lshell32','-lcomdlg32','-ldbghelp','-ldinput8','-loleaut32','-lwldap32','-lbcrypt','-ladvapi32','-luser32','-lgdi32','-lole32','-lws2_32','-o',child])
        env=self.clean_env();env['EVRM_EXPECT_LAYER_PATH']=str(manifest)
        self.run_checked([child],env=env)
        inspected=self.run_checked([Path(self.compiler).with_name('dumpbin.exe'),'/NOLOGO','/DEPENDENTS',child]).stdout
        names=[line.strip() for line in inspected.splitlines() if line.strip().lower().endswith('.dll')]
        self.assertGreaterEqual(len(names),12)
        destination=os.environ.get('EVRM_BOOTSTRAP_EVIDENCE_DIRECTORY')
        if destination:(Path(destination)/'bootstrap-system-dependencies.json').write_text(json.dumps({'ordinaryImports':names,'powerCalls':0,'earlyDiscoveryObserved':True},indent=2)+'\n',encoding='utf-8')

    def test_bootstrap_has_project_version_metadata(self):
        _,built,_=self.prepare()
        from ctypes import wintypes
        version=ctypes.WinDLL('version.dll')
        version.GetFileVersionInfoSizeW.argtypes=[wintypes.LPCWSTR,ctypes.POINTER(wintypes.DWORD)];version.GetFileVersionInfoSizeW.restype=wintypes.DWORD
        ignored=wintypes.DWORD();size=version.GetFileVersionInfoSizeW(str(built['dll']),ctypes.byref(ignored))
        self.assertGreater(size,0,'bootstrap has no project VERSIONINFO resource')
        data=ctypes.create_string_buffer(size)
        version.GetFileVersionInfoW.argtypes=[wintypes.LPCWSTR,wintypes.DWORD,wintypes.DWORD,wintypes.LPVOID]
        self.assertTrue(version.GetFileVersionInfoW(str(built['dll']),0,size,data))
        version.VerQueryValueW.argtypes=[wintypes.LPCVOID,wintypes.LPCWSTR,ctypes.POINTER(wintypes.LPVOID),ctypes.POINTER(wintypes.UINT)]
        for name,expected in [('CompanyName','lightnill'),('ProductName','EnshroudedVRM'),('OriginalFilename','vulkan-1.dll'),
                              ('FileDescription','EnshroudedVRM standalone bootstrap'),('InternalName','EnshroudedVRM.bootstrap'),
                              ('FileVersion','0.2.0-standalone-dev'),('ProductVersion','0.2.0-standalone-dev'),
                              ('LegalCopyright','Copyright (c) 2026 lightnill; third-party notices included')]:
            pointer=wintypes.LPVOID();length=wintypes.UINT()
            self.assertTrue(version.VerQueryValueW(data,'\\StringFileInfo\\040904b0\\'+name,ctypes.byref(pointer),ctypes.byref(length)))
            self.assertEqual(ctypes.wstring_at(pointer),expected)

    def test_bootstrap_bytes_do_not_depend_on_output_folder(self):
        module,first,_=self.prepare()
        provider=self.folder/'owned_provider.dll'
        second=module.build_bootstrap(ROOT,self.folder/'different-output',Path(self.bootstrap_compiler),provider,str(provider.with_suffix('')),fixture_manifest=self.probe_manifest())
        self.assertEqual(hashlib.sha256(first['dll'].read_bytes()).hexdigest(),hashlib.sha256(second['dll'].read_bytes()).hexdigest(),'bootstrap DLL varies with output directory')

    def test_control_malformed_manifest_without_bootstrap_or_probe_dll(self):
        """Characterize the loader independently; does not turn the product gate green."""
        include=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if not include: self.skipTest('Vulkan headers required')
        folder=self.folder/'no_bootstrap';folder.mkdir(exist_ok=True)
        child=folder/'control.exe'
        self.run_checked([self.compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-municode',
                          '-DEVRM_NO_BOOTSTRAP','-I'+include,ROOT/'tests/bootstrap_vulkan_child.cpp','-o',child])
        manifest=folder/'control.json'
        layer={'name':'VK_LAYER_EVRM_control','type':'GLOBAL','library_path':str(folder/'missing_owned_fixture.dll'),
               'api_version':'1.3.0','implementation_version':'1','description':'Independent control',
               'disable_environment':{'ENSHROUDED_VRM_DISABLE':'1'}}
        env=self.clean_env();env['VK_ADD_IMPLICIT_LAYER_PATH']=str(folder);env['VK_LOADER_DEBUG']='all'
        for isolate in [False, True]:
            print('independent control; other layers disabled:',isolate,flush=True)
            if isolate:
                env['VK_LOADER_LAYERS_DISABLE']='*';env['VK_LOADER_LAYERS_ALLOW']='VK_LAYER_EVRM_control'
            manifest.write_text(json.dumps({'file_format_version':'1.2.0','layer':layer}))
            self.run_checked([child],env=env)
            for label, changed in [('wrong-type',dict(layer,implementation_version=1)),
                                   ('missing-field',{k:v for k,v in layer.items() if k!='implementation_version'})]:
                with self.subTest(control=label,other_layers_disabled=isolate):
                    manifest.write_text(json.dumps({'file_format_version':'1.2.0','layer':changed}))
                    result=subprocess.run([str(child)],capture_output=True,text=True,env=env,timeout=30)
                    self.assertEqual(result.returncode,5,result.stdout+result.stderr)
                    self.assertIn('vkCreateInstance=-1',result.stderr)

    def test_early_setup_and_abi(self):
        _, built, game = self.prepare()
        dependent = game / 'bootstrap_dependent.dll'
        self.run_checked([self.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', '-shared',
                          ROOT/'tests/bootstrap_dependent.cpp', built['import_library'],
                          '-Wl,--out-implib,'+str(game/'dependent.a'), '-o', dependent])
        child = game/'enshrouded.exe'
        self.run_checked([self.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', '-municode',
                          ROOT/'tests/bootstrap_child.cpp', built['import_library'], game/'dependent.a', '-o', child])
        runtime = game/'EnshroudedVRM/Runtime'; runtime.mkdir(parents=True, exist_ok=True)
        env = {k:v for k,v in os.environ.items() if not k.startswith(('VK_', 'ENSHROUDED_VRM_', 'EVRM_EXPECT_'))}
        manifest=runtime/'enshrouded_vrm_layer.json'
        env['EVRM_EXPECT_LAYER_PATH'] = str(manifest)
        self.run_checked([child], env=env, cwd=self.folder)
        # A reference in the IAT loads the bootstrap even when main never calls a power API.
        self.run_checked([child, 'none'], env=env, cwd=self.folder)
        env['VK_ADD_IMPLICIT_LAYER_PATH'] = 'Z:\\other layer'
        env['EVRM_EXPECT_LAYER_PATH'] = 'Z:\\other layer;' + str(manifest)
        self.run_checked([child], env=env)
        env['VK_ADD_IMPLICIT_LAYER_PATH'] = str(manifest)
        env['EVRM_EXPECT_LAYER_PATH'] = str(manifest)
        self.run_checked([child], env=env)
        for name, value in [('ENSHROUDED_VRM_DISABLE', '1'), ('VK_IMPLICIT_LAYER_PATH', 'Z:\\override')]:
            settings = dict(env, **{name:value, 'VK_ADD_IMPLICIT_LAYER_PATH':'Z:\\existing', 'EVRM_EXPECT_LAYER_PATH':'Z:\\existing'})
            self.run_checked([child], env=settings)
        for name in ['assets.exe', 'enshrouded_server.exe', 'unrelated.exe']:
            dormant = game/name; shutil.copyfile(child, dormant)
            settings = dict(env, VK_ADD_IMPLICIT_LAYER_PATH='Z:\\existing', EVRM_EXPECT_LAYER_PATH='Z:\\existing')
            self.run_checked([dormant], env=settings)

        # A missing, changed or hard-linked manifest must leave forwarding usable.
        inherited=dict(env,VK_ADD_IMPLICIT_LAYER_PATH='Z:\\existing',EVRM_EXPECT_LAYER_PATH='Z:\\existing')
        original=manifest.read_bytes();manifest.unlink();self.run_checked([child],env=inherited)
        manifest.write_bytes(b'{changed}');self.run_checked([child],env=inherited);manifest.write_bytes(original)
        linked=game/'manifest-hardlink.json';os.link(manifest,linked)
        try:self.run_checked([child],env=inherited)
        finally:linked.unlink()

        # Real Unicode/space/percent installation path, and semicolon decline.
        for leaf,activate in [('game space 日本%folder',True),('game;invalid',False)]:
            copied=self.folder/leaf;shutil.copytree(game,copied)
            copied_manifest=copied/'EnshroudedVRM/Runtime/enshrouded_vrm_layer.json'
            settings=dict(env,VK_ADD_IMPLICIT_LAYER_PATH='Z:\\existing',
                          EVRM_EXPECT_LAYER_PATH='Z:\\existing'+(';' + str(copied_manifest) if activate else ''))
            self.run_checked([copied/'enshrouded.exe'],env=settings,cwd=self.folder)

        # Normal installers must not change the checked file or its directories
        # between the precheck and Vulkan's later read.
        env.pop('VK_ADD_IMPLICIT_LAYER_PATH',None);env['EVRM_EXPECT_LAYER_PATH']=str(manifest)
        process=subprocess.Popen([str(child),'hold'],env=env,stdin=subprocess.PIPE,stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        try:
            self.assertEqual(process.stdout.readline().strip(),'holding checked manifest')
            with self.assertRaises(PermissionError): manifest.write_bytes(b'changed')
            with self.assertRaises(PermissionError): runtime.rename(runtime.with_name('renamed'))
        finally:
            stdout,stderr=process.communicate('\n',timeout=15)
            self.assertEqual(process.returncode,0,stdout+stderr)


if __name__ == '__main__':
    unittest.main()
