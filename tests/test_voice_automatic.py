"""Official local SDK against its direct export; public audio only."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
import wave
import hashlib
import json
from native_voice_fixture import stage_voice_library

class AutomaticVoiceTests(unittest.TestCase):
    def test_official_sdk_adapter_and_failure_boundaries(self):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler or os.name!='nt':self.skipTest('Windows x64 compiler required')
        root=Path(__file__).resolve().parents[1]
        vendor=root/'vendor/oculus-lipsync';pins=json.loads((vendor/'PROVENANCE.json').read_text())
        for pin in pins['files']:self.assertEqual(hashlib.sha256((vendor/pin['path']).read_bytes()).hexdigest(),pin['sha256'])
        self.assertEqual(hashlib.sha256((root/'tests/fixtures/voice-public-speech.wav').read_bytes()).hexdigest(),'6318883d5a5d5dc4b95457e43e2bf88d934b187f850a46b52ef02b2f459fb775')
        with tempfile.TemporaryDirectory(prefix='evrm-ovr-',ignore_cleanup_errors=True) as folder:
            folder=Path(folder);sdk=stage_voice_library(folder)
            with wave.open(str(root/'tests/fixtures/voice-public-speech.wav'),'rb') as wav:
                self.assertEqual((wav.getframerate(),wav.getnchannels(),wav.getsampwidth()),(16000,1,2));samples=wav.readframes(wav.getnframes())
            pcm=bytearray()
            for (sample,) in struct.iter_unpack('<h',samples):pcm.extend(struct.pack('<fff',sample/32768,sample/32768,sample/32768))
            path=folder/'input.f32';path.write_bytes(pcm);binary=folder/'automatic.exe'
            build=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(root/'tests/voice_automatic.cpp'),str(root/'src/voice_automatic.cpp'),'-o',str(binary)],capture_output=True,text=True,timeout=90)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(binary),str(path),str(sdk)],capture_output=True,text=True,timeout=45);self.assertEqual(run.returncode,0,run.stdout+run.stderr);print(run.stdout.strip())
            for kind in ('missing','tampered','bad-hash'):
                destination=folder/kind;destination.mkdir();child=destination/'automatic.exe';shutil.copyfile(binary,child)
                if kind=='tampered':(destination/'OVRLipSync.dll').write_bytes(b'not the official library')
                if kind=='bad-hash':
                    with (destination/'OVRLipSync.dll').open('wb') as changed:changed.truncate(40471800)
                run=subprocess.run([str(child),'--bad-hash' if kind=='bad-hash' else '--unavailable'],capture_output=True,text=True,timeout=30);self.assertEqual(run.returncode,0,run.stdout+run.stderr);print(run.stdout.strip())
            integration=folder/'integration.exe'
            build=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(root/'tests/voice_automatic_integration.cpp'),*[str(root/'src'/name) for name in (
                'voice_automatic.cpp','voice_analyser.cpp','voice_worker.cpp','voice_features.cpp','voice_profiles.cpp','voice_stream.cpp','voice_clock.cpp')],'-o',str(integration)],capture_output=True,text=True,timeout=90)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(integration),str(path)],capture_output=True,text=True,timeout=45);self.assertEqual(run.returncode,0,run.stdout+run.stderr);print(run.stdout.strip())

if __name__=='__main__':unittest.main()
