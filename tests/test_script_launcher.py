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
import time
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
        self.game = self.base/'game & ! %PATH% 模型'/'enshrouded.exe'
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

    def launch_environment(self):
        env = os.environ.copy()
        env['PORTABLE_TEST_MARKER'] = str(self.marker)
        env['ENSHROUDED_VRM_CAPTURE_PLAYER_ANCHOR'] = '1'
        env['ENSHROUDED_VRM_TRACE_COMMANDS'] = '1'
        return env

    def parse_result(self,run):
        rows = []
        for line in (run.stdout+'\n'+run.stderr).splitlines():
            try:rows.append(json.loads(line.lstrip('\ufeff')))
            except ValueError:pass
        result = next((r for r in reversed(rows) if isinstance(r,dict) and 'gameStarted' in r),{})
        return run,result

    def run_script(self,*args,use_paths=True):
        options = [*args]
        if use_paths:options += ['-Game',str(self.game),'-Model',str(self.model)]
        run = subprocess.run([str(powershell),'-NoLogo','-NoProfile','-File',str(self.root/script_names[1]),*options],cwd=self.base,env=self.launch_environment(),capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=40)
        return self.parse_result(run)

    def run_play_cmd(self,wait=True):
        env = self.launch_environment()
        cmd = Path(os.environ['SystemRoot'])/'System32/cmd.exe'
        # Use the literal fixture path as Windows would when opening the file;
        # an environment-variable indirection masks initial CMD percent expansion.
        command = '"'+str(cmd)+'" /d /s /c ""'+str(self.root/'Play-EnshroudedVRM.cmd')+'"'+(' -Wait' if wait else '')+'"'
        run = subprocess.run(command,cwd=self.base,env=env,stdin=subprocess.DEVNULL,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=40)
        return self.parse_result(run)

    def save_settings(self,**overrides):
        saved = {'game':str(self.game),'model':str(self.model),'springs':True}
        saved.update(overrides)
        path = self.root/'State/settings.json'
        path.parent.mkdir(exist_ok=True)
        path.write_text(json.dumps(saved),encoding='utf-8')
        return path

    def test_check_accepts_unicode_relocation_without_creating_state(self):
        run,result = self.run_script('-Check')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('ok'),run.stdout+run.stderr)
        self.assertFalse(result['gameStarted'])
        self.assertFalse(result['captureFiles'])
        self.assertFalse((self.root/'State').exists())
        self.assertFalse((self.root/'Play-EnshroudedVRM.cmd').exists())
        self.assertFalse(self.marker.exists())

    def test_model_free_check_defers_selection_to_f8(self):
        run,result=self.run_script('-Check','-Game',str(self.game),use_paths=False)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result['ok'])
        self.assertFalse((self.root/'State').exists())

    def test_model_free_launch_starts_original_character_with_mod(self):
        run,result=self.run_script('-Launch','-Wait','-Game',str(self.game),use_paths=False)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result['gameStarted'])
        self.assertEqual(result['model'],'')
        self.assertEqual(len(self.marker.read_bytes()),4)

    def test_play_ignores_missing_legacy_avatar_path(self):
        self.save_settings(model=str(self.base/'deleted-old-avatar.vrm'))
        run,result=self.run_script('-Play','-Wait',use_paths=False)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result['gameStarted'])
        self.assertEqual(result['model'],'')

    def run_diagnostics(self,body,extra_env=None):
        command="& (Get-Module launcher_core) { $p=Get-EvrmLaunchPlan $env:EVRM_TEST_ROOT $env:EVRM_TEST_GAME $env:EVRM_TEST_MODEL; "+body+" }"
        probe=self.base/'diagnostics-check.ps1'
        probe.write_text("$ErrorActionPreference='Stop'; Import-Module -Name $env:EVRM_TEST_MODULE -DisableNameChecking; "+command,encoding='utf-8-sig')
        env=os.environ.copy()
        env.update({'EVRM_TEST_MODULE':str(self.root/'launcher_core.psm1'),'EVRM_TEST_ROOT':str(self.root),'EVRM_TEST_GAME':str(self.game),'EVRM_TEST_MODEL':str(self.model)})
        env.update(extra_env or {})
        return subprocess.run([str(powershell),'-NoLogo','-NoProfile','-File',str(probe)],cwd=self.base,env=env,capture_output=True,text=True,encoding='utf-8',errors='replace',timeout=30)

    def test_launch_records_early_exit_without_starting_another_child(self):
        run,result=self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('gameStarted'))
        self.assertIn('attemptReportPath',result)
        record=json.loads(Path(result['attemptReportPath']).read_text(encoding='utf-8-sig'))
        self.assertEqual(record['status'],'exited-during-startup')
        self.assertTrue(record['earlyExit'])
        self.assertEqual(record['exitCode'],0)
        self.assertEqual(len(self.marker.read_bytes()),4)

    def test_failed_process_start_is_preserved_as_an_attempt(self):
        self.game.write_bytes(b'not an executable')
        self.manifest['game_sha256']=self.hash(self.game)
        self.save_manifest()
        run,result=self.run_script('-Launch','-Wait')
        self.assertNotEqual(run.returncode,0,run.stdout+run.stderr)
        reports=list((self.root/'State/Logs').glob('*.attempt.json'))
        self.assertEqual(len(reports),1)
        record=json.loads(reports[0].read_text(encoding='utf-8-sig'))
        self.assertEqual(record['status'],'launch-failed')
        self.assertFalse(record['gameStarted'])
        self.assertFalse(self.marker.exists())

    def test_history_keeps_five_session_groups_and_unrecognized_files(self):
        folder=self.root/'State/Logs'
        folder.mkdir(parents=True)
        ids=['20261020-00000'+str(index)+'-1234-deadbeef' for index in range(8)]
        for session in ids:
            for suffix in ('.log','.game.log','.attempt.json'):
                (folder/(session+suffix)).write_bytes(b'old fixture')
        (folder/'notes.log').write_bytes(b'keep notes')
        protected=folder/'20261019-000000-1234-deadbeef.log'
        protected.mkdir()
        (protected/'private.txt').write_bytes(b'keep directory')
        run=self.run_diagnostics("$a=New-EvrmLaunchAttempt $p; Remove-EvrmOldLogs $p; $a | ConvertTo-Json -Compress")
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        current=json.loads(run.stdout.lstrip('\ufeff'))['session']
        retained={path.name.split('.',1)[0] for path in folder.iterdir() if path.is_file() and path.name!='notes.log'}
        self.assertEqual(retained,set(ids[-4:])|{current})
        self.assertEqual((folder/'notes.log').read_bytes(),b'keep notes')
        self.assertEqual((protected/'private.txt').read_bytes(),b'keep directory')

    def test_history_trims_completed_native_log_to_one_megabyte(self):
        folder=self.root/'State/Logs'
        folder.mkdir(parents=True)
        old=folder/'20261020-000000-1234-deadbeef.log'
        content=b'x'*(1024*1024+30)+b'final native event'
        old.write_bytes(content)
        run=self.run_diagnostics("$a=New-EvrmLaunchAttempt $p; Remove-EvrmOldLogs $p")
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertEqual(old.read_bytes(),content[-1048576:])

    def test_history_leaves_hard_linked_native_log_and_external_file_unchanged(self):
        folder=self.root/'State/Logs'
        folder.mkdir(parents=True)
        outside=self.base/'outside-data.bin'
        content=b'protected'*(150000)
        outside.write_bytes(content)
        linked=folder/'20261020-000000-1234-deadbeef.log'
        try:os.link(outside,linked)
        except OSError:self.skipTest('Hard links unavailable')
        run=self.run_diagnostics("$a=New-EvrmLaunchAttempt $p; Remove-EvrmOldLogs $p")
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertEqual(outside.read_bytes(),content)
        self.assertEqual(linked.read_bytes(),content)
        self.assertEqual(linked.stat().st_nlink,2)

    def test_attempt_write_leaves_hard_linked_destination_untouched(self):
        folder=self.root/'State/Logs'
        folder.mkdir(parents=True)
        outside=self.base/'outside-record.txt'
        outside.write_bytes(b'user-owned external data')
        linked=folder/'20261020-000000-1234-deadbeef.attempt.json'
        try:os.link(outside,linked)
        except OSError:self.skipTest('Hard links unavailable')
        run=self.run_diagnostics("$p.Log=Join-Path $env:EVRM_TEST_ROOT 'State\\Logs\\20261020-000000-1234-deadbeef.log'; $a=New-EvrmLaunchAttempt $p; $a | ConvertTo-Json -Compress")
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertEqual(outside.read_bytes(),b'user-owned external data')
        self.assertEqual(linked.read_bytes(),b'user-owned external data')
        self.assertIn('diagnosticWarning',json.loads(run.stdout.lstrip('\ufeff')))

    def test_game_log_hard_link_is_not_copied_into_history(self):
        outside=self.base/'outside-game-log.txt'
        outside.write_bytes(b'private unrelated file')
        try:os.link(outside,self.game.parent/'enshrouded.log')
        except OSError:self.skipTest('Hard links unavailable')
        run=self.run_diagnostics("$a=New-EvrmLaunchAttempt $p; Save-EvrmStartupLog $p $a $null")
        self.assertNotEqual(run.returncode,0)
        self.assertFalse(list((self.root/'State/Logs').glob('*.game.log')))
        self.assertEqual(outside.read_bytes(),b'private unrelated file')

    def test_game_log_snapshot_is_bounded_and_never_changes_original(self):
        source=self.game.parent/'enshrouded.log'
        content=b'a'*300000+b'last startup error'
        source.write_bytes(content)
        run=self.run_diagnostics("$a=New-EvrmLaunchAttempt $p; Save-EvrmStartupLog $p $a $null; $a | ConvertTo-Json -Compress")
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        record=json.loads(run.stdout.lstrip('\ufeff'))
        snapshot=self.root/'State/Logs'/(record['session']+'.game.log')
        self.assertEqual(snapshot.read_bytes(),content[-262144:])
        self.assertTrue(record['gameLogCaptured'])
        self.assertTrue(record['gameLogTruncated'])
        self.assertEqual(source.read_bytes(),content)

    def test_unchanged_previous_game_log_is_not_attributed_to_new_attempt(self):
        (self.game.parent/'enshrouded.log').write_bytes(b'previous successful session')
        run=self.run_diagnostics("$baseline=Get-EvrmGameLogStamp $p; $a=New-EvrmLaunchAttempt $p; Save-EvrmStartupLog $p $a $baseline; $a | ConvertTo-Json -Compress")
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        record=json.loads(run.stdout.lstrip('\ufeff'))
        self.assertFalse(record['gameLogCaptured'])
        self.assertFalse((self.root/'State/Logs'/(record['session']+'.game.log')).exists())

    def test_unknown_baseline_never_attributes_previous_game_log_to_new_attempt(self):
        source=self.game.parent/'enshrouded.log'
        source.write_bytes(b'unchanged previous game session')
        command="""$script:savedStamp=(Get-Command Get-EvrmGameLogStamp).ScriptBlock; $script:stampCalls=0; Set-Item function:script:Get-EvrmGameLogStamp { param($Plan) $script:stampCalls++; if($script:stampCalls -eq 1) { throw 'fixture baseline read failed' }; & $script:savedStamp $Plan }; $p.Environment['PORTABLE_TEST_MARKER']=Join-Path $env:EVRM_TEST_ROOT 'probe-marker.bin'; try { $result=Invoke-EvrmLaunch $p } finally { Set-Item function:script:Get-EvrmGameLogStamp $script:savedStamp }; $result | ConvertTo-Json -Compress"""
        run=self.run_diagnostics(command)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        result=json.loads(run.stdout.lstrip('\ufeff'))
        self.assertTrue(result['gameStarted'])
        self.assertFalse(list((self.root/'State/Logs').glob('*.game.log')))
        self.assertIn('diagnosticWarning',result)
        self.assertEqual(source.read_bytes(),b'unchanged previous game session')

    def test_storage_failure_does_not_disable_early_exit_observation(self):
        command="""$script:savedWrite=(Get-Command Write-EvrmLaunchAttempt).ScriptBlock; Set-Item function:script:Write-EvrmLaunchAttempt { param($Plan,$Attempt) throw 'fixture storage unavailable' }; $p.Environment['PORTABLE_TEST_MARKER']=Join-Path $env:EVRM_TEST_ROOT 'never-written.bin'; $p.Environment['ENSHROUDED_VRM_CAPTURE_FRAMES']='invalid'; try { $result=Invoke-EvrmLaunch $p } finally { Set-Item function:script:Write-EvrmLaunchAttempt $script:savedWrite }; $result | ConvertTo-Json -Compress"""
        run=self.run_diagnostics(command)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        result=json.loads(run.stdout.lstrip('\ufeff'))
        self.assertTrue(result['gameStarted'])
        self.assertEqual(result.get('startupStatus'),'exited-during-startup')
        self.assertTrue(result.get('earlyExit'))
        self.assertEqual(result.get('exitCode'),8)
        self.assertIn('diagnosticWarning',result)
        self.assertFalse((self.root/'never-written.bin').exists())

    def test_check_never_prunes_existing_history(self):
        folder=self.root/'State/Logs'
        folder.mkdir(parents=True)
        for index in range(8):
            (folder/('20261020-00000'+str(index)+'-1234-deadbeef.log')).write_bytes(b'keep during check')
        before={path.name:path.read_bytes() for path in folder.iterdir()}
        run,result=self.run_script('-Check')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertFalse(result['gameStarted'])
        self.assertEqual({path.name:path.read_bytes() for path in folder.iterdir()},before)

    def test_locked_old_log_delays_cleanup_without_blocking_game(self):
        folder=self.root/'State/Logs'
        folder.mkdir(parents=True)
        for index in range(7):
            (folder/('20261020-00000'+str(index)+'-1234-deadbeef.log')).write_bytes(b'old owned log')
        oldest=folder/'20261020-000000-1234-deadbeef.log'
        with oldest.open('rb') as locked:
            run,result=self.run_script('-Launch','-Wait')
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertTrue(result['gameStarted'])
            self.assertIn('diagnosticWarning',result)
            self.assertEqual(locked.read(),b'old owned log')
        run,result=self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        groups={path.name.split('.',1)[0] for path in folder.iterdir() if path.is_file()}
        self.assertEqual(len(groups),5)
        self.assertEqual(len(self.marker.read_bytes()),8)

    def test_startup_observer_records_running_process_without_claiming_readiness(self):
        run=self.run_diagnostics("$a=New-EvrmLaunchAttempt $p; $process=[Diagnostics.Process]::GetCurrentProcess(); try { $a=Watch-EvrmStartup $p $process $a $null 0.05 } finally { $process.Dispose() }; $a | ConvertTo-Json -Compress")
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        record=json.loads(run.stdout.lstrip('\ufeff'))
        self.assertEqual(record['status'],'running-after-startup-check')
        self.assertFalse(record['earlyExit'])
        self.assertIsNone(record['exitCode'])

    def test_startup_observer_preserves_first_log_before_a_retry(self):
        source=self.game.parent/'enshrouded.log'
        source.write_bytes(b'previous game log')
        command="$baseline=Get-EvrmGameLogStamp $p; $a=New-EvrmLaunchAttempt $p; $start=[Diagnostics.ProcessStartInfo]::new(); $start.FileName=$p.Game; $start.UseShellExecute=$false; foreach($entry in $p.Environment.GetEnumerator()) { $start.EnvironmentVariables[$entry.Key]=$entry.Value }; $start.EnvironmentVariables['PORTABLE_TEST_MARKER']=Join-Path ([IO.Path]::GetDirectoryName($p.Game)) 'enshrouded.log'; $process=[Diagnostics.Process]::Start($start); try { $a=Watch-EvrmStartup $p $process $a $baseline 3 } finally { $process.Dispose() }; [IO.File]::WriteAllText((Join-Path ([IO.Path]::GetDirectoryName($p.Game)) 'enshrouded.log'),'second successful startup'); $a | ConvertTo-Json -Compress"
        run=self.run_diagnostics(command)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        record=json.loads(run.stdout.lstrip('\ufeff'))
        self.assertEqual(record['status'],'exited-during-startup')
        self.assertEqual(record['exitCode'],0)
        saved=(self.root/'State/Logs'/(record['session']+'.game.log')).read_bytes()
        self.assertTrue(saved.startswith(b'previous game log'))
        self.assertEqual(len(saved),len(b'previous game log')+4)
        self.assertEqual(source.read_bytes(),b'second successful startup')

    def test_startup_observer_records_actual_nonzero_exit_code(self):
        command="$a=New-EvrmLaunchAttempt $p; $start=[Diagnostics.ProcessStartInfo]::new(); $start.FileName=$p.Game; $start.UseShellExecute=$false; foreach($entry in $p.Environment.GetEnumerator()) { $start.EnvironmentVariables[$entry.Key]=$entry.Value }; $start.EnvironmentVariables['PORTABLE_TEST_MARKER']=Join-Path $env:EVRM_TEST_ROOT 'never-created.bin'; $start.EnvironmentVariables['ENSHROUDED_VRM_CAPTURE_FRAMES']='invalid'; $process=[Diagnostics.Process]::Start($start); try { $a=Watch-EvrmStartup $p $process $a $null 3 } finally { $process.Dispose() }; $a | ConvertTo-Json -Compress"
        run=self.run_diagnostics(command)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        record=json.loads(run.stdout.lstrip('\ufeff'))
        self.assertTrue(record['earlyExit'])
        self.assertEqual(record['exitCode'],8)
        self.assertFalse((self.root/'never-created.bin').exists())

    def test_diagnostic_write_failure_does_not_abort_startup_observation(self):
        run=self.run_diagnostics("$a=New-EvrmLaunchAttempt $p; $path=Get-EvrmDiagnosticFile $p '.attempt.json'; [IO.File]::Delete($path); [void][IO.Directory]::CreateDirectory($path); $process=[Diagnostics.Process]::GetCurrentProcess(); try { $a=Watch-EvrmStartup $p $process $a $null 0.05 } finally { $process.Dispose() }; $a | ConvertTo-Json -Compress")
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        record=json.loads(run.stdout.lstrip('\ufeff'))
        self.assertEqual(record['status'],'running-after-startup-check')
        self.assertIn('diagnosticWarning',record)

    def test_cleanup_rejects_linked_log_directory_without_touching_target(self):
        outside=self.base/'outside logs'
        outside.mkdir()
        target=outside/'20261020-000000-1234-deadbeef.log'
        target.write_bytes(b'keep outside')
        (self.root/'State').mkdir()
        cmd=Path(os.environ['SystemRoot'])/'System32/cmd.exe'
        linked=self.root/'State/Logs'
        run=subprocess.run([str(cmd),'/d','/c','mklink','/J',str(linked),str(outside)],capture_output=True,text=True)
        if run.returncode:self.skipTest('Junction creation is unavailable')
        try:
            run=self.run_diagnostics("Remove-EvrmOldLogs $p")
            self.assertNotEqual(run.returncode,0)
            self.assertEqual(target.read_bytes(),b'keep outside')
        finally:
            os.rmdir(linked)

    def test_successful_launch_generates_play_that_uses_saved_settings(self):
        run,result = self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        play = self.root/'Play-EnshroudedVRM.cmd'
        self.assertTrue(play.is_file(),'First successful launch must create Play')
        self.assertEqual(result.get('playScriptPath'),str(play))
        self.marker.unlink()
        run,result = self.run_play_cmd()
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('gameStarted'),run.stdout+run.stderr)
        self.assertEqual(result['model'],'')
        self.assertEqual(len(self.marker.read_bytes()),4,'Play must start exactly one child')

    def test_play_uses_f8_for_models_and_reads_springs_without_regeneration(self):
        run,result = self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        play = self.root/'Play-EnshroudedVRM.cmd'
        self.assertTrue(play.is_file(),'First successful launch must create Play')
        original = play.read_bytes()
        stamp = play.stat().st_mtime_ns
        other = self.base/'another avatar & ! %PATH% モデル.vrm'
        shutil.copyfile(self.model,other)
        state = self.save_settings(model=str(other),springs=False)
        self.marker.unlink()
        run,result = self.run_play_cmd()
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertEqual(result.get('model'),'')
        self.assertFalse(json.loads(state.read_text())['springs'])
        self.assertEqual(play.read_bytes(),original)
        self.assertEqual(play.stat().st_mtime_ns,stamp,'An identical Play must be left alone')
        self.assertEqual(len(self.marker.read_bytes()),4)

    def test_play_default_double_click_needs_no_input_or_extra_options(self):
        run,result = self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue((self.root/'Play-EnshroudedVRM.cmd').is_file())
        self.marker.unlink()
        run,result = self.run_play_cmd(wait=False)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('gameStarted'),run.stdout+run.stderr)
        deadline = time.monotonic()+5
        while not self.marker.exists() and time.monotonic()<deadline:time.sleep(0.02)
        self.assertTrue(self.marker.exists(),'The default Play command did not launch its child')
        self.assertEqual(len(self.marker.read_bytes()),4)

    def test_play_runs_from_relocated_package_with_cmd_metacharacters(self):
        run,result = self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue((self.root/'Play-EnshroudedVRM.cmd').is_file())
        relocated = self.base/'relocated & ! 模型'
        self.root.rename(relocated)
        self.root = relocated
        self.marker.unlink()
        run,result = self.run_play_cmd()
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('gameStarted'),run.stdout+run.stderr)
        self.assertEqual(len(self.marker.read_bytes()),4)

    def test_windows_file_association_opens_relocated_play(self):
        run,result = self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue((self.root/'Play-EnshroudedVRM.cmd').is_file())
        relocated = self.base/'shell opening & ! 模型'
        self.root.rename(relocated)
        self.root = relocated
        self.marker.unlink()
        report = self.base/'shell report.json'
        # Start-Process uses the real Windows file association for .cmd. Retain
        # and wait for its process so the hidden shell releases its working
        # directory before TemporaryDirectory cleanup; os.startfile has no handle.
        command = """$arguments=@('-Wait','-Report',('"'+$env:EVRM_SHELL_REPORT+'"')); $process=Start-Process -FilePath (Join-Path $env:EVRM_TEST_ROOT 'Play-EnshroudedVRM.cmd') -ArgumentList $arguments -WorkingDirectory ([IO.Path]::GetDirectoryName($env:EVRM_TEST_ROOT)) -WindowStyle Hidden -Wait -PassThru; exit $process.ExitCode"""
        run = self.run_module(command,{'PORTABLE_TEST_MARKER':str(self.marker),'EVRM_SHELL_REPORT':str(report)})
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        result = json.loads(report.read_text(encoding='utf-8-sig'))
        self.assertTrue(result.get('gameStarted'),str(result))
        self.assertEqual(result.get('exitCode'),0)
        self.assertEqual(len(self.marker.read_bytes()),4)

    def test_percent_package_folder_rejected_before_generating_unusable_play(self):
        relocated = self.base/'portable %PATH% folder'
        self.root.rename(relocated)
        self.root = relocated
        self.assert_rejected('percent')
        self.assertFalse((self.root/'Play-EnshroudedVRM.cmd').exists())

    def test_play_requires_saved_setup_even_with_one_bundled_avatar(self):
        avatars = self.root/'Avatars'
        avatars.mkdir()
        shutil.copyfile(self.model,avatars/'only.vrm')
        run,result = self.run_script('-Play','-Wait',use_paths=False)
        self.assertNotEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertFalse(result.get('gameStarted',True),run.stdout+run.stderr)
        self.assertIn('Launch-EnshroudedVRM.cmd',result.get('error',''))
        self.assertFalse(self.marker.exists())
        self.assertFalse((self.root/'State').exists())

    def test_play_rejects_malformed_or_incomplete_saved_setup(self):
        path = self.save_settings()
        for data in ['not JSON','null','[]','{}',json.dumps({'game':str(self.game),'model':str(self.model),'springs':'false'}),json.dumps({'game':'','model':str(self.model),'springs':True})]:
            with self.subTest(data=data):
                path.write_text(data,encoding='utf-8')
                run,result = self.run_script('-Play','-Wait',use_paths=False)
                self.assertNotEqual(run.returncode,0,run.stdout+run.stderr)
                self.assertFalse(result.get('gameStarted',True),run.stdout+run.stderr)
                self.assertIn('Launch-EnshroudedVRM.cmd',result.get('error',''))
                self.assertFalse(self.marker.exists())
                self.assertFalse((self.root/'Play-EnshroudedVRM.cmd').exists())

    def test_play_does_not_require_legacy_model_field(self):
        path=self.save_settings()
        data=json.loads(path.read_text());del data['model'];path.write_text(json.dumps(data),encoding='utf-8')
        run,result=self.run_script('-Play','-Wait',use_paths=False)
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result['gameStarted'])
        self.assertEqual(result['model'],'')

    def test_play_rejects_paths_dependent_on_current_drive_or_directory(self):
        # Both forms point at real fixture files from this test's working
        # directory. Accepting them would start a child, not just fail later.
        for field,path in [('game',self.game)]:
            variants = [path.drive+os.path.relpath(path,self.base),str(path)[2:]]
            for value in variants:
                with self.subTest(field=field,value=value):
                    if self.marker.exists():self.marker.unlink()
                    self.save_settings(**{field:value})
                    run,result = self.run_script('-Play','-Wait',use_paths=False)
                    self.assertNotEqual(run.returncode,0,run.stdout+run.stderr)
                    self.assertFalse(result.get('gameStarted',True),run.stdout+run.stderr)
                    self.assertIn('saved '+('avatar' if field=='model' else 'game')+' path',result.get('error','').lower())
                    self.assertFalse(self.marker.exists())

    def test_play_rejects_conflicting_modes_and_configuration_overrides(self):
        self.save_settings()
        for options in [('-Check',),('-Launch',),('-Game',str(self.game)),('-Model',str(self.model)),('-NoSprings',),('-EnableSprings',)]:
            with self.subTest(options=options):
                run,result = self.run_script('-Play',*options,use_paths=False)
                self.assertNotEqual(run.returncode,0,run.stdout+run.stderr)
                self.assertFalse(result.get('gameStarted',True),run.stdout+run.stderr)
                self.assertIn('-Play',result.get('error',''))
                self.assertFalse(self.marker.exists())

    def test_generated_play_keeps_game_and_package_fingerprint_checks(self):
        run,result = self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue((self.root/'Play-EnshroudedVRM.cmd').is_file())
        self.marker.unlink()
        original = self.game.read_bytes()
        self.game.write_bytes(b'unsupported build')
        run,result = self.run_play_cmd()
        self.assertNotEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertIn('game version is not supported',result.get('error','').lower())
        self.assertFalse(self.marker.exists())
        self.game.write_bytes(original)
        (self.root/'Runtime/enshrouded_vrm_runtime.dll').write_bytes(b'changed runtime')
        run,result = self.run_play_cmd()
        self.assertNotEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertIn('release hash',result.get('error','').lower())
        self.assertFalse(self.marker.exists())

    def test_failed_child_start_does_not_create_play(self):
        self.game.write_bytes(b'valid fingerprint, invalid Windows executable')
        self.manifest['game_sha256'] = self.hash(self.game)
        self.save_manifest()
        run,result = self.run_script('-Launch','-Wait')
        self.assertNotEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertFalse(result.get('gameStarted',True),run.stdout+run.stderr)
        self.assertFalse((self.root/'Play-EnshroudedVRM.cmd').exists())
        self.assertFalse(self.marker.exists())

    def test_existing_play_file_is_preserved_and_started_game_is_reported(self):
        play = self.root/'Play-EnshroudedVRM.cmd'
        existing = b'@echo off\r\nrem User-owned file; do not replace.\r\n'
        play.write_bytes(existing)
        run,result = self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('gameStarted'),run.stdout+run.stderr)
        self.assertIn('playScriptWarning',result)
        self.assertEqual(play.read_bytes(),existing)
        self.assertEqual(len(self.marker.read_bytes()),4)

    def test_play_creation_failure_never_retries_or_misreports_started_child(self):
        (self.root/'Play-EnshroudedVRM.cmd').mkdir()
        run,result = self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('gameStarted'),run.stdout+run.stderr)
        self.assertIn('playScriptWarning',result)
        self.assertEqual(len(self.marker.read_bytes()),4)

    def stub_interactive_selection(self):
        entry = self.root/'Launch-EnshroudedVRM.ps1'
        text = entry.read_text(encoding='utf-8-sig')
        # Isolate the native file-selection dependency in this fixture only.
        # All validation, launch, result handling and warning display stay real.
        start = text.index("    if ($interactive) {\n        Write-Host 'EnshroudedVRM")
        end = text.index('    $plan = Get-EvrmLaunchPlan',start)
        entry.write_text(text[:start]+text[end:],encoding='utf-8')
        for item in self.files:
            if item['path']=='Launch-EnshroudedVRM.ps1':item['sha256']=self.hash(entry)
        self.save_manifest()

    def test_interactive_generation_warning_does_not_undo_started_result(self):
        self.stub_interactive_selection()
        (self.root/'Play-EnshroudedVRM.cmd').write_bytes(b'user-owned file')
        command = "function Read-Host { 'y' }; & (Join-Path $env:EVRM_TEST_ROOT 'Launch-EnshroudedVRM.ps1') -Game $env:EVRM_TEST_GAME -Model $env:EVRM_TEST_MODEL -WarningAction Stop"
        run,result = self.parse_result(self.run_module(command,{'PORTABLE_TEST_MARKER':str(self.marker)}))
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('gameStarted'),run.stdout+run.stderr)
        self.assertIn('playScriptWarning',result)
        deadline = time.monotonic()+5
        while not self.marker.exists() and time.monotonic()<deadline:time.sleep(0.02)
        self.assertEqual(len(self.marker.read_bytes()),4)

    def test_interactive_message_failure_preserves_started_result(self):
        self.stub_interactive_selection()
        command = "function Read-Host { 'y' }; function Write-Host { param($Object) if ([string]$Object -like 'Next time,*') { throw 'fixture display failure' } }; & (Join-Path $env:EVRM_TEST_ROOT 'Launch-EnshroudedVRM.ps1') -Game $env:EVRM_TEST_GAME -Model $env:EVRM_TEST_MODEL"
        run,result = self.parse_result(self.run_module(command,{'PORTABLE_TEST_MARKER':str(self.marker)}))
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue(result.get('gameStarted'),run.stdout+run.stderr)
        self.assertIn('launcherWarning',result)
        deadline = time.monotonic()+5
        while not self.marker.exists() and time.monotonic()<deadline:time.sleep(0.02)
        self.assertEqual(len(self.marker.read_bytes()),4)

    def test_saved_launch_preserves_adjacent_loader_and_mod_files(self):
        inputs = {'dbghelp.dll':b'EML stand-in, not loaded','winmm.dll':b'loader stand-in, not loaded','mods/eml-test/mod.json':b'{"id":"fixture"}','mods/shroudforge-test/settings.json':b'{"fixture":true}'}
        for name,data in inputs.items():
            path = self.game.parent/name
            path.parent.mkdir(parents=True,exist_ok=True)
            path.write_bytes(data)
        run,result = self.run_script('-Launch','-Wait')
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertTrue((self.root/'Play-EnshroudedVRM.cmd').is_file())
        self.marker.unlink()
        run,result = self.run_play_cmd()
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertEqual(len(self.marker.read_bytes()),4)
        for name,data in inputs.items():self.assertEqual((self.game.parent/name).read_bytes(),data)

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

    def test_setup_skips_game_picker_when_executable_is_already_found(self):
        run=self.run_module("$selection=@(Get-EvrmSetupSelections $env:EVRM_TEST_GAME $env:EVRM_TEST_ROOT); ConvertTo-Json -InputObject $selection -Compress")
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertEqual(json.loads(run.stdout.lstrip('\ufeff')),[])
        self.assertFalse(self.marker.exists())
        self.assertFalse((self.root/'State').exists())

    def test_setup_requests_game_picker_when_existing_executable_is_unsupported(self):
        self.game.write_bytes(b'old or unsupported executable')
        run=self.run_module("$selection=@(Get-EvrmSetupSelections $env:EVRM_TEST_GAME $env:EVRM_TEST_ROOT); ConvertTo-Json -InputObject $selection -Compress")
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertEqual(json.loads(run.stdout.lstrip('\ufeff')),['Game'])
        self.assertFalse(self.marker.exists())
        self.assertFalse((self.root/'State').exists())

    def test_setup_requests_game_picker_when_saved_executable_is_missing(self):
        self.game.unlink()
        run=self.run_module("$selection=@(Get-EvrmSetupSelections $env:EVRM_TEST_GAME); ConvertTo-Json -InputObject $selection -Compress")
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        self.assertEqual(json.loads(run.stdout.lstrip('\ufeff')),['Game'])
        self.assertFalse(self.marker.exists())
        self.assertFalse((self.root/'State').exists())

    def test_setup_requests_game_picker_for_empty_or_wrong_file_selection(self):
        for path in ['',str(self.model)]:
            with self.subTest(path=path):
                run=self.run_module("$selection=@(Get-EvrmSetupSelections $env:EVRM_SETUP_GAME); ConvertTo-Json -InputObject $selection -Compress",{'EVRM_SETUP_GAME':path})
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                self.assertEqual(json.loads(run.stdout.lstrip('\ufeff')),['Game'])
                self.assertFalse(self.marker.exists())

    def first_interactive_picker(self,expect_start=False):
        entry=self.root/'Launch-EnshroudedVRM.ps1'
        text=entry.read_text(encoding='utf-8-sig')
        factory='$dialog = [Windows.Forms.OpenFileDialog]::new()'
        if text.count(factory)!=1:raise RuntimeError('Cannot safely substitute the native file dialog')
        entry.write_text(text.replace(factory,'$dialog = New-EvrmTestDialog'),encoding='utf-8')
        for item in self.files:
            if item['path']=='Launch-EnshroudedVRM.ps1':item['sha256']=self.hash(entry)
        self.save_manifest()
        title=self.base/'picker-title.txt'
        # Substitute only the UI object, retaining the real interactive flow and
        # picker-selection policy. Cancellation prevents all game launches.
        command="""
function Read-Host { throw 'Unexpected startup confirmation' }
function New-EvrmTestDialog {
    $dialog=[pscustomobject]@{RestoreDirectory=$false; CheckFileExists=$false; CheckPathExists=$false; Title=''; Filter=''; FileName=''; InitialDirectory=''}
    $dialog | Add-Member ScriptMethod ShowDialog { [IO.File]::WriteAllText($env:EVRM_PICKER_MARKER,$this.Title); return [Windows.Forms.DialogResult]::Cancel }
    $dialog | Add-Member ScriptMethod Dispose {}
    return $dialog
}
& (Join-Path $env:EVRM_TEST_ROOT 'Launch-EnshroudedVRM.ps1') -Game $env:EVRM_TEST_GAME -Model $env:EVRM_TEST_MODEL
"""
        run=self.run_module(command,{'EVRM_PICKER_MARKER':str(title),'PORTABLE_TEST_MARKER':str(self.marker)})
        self.assertEqual(run.returncode,0,run.stdout+run.stderr)
        if expect_start:
            self.assertEqual(len(self.marker.read_bytes()),4)
            self.assertTrue((self.root/'Play-EnshroudedVRM.cmd').exists())
            self.assertFalse(title.exists())
            return None
        self.assertFalse(self.marker.exists())
        self.assertFalse((self.root/'Play-EnshroudedVRM.cmd').exists())
        return title.read_text(encoding='utf-8-sig')

    def test_interactive_startup_uses_f8_without_picker_or_confirmation(self):
        self.assertIsNone(self.first_interactive_picker(expect_start=True))

    def test_interactive_setup_opens_game_picker_when_executable_is_missing(self):
        self.game.unlink()
        self.assertEqual(self.first_interactive_picker(),'Choose enshrouded.exe from Steam')

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
