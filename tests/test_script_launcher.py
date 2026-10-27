"""Real Windows PowerShell5.1 launcher integration, using synthetic files only."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import sys
import tempfile
import unittest

child_binary = Path(sys.argv[1]).resolve()
sys.argv = sys.argv[:1]+sys.argv[2:]
script_source = Path(__file__).resolve().parents[1]/'portable/script'
powershell = Path(os.environ['SystemRoot'])/'System32/WindowsPowerShell/v1.0/powershell.exe'
script_names = ['Launch-EnshroudedVRM.cmd','Launch-EnshroudedVRM.ps1','launcher_core.psm1']

class ScriptLaunchTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.base = Path(self.temp.name)
        self.root = self.base/'portable & ! 模型'
        self.root.mkdir()
        self.game = self.base/'game & ! 模型'/'enshrouded.exe'
        self.game.parent.mkdir()
        shutil.copyfile(child_binary,self.game)
        self.model = self.base/'test avatar モデル.vrm'
        self.model.write_bytes(struct.pack('<III',0x46546c67,2,12))
        self.marker = self.base/'child marker.bin'
        self.files = []
        for name in script_names:
            source = script_source/name
            if source.exists():
                shutil.copyfile(source,self.root/name)
                self.add_member(name)
        for name in ['Runtime/enshrouded_vrm_runtime.dll','Runtime/enshrouded_vrm_layer.json','Compatibility/profile.json','Compatibility/catalog.json']:
            file = self.root/name
            file.parent.mkdir(parents=True,exist_ok=True)
            file.write_bytes(b'synthetic no-graphics fixture')
            self.add_member(name)
        self.manifest = {'schema':'enshrouded-vrm-portable-v1','version':'synthetic','publication_status':'preview','game_sha256':self.hash(self.game),'files':self.files}
        self.save_manifest()

    def hash(self,path):return hashlib.sha256(path.read_bytes()).hexdigest()
    def add_member(self,name):self.files.append({'path':name,'sha256':self.hash(self.root/name)})
    def save_manifest(self):(self.root/'release-manifest.json').write_text(json.dumps(self.manifest))

    def run_script(self,*args):
        env = os.environ.copy()
        env['PORTABLE_TEST_MARKER'] = str(self.marker)
        env['ENSHROUDED_VRM_CAPTURE_PLAYER_ANCHOR'] = '1'
        env['ENSHROUDED_VRM_TRACE_COMMANDS'] = '1'
        run = subprocess.run([str(powershell),'-NoLogo','-NoProfile','-File',str(self.root/script_names[1]),*args,'-Game',str(self.game),'-Model',str(self.model)],cwd=self.base,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=40)
        rows = []
        for line in (run.stdout+'\n'+run.stderr).splitlines():
            try:rows.append(json.loads(line.lstrip('\ufeff')))
            except ValueError:pass
        result = next((r for r in reversed(rows) if isinstance(r,dict) and 'gameStarted' in r),{})
        return run,result

    def test_check_accepts_unicode_relocation_without_creating_state(self):
        run,result = self.run_script('-Check')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('ok'),run.stdout+run.stderr)
        self.assertFalse(result['gameStarted'])
        self.assertFalse(result['captureFiles'])
        self.assertFalse((self.root/'State').exists())
        self.assertFalse(self.marker.exists())

    def test_actual_unicode_child_round_trip(self):
        run,result = self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('gameStarted'),run.stdout+run.stderr)
        data = self.marker.read_bytes() if self.marker.exists() else b''
        self.assertEqual(len(data),4,'Expected one child with capture/trace exports disabled')
        self.assertEqual(result['processId'],struct.unpack('<I',data)[0])
        self.assertEqual(result['publicationStatus'],'preview')
        self.assertEqual(result['runtimeSha256'],self.hash(self.root/'Runtime/enshrouded_vrm_runtime.dll'))

    def assert_rejected(self,fragment):
        run,result = self.run_script('-Check')
        self.assertNotEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertFalse(result.get('ok',True),run.stdout+run.stderr)
        self.assertFalse(result.get('gameStarted',True),run.stdout+run.stderr)
        self.assertIn(fragment.lower(),result.get('error','').lower())
        self.assertFalse(self.marker.exists())
        self.assertFalse((self.root/'State').exists())

    def test_tampered_runtime_rejected(self):
        (self.root/'Runtime/enshrouded_vrm_runtime.dll').write_bytes(b'tampered')
        self.assert_rejected('release hash')

    def test_unsupported_game_rejected(self):
        self.game.write_bytes(b'unsupported game build')
        self.assert_rejected('game version is not supported')

    def test_invalid_model_header_rejected(self):
        for header in [struct.pack('<III',0x46546c67,1,12),struct.pack('<III',0x46546c67,2,13),b'short']:
            with self.subTest(header=header):
                self.model.write_bytes(header)
                self.assert_rejected('supported VRM/GLB container')

    def test_traversal_and_reserved_manifest_paths_rejected(self):
        for name in ['../outside','Runtime/../outside','a//b','a/NUL.txt','a/name. ']:
            with self.subTest(name=name):
                self.manifest['files'] = self.files+[{'path':name,'sha256':self.hash(self.game)}]
                self.save_manifest()
                self.assert_rejected('unsafe manifest entry')

    def test_duplicate_manifest_path_rejected_case_insensitively(self):
        entry = dict(self.files[-1]);entry['path'] = entry['path'].upper()
        self.manifest['files'] = self.files+[entry];self.save_manifest()
        self.assert_rejected('file twice')

    def test_missing_required_component_rejected(self):
        self.manifest['files'] = [e for e in self.files if e['path']!='Compatibility/catalog.json']
        self.save_manifest();self.assert_rejected('missing a required component')

    def test_unknown_publication_status_rejected(self):
        self.manifest['publication_status']='unknown';self.save_manifest()
        self.assert_rejected('unsupported publication status')

    def test_semicolon_package_root_rejected(self):
        new_root=self.base/'portable;ambiguous'
        self.root.rename(new_root);self.root=new_root
        self.assert_rejected('semicolon')

    def test_report_failure_preserves_started_child(self):
        report=self.base/'report-directory';report.mkdir()
        run,result=self.run_script('-Launch','-Wait','-Report',str(report))
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('gameStarted'),run.stdout+run.stderr)
        self.assertIn('reportWarning',result)
        self.assertEqual(len(self.marker.read_bytes()),4)

    def test_session_report_failure_preserves_started_child(self):
        (self.root/'State/last-session.json').mkdir(parents=True)
        run,result=self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('gameStarted'),run.stdout+run.stderr)
        self.assertIn('settingsWarning',result)
        self.assertEqual(len(self.marker.read_bytes()),4)

    def run_module(self,command,extra_env=None):
        env=os.environ.copy()
        env.update({'EVRM_TEST_MODULE':str(self.root/'launcher_core.psm1'),'EVRM_TEST_ROOT':str(self.root),'EVRM_TEST_GAME':str(self.game),'EVRM_TEST_MODEL':str(self.model)})
        env.update(extra_env or {})
        prefix="[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); $ErrorActionPreference='Stop'; Import-Module -Name $env:EVRM_TEST_MODULE -DisableNameChecking; "
        return subprocess.run([str(powershell),'-NoLogo','-NoProfile','-Command',prefix+command],cwd=self.base,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30)

    def test_child_environment_isolated_and_layer_alias_valid(self):
        run=self.run_module("$before=$env:ENSHROUDED_VRM_TRACE_COMMANDS; $p=Get-EvrmLaunchPlan $env:EVRM_TEST_ROOT $env:EVRM_TEST_GAME $env:EVRM_TEST_MODEL $false; $layer=$p.Environment['VK_LAYER_PATH'].Split(';')[0]; [ordered]@{before=$before;after=$env:ENSHROUDED_VRM_TRACE_COMMANDS;trace=$p.Environment.ContainsKey('ENSHROUDED_VRM_TRACE_COMMANDS');capture=$p.Environment['ENSHROUDED_VRM_CAPTURE_FRAMES'];springs=$p.Environment['ENSHROUDED_VRM_SPRING_BONES'];gpu=$p.Environment['ENSHROUDED_VRM_GPU_SKINNING'];tools=$p.Environment['ENSHROUDED_VRM_TOOL_SLOTS'];model=$p.Environment['ENSHROUDED_VRM_MODEL'];layers=$p.Environment['VK_INSTANCE_LAYERS'];ascii=($layer -notmatch '[^\\x00-\\x7f]');exists=[IO.Directory]::Exists($layer)}|ConvertTo-Json -Compress",{'ENSHROUDED_VRM_TRACE_COMMANDS':'1','VK_INSTANCE_LAYERS':'VK_LAYER_OTHER;VK_LAYER_LOCAL_enshrouded_vrm'})
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        result=json.loads(run.stdout.lstrip('\ufeff'))
        self.assertEqual((result['before'],result['after']),('1','1'))
        self.assertFalse(result['trace'])
        self.assertEqual((result['capture'],result['springs'],result['gpu'],result['tools']),('none','0','1','1'))
        self.assertEqual(result['model'],str(self.model))
        self.assertEqual(result['layers'],'VK_LAYER_LOCAL_enshrouded_vrm;VK_LAYER_OTHER')
        self.assertTrue(result['ascii'] and result['exists'])

    def test_steam_library_parser_modern_and_legacy(self):
        cases=[('"libraryfolders" { "0" { "path" "C:\\\\Steam" "apps" { "1203620" "1" } } "1" { "path" "D:\\\\Steam Library" } }',['C:\\Steam','D:\\Steam Library']),('"LibraryFolders" { "1" "E:\\\\Steam" }',['E:\\Steam'])]
        for text,expected in cases:
            with self.subTest(text=text):
                run=self.run_module('$p=@(Get-EvrmSteamLibraries $env:EVRM_TEST_VDF); ConvertTo-Json -InputObject $p -Compress',{'EVRM_TEST_VDF':text})
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                self.assertEqual(json.loads(run.stdout.lstrip('\ufeff')),expected)

    def test_broken_steam_library_metadata_rejected(self):
        run=self.run_module('Get-EvrmSteamLibraries $env:EVRM_TEST_VDF',{'EVRM_TEST_VDF':'"libraryfolders" { "path" "unterminated'})
        self.assertNotEqual(run.returncode,0)
        self.assertIn('unfinished string',run.stderr)

    def test_wrapper_starts_builtin_powershell_from_unicode_folder(self):
        run=subprocess.run([str(self.root/script_names[0]),'-Version'],shell=True,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=15)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertIn('script launcher by lightnill',run.stdout)

    def test_relative_paths_follow_powershell_location(self):
        run=self.run_module("Set-Location -LiteralPath ([IO.Path]::GetDirectoryName($env:EVRM_TEST_GAME)); & (Join-Path $env:EVRM_TEST_ROOT 'Launch-EnshroudedVRM.ps1') -Check -Game 'enshrouded.exe' -Model $env:EVRM_TEST_RELATIVE_MODEL",{'EVRM_TEST_RELATIVE_MODEL':os.path.relpath(self.model,self.game.parent)})
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(json.loads(run.stdout.lstrip('\ufeff'))['ok'])

    def test_report_path_follows_powershell_location(self):
        run=self.run_module("Set-Location -LiteralPath $env:EVRM_TEST_ROOT; Write-EvrmJson 'relative-report.json' @{ok=$true}")
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue((self.root/'relative-report.json').is_file())

    def test_saved_springs_can_be_enabled_again(self):
        state=self.root/'State';state.mkdir()
        (state/'settings.json').write_text(json.dumps({'game':str(self.game),'model':str(self.model),'springs':False}))
        run,result=self.run_script('-Launch','-Wait','-EnableSprings')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('gameStarted'))
        self.assertTrue(json.loads((state/'settings.json').read_text())['springs'])

    def test_unc_rejected_before_native_filesystem_access(self):
        env=os.environ.copy();env['EVRM_TEST_MODULE']=str(self.root/'launcher_core.psm1')
        command="[Console]::OutputEncoding=[Text.UTF8Encoding]::new($false); Add-Type 'namespace EvrmScript { public static class Paths { public static int Calls; public static string Resolve(string p) { Calls++; throw new System.Exception(\"unexpected filesystem access\"); } } }'; Import-Module $env:EVRM_TEST_MODULE -DisableNameChecking; try { & (Get-Module launcher_core) { Resolve-EvrmLocalPath '\\\\example.invalid\\share\\avatar.vrm' } } catch { [ordered]@{error=$_.Exception.Message;calls=[EvrmScript.Paths]::Calls}|ConvertTo-Json -Compress }"
        run=subprocess.run([str(powershell),'-NoLogo','-NoProfile','-Command',command],env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        result=json.loads(run.stdout.lstrip('\ufeff'))
        self.assertEqual(result['calls'],0,'A UNC input reached the native path opener')
        self.assertIn('local folders',result['error'])

if __name__=='__main__':unittest.main()
