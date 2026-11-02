"""Owned CPU preview subject ordering; no game, graphics driver or DLL loading."""
from pathlib import Path
import ctypes,os,shutil,subprocess,tempfile,unittest
class PreviewSubjectTests(unittest.TestCase):
    def native(self,name,sources=()):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if os.name!='nt' or not compiler:self.skipTest('explicit Windows compiler required')
        root=Path(__file__).resolve().parents[1]
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        with tempfile.TemporaryDirectory(prefix='evrm-preview-subject-',ignore_cleanup_errors=True) as folder:
            exe=Path(folder)/(name+'.exe')
            extra=[]
            if name=='native_preview_admission':
                body=(root/'src/native_backend.cpp').read_text();begin=body.index('    bool prepare_character_subject(');end=body.index('    static std::uint64_t owner_clock_milliseconds()',begin)
                (Path(folder)/'native_preview_admission.inc').write_text(body[begin:end]);headers=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
                begin=body.index('    void cover_native_draw(');end=body.index('\n\n};',begin)
                (Path(folder)/'native_preview_cover.inc').write_text(body[begin:end])
                pieces=[]
                for first,last in (('void NativeBackend::render_current(', 'void NativeBackend::before_end_rendering('),('void NativeBackend::after_native_draw(', 'void NativeBackend::scope_unsupported(')):
                    begin=body.index(first);end=body.index(last,begin);pieces.append(body[begin:end].replace('NativeBackend::','FixtureNativeBackend::'))
                (Path(folder)/'native_preview_render.inc').write_text('\n'.join(pieces))
                state=(root/'src/native_state.cpp').read_text();begin=state.index('NativeBufferBinding::operator bool()');end=state.index('struct NativeState::Impl',begin)
                (Path(folder)/'native_preview_binding.inc').write_text(state[begin:end])
                self.assertTrue(headers,'explicit Vulkan headers required');extra=['-I'+folder,'-I'+headers]
            built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(root/'tests'/(name+'.cpp')),
                                  *extra,*[str(root/'src'/p) for p in sources],'-o',str(exe)],capture_output=True,text=True,timeout=180)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr);print(ran.stdout)
    def test_subject_ordering(self):self.native('native_preview_subject')
    def test_original_forwarding(self):self.native('native_preview_subject_calls')
    def test_exact_relative_call(self):self.native('native_relative_call',('native_relative_call.cpp',))
    def test_actual_character_admission(self):self.native('native_preview_admission')
if __name__=='__main__':unittest.main()
