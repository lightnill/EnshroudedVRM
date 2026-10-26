import ctypes
import hashlib
import json
import os
import shutil
import struct
import subprocess
import sys
import tempfile
import time
import unittest
from pathlib import Path

launcher_binary=Path(sys.argv[1]).resolve()
child_binary=Path(sys.argv[2]).resolve()
sys.argv=sys.argv[:1]

class NativeLaunchTests(unittest.TestCase):
    def run_child(self,broken_report=False,broken_identity=False,status='local-test-only'):
        with tempfile.TemporaryDirectory() as folder:
            base=Path(folder);root=base/'portable';root.mkdir()
            exe=root/'EnshroudedVRM.exe';shutil.copyfile(launcher_binary,exe)
            game=base/'game & ! 模型'/'enshrouded.exe';game.parent.mkdir();shutil.copyfile(child_binary,game)
            model=base/'test avatar モデル.vrm';model.write_bytes(struct.pack('<III',0x46546c67,2,12))
            manifest={'schema':'enshrouded-vrm-portable-v1','version':'synthetic','publication_status':status,'game_sha256':hashlib.sha256(game.read_bytes()).hexdigest(),'files':[]}
            for name in ['Runtime/enshrouded_vrm_runtime.dll','Runtime/enshrouded_vrm_layer.json','Compatibility/profile.json','Compatibility/catalog.json']:
                file=root/name;file.parent.mkdir(parents=True,exist_ok=True);file.write_bytes(b'synthetic no-graphics fixture')
                manifest['files'].append({'path':name,'sha256':hashlib.sha256(file.read_bytes()).hexdigest()})
            (root/'release-manifest.json').write_text(json.dumps(manifest))
            marker=base/'child marker.bin';report=base/'report.json'
            if broken_report:report.mkdir()
            env=os.environ.copy();env['PORTABLE_TEST_MARKER']=str(marker);env['ENSHROUDED_VRM_TRACE_COMMANDS']='1';env['ENSHROUDED_VRM_CAPTURE_PLAYER_ANCHOR']='1'
            if broken_identity:env['PORTABLE_TEST_FAIL_TIMES']='1'
            run=subprocess.run([str(exe),'--launch','--wait','--game',str(game),'--model',str(model),'--report',str(report)],env=env,capture_output=True,text=True,encoding='utf-8',timeout=15)
            for _ in range(20):
                if marker.exists() and marker.stat().st_size>=4:break
                time.sleep(.05)
            marker_data=marker.read_bytes() if marker.exists() else b''
            # Wait only for this recorded synthetic child before temp cleanup.
            if len(marker_data)>=4:
                kernel=ctypes.WinDLL('kernel32',use_last_error=True);kernel.OpenProcess.restype=ctypes.c_void_p;kernel.WaitForSingleObject.argtypes=[ctypes.c_void_p,ctypes.c_ulong];kernel.CloseHandle.argtypes=[ctypes.c_void_p]
                handle=kernel.OpenProcess(0x00100000,False,struct.unpack('<I',marker_data[:4])[0])
                if handle:kernel.WaitForSingleObject(handle,3000);kernel.CloseHandle(handle)
            lines=[json.loads(line) for line in run.stdout.splitlines() if line.startswith('{')]
            final=next((item for item in reversed(lines) if 'gameStarted' in item),{})
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertTrue(final.get('gameStarted'),run.stdout+run.stderr)
            self.assertEqual(final.get('publicationStatus'),status)
            self.assertEqual(len(marker_data),4,'expected exactly one synthetic child with the correct environment')
            self.assertEqual(final['processId'],struct.unpack('<I',marker_data)[0])
            if broken_report:self.assertIn('reportWarning',final)
            if broken_identity:self.assertIn('identityWarning',final)

    def test_actual_unicode_child_round_trip(self):self.run_child()
    def test_public_preview_child_round_trip(self):self.run_child(status='preview')
    def test_report_failure_preserves_started_child(self):self.run_child(broken_report=True)
    def test_identity_failure_preserves_started_child(self):self.run_child(broken_identity=True)

if __name__=='__main__':unittest.main()
