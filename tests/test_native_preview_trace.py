"""Owned CPU ancestry/report fixtures; no game, GPU, DLL or screen capture."""
from pathlib import Path
import ctypes,os,shutil,subprocess,tempfile,unittest
class PreviewTraceTests(unittest.TestCase):
    def native(self,name):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if os.name!='nt' or not compiler:self.skipTest('explicit Windows compiler required')
        root=Path(__file__).resolve().parents[1];ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        with tempfile.TemporaryDirectory(prefix='evrm-preview-trace-',ignore_cleanup_errors=True) as folder:
            exe=Path(folder)/(name+'.exe');built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(root/'tests'/(name+'.cpp')),'-o',str(exe)],capture_output=True,text=True,timeout=180)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr);print(ran.stdout)
    def test_history(self):self.native('native_preview_trace')
    def test_export(self):self.native('native_preview_trace_export')
    def test_gpu_wiring(self):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++');headers=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if os.name!='nt' or not compiler or not headers:self.skipTest('explicit Windows compiler/Vulkan headers required')
        root=Path(__file__).resolve().parents[1];ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        with tempfile.TemporaryDirectory(prefix='evrm-preview-wiring-',ignore_cleanup_errors=True) as folder:
            exe=Path(folder)/'wiring.exe';built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-I'+headers,str(root/'tests/native_preview_trace_wiring.cpp'),str(root/'src/gpu_probe.cpp'),'-o',str(exe)],capture_output=True,text=True,timeout=180)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr);print(ran.stdout)
    def test_retirement_race(self):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++');headers=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if os.name!='nt' or not compiler or not headers:self.skipTest('explicit Windows compiler/Vulkan headers required')
        root=Path(__file__).resolve().parents[1];text=(root/'src/runtime_hooks.inl').read_text();first=text.index('EVRM_EXPORT void VKAPI_CALL vkDestroyBuffer(');last=text.index('EVRM_EXPORT VkResult VKAPI_CALL vkAllocateMemory(',first)
        with tempfile.TemporaryDirectory(prefix='evrm-preview-retirement-',ignore_cleanup_errors=True) as raw:
            folder=Path(raw);(folder/'actual_destroy.inc').write_text(text[first:last]);exe=folder/'retirement.exe'
            built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-I'+headers,'-I'+str(folder),str(root/'tests/native_preview_retirement.cpp'),'-o',str(exe)],capture_output=True,text=True,timeout=180)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            ran=subprocess.run([str(exe)],capture_output=True,text=True,timeout=30);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr);print(ran.stdout)
if __name__=='__main__':unittest.main()
