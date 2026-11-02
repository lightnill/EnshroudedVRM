"""Owned CPU request/task correlation checks; no game, hooks, GPU or DLL load."""
from pathlib import Path
import ctypes,os,shutil,subprocess,tempfile,unittest
class PreviewHandoffTests(unittest.TestCase):
    def native(self,name):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if os.name!='nt' or not compiler:self.skipTest('explicit Windows compiler required')
        root=Path(__file__).resolve().parents[1]
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        with tempfile.TemporaryDirectory(prefix='evrm-preview-handoff-',ignore_cleanup_errors=True) as folder:
            exe=Path(folder)/(name+'.exe')
            built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(root/'tests'/(name+'.cpp')),'-o',str(exe)],capture_output=True,text=True,timeout=180)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr);print(ran.stdout)
    def test_request_task_correlation(self):self.native('native_preview_handoff')
    def test_private_export(self):self.native('native_preview_handoff_export')
    def test_original_call_wiring(self):self.native('native_preview_handoff_calls')
    def test_unsupported_host_installs_nothing(self):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if os.name!='nt' or not compiler:self.skipTest('explicit Windows compiler required')
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-preview-admission-',ignore_cleanup_errors=True) as folder:
            exe=Path(folder)/'admission.exe'
            sources=[root/'tests/native_preview_handoff_admission.cpp',*[root/'src'/name for name in ('native_preview_handoff.cpp','native_relative_call.cpp','native_owner_process.cpp','native_owner.cpp')]]
            built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',*[str(p) for p in sources],'-o',str(exe)],capture_output=True,text=True,timeout=180)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr);print(ran.stdout)
    def test_report_contention(self):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++');headers=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if os.name!='nt' or not compiler or not headers:self.skipTest('explicit Windows compiler/Vulkan headers required')
        root=Path(__file__).resolve().parents[1];text=(root/'src/runtime_hooks.inl').read_text()
        first=text.index('EVRM_EXPORT void VKAPI_CALL vkDestroyBuffer(');last=text.index('EVRM_EXPORT VkResult VKAPI_CALL vkAllocateMemory(',first)
        actual=text[first:last];self.assertEqual(actual.count('evrm::native_preview_trace_retired'),1)
        # Only redirect collector access to the gated history specialization.
        # Both use the actual production retirement helper; forwarding is exact.
        actual=actual.replace('evrm::native_preview_trace_retired','retire_test_sources')
        with tempfile.TemporaryDirectory(prefix='evrm-preview-contention-',ignore_cleanup_errors=True) as folder:
            folder=Path(folder);(folder/'actual_destroy.inc').write_text(actual);exe=folder/'contention.exe'
            built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-I'+headers,'-I'+str(folder),str(root/'tests/native_preview_report_contention.cpp'),'-o',str(exe)],capture_output=True,text=True,timeout=180)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30)
            self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr);print(ran.stdout)
if __name__=='__main__':unittest.main()
