"""Performance contracts and correctness; no flaky elapsed-time thresholds."""
from pathlib import Path
import ctypes, hashlib, os, shutil, subprocess, tempfile, unittest

class PerformanceContracts(unittest.TestCase):
    def native(self,name,sources=(),flags=()):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler or os.name!='nt':self.skipTest('Windows native compiler required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-performance-',ignore_cleanup_errors=True) as folder:
            binary=Path(folder)/(name+'.exe')
            result=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(root/'tests'/(name+'.cpp')),
                *[str(root/'src'/s) for s in sources],*flags,'-o',str(binary)],capture_output=True,text=True,timeout=180)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(binary),folder],capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())
            return result.stdout
    def test_immutable_owner_snapshot(self):self.native('native_owner_snapshot',('native_owner.cpp',))
    def test_persistent_diagnostics(self):self.native('runtime_log',flags=('-municode',))
    def test_sha256_provider_and_fallback(self):
        output=self.native('sha256')
        rows=output.splitlines();self.assertEqual(len(rows),14)
        for row in rows:
            size,digest=row.split();size=int(size)
            data=bytes((i*17+(i>>7)*31)&255 for i in range(size))
            self.assertEqual(digest,hashlib.sha256(data).hexdigest())

if __name__=='__main__':unittest.main()
