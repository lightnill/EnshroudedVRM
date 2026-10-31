"""Inject fake capture devices/public samples; never activate a real microphone."""
from pathlib import Path
import hashlib,os,shutil,struct,subprocess,tempfile,unittest,wave
from native_voice_fixture import stage_voice_library
class MicrophoneTests(unittest.TestCase):
    def compile_run(self,name,sources=(),public=False,flags=()):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler or os.name!='nt':self.skipTest('Windows native compiler required')
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-microphone-',ignore_cleanup_errors=True) as folder:
            folder=Path(folder);arguments=[]
            if public:
                stage_voice_library(folder)
                with wave.open(str(root/'tests/fixtures/voice-public-speech.wav'),'rb') as audio:
                    pcm=b''.join(struct.pack('<fff',v/32768,v/32768,v/32768) for (v,) in struct.iter_unpack('<h',audio.readframes(audio.getnframes())))
                path=folder/'public.f32';path.write_bytes(pcm);arguments=[str(path)]
            binary=folder/(name+'.exe')
            build=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(root/'tests'/(name+'.cpp')),
                *[str(root/'src'/s) for s in sources],*flags,'-o',str(binary)],capture_output=True,text=True,timeout=120)
            self.assertEqual(build.returncode,0,build.stdout+build.stderr)
            run=subprocess.run([str(binary),*arguments],capture_output=True,text=True,timeout=45)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr);print(run.stdout.strip())
    def test_packet_assembly(self):self.compile_run('microphone_audio')
    def test_source_routing(self):self.compile_run('voice_inputs',('mod_settings.cpp',))
    def test_selection_lifetime_and_public_speech(self):
        self.compile_run('microphone_source',('microphone_source.cpp','microphone_backend.cpp','voice_worker.cpp','voice_analyser.cpp','voice_automatic.cpp',
            'voice_features.cpp','voice_profiles.cpp','voice_stream.cpp','voice_clock.cpp'),True,('-lole32','-luuid'))
if __name__=='__main__':unittest.main()
