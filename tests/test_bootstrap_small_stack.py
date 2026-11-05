"""Execute only an explicitly approved prebuilt DLL, never an unscanned rebuild."""
from pathlib import Path
import ctypes
import hashlib
import os
import shutil
import subprocess
import tempfile
import unittest
import sys

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT/'portable'))
from native_compiler import default_compiler, run_compiler


class BootstrapSmallStackTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        directory=os.environ.get('EVRM_APPROVED_BOOTSTRAP_DIRECTORY')
        approved=os.environ.get('EVRM_APPROVED_BOOTSTRAP_SHA256')
        tools=os.environ.get('EVRM_TEST_BOOTSTRAP_COMPILER_DIRECTORY') or str(Path(default_compiler()).parent)
        if os.name!='nt' or not all((directory,approved,tools)):
            raise unittest.SkipTest('explicit approved prebuilt bootstrap and Microsoft tools required')
        cls.dll=Path(directory)/'vulkan-1.dll'
        if hashlib.sha256(cls.dll.read_bytes()).hexdigest()!=approved:
            raise AssertionError('prebuilt DLL differs from explicitly approved hash; execution refused')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        cls.approved=approved
        destination=os.environ.get('EVRM_BOOTSTRAP_SMALL_STACK_DIRECTORY')
        if destination:
            cls.folder=Path(destination).resolve()
            cls.folder.mkdir(parents=True,exist_ok=False)
        else:
            temporary=tempfile.TemporaryDirectory(prefix='evrm-small-stack-',ignore_cleanup_errors=True)
            cls.addClassCleanup(temporary.cleanup)
            cls.folder=Path(temporary.name)
        cls.child=cls.folder/'enshrouded.exe'
        command=[str(Path(tools)/'cl.exe'),'/nologo','/std:c++17','/O2','/MT','/EHsc','/utf-8','/W4','/WX',
                 str(ROOT/'tests/bootstrap_small_stack.cpp'),'/Fo'+str(cls.folder/'child.obj'),'/Fe'+str(cls.child),
                 '/link','/INCREMENTAL:NO','/STACK:1048576,4096']
        compiled=run_compiler(command,cwd=cls.folder,capture_output=True,text=True,timeout=60)
        if compiled.returncode:raise AssertionError(compiled.stdout+compiled.stderr)
        shutil.copyfile(cls.dll,cls.folder/'vulkan-1.dll')
        runtime=cls.folder/'EnshroudedVRM/Runtime';runtime.mkdir(parents=True)
        (runtime/'enshrouded_vrm_layer.json').write_bytes((ROOT/'portable/standalone_layer.json').read_bytes())
        system=ctypes.create_unicode_buffer(32768)
        if not ctypes.windll.kernel32.GetSystemDirectoryW(system,len(system)):raise AssertionError('system directory missing')
        cls.system=Path(system.value)/'vulkan-1.dll'

    def invoke(self,mode,reserve,count,system=False):
        provider=self.system if system else self.folder/'vulkan-1.dll'
        env={key:value for key,value in os.environ.items() if not key.startswith(('VK_','ENSHROUDED_VRM_'))}
        result=subprocess.run([str(self.child),mode,str(provider),str(reserve),str(count)],
                              cwd=self.folder,env=env,capture_output=True,text=True,timeout=25)
        self.assertEqual(result.returncode,0,f'{mode}, reserve={reserve}, system={system}, exit=0x{result.returncode&0xffffffff:08x}\n'+result.stdout+result.stderr)
        self.assertIn('attach/detach and export lookup passed',result.stdout)
        self.assertEqual(hashlib.sha256(self.dll.read_bytes()).hexdigest(),self.approved)
        print(result.stdout.strip())

    def test_system_loader_control_on_64k_stacks(self):
        for mode in ('load','notify'):
            with self.subTest(mode=mode):self.invoke(mode,65536,4,system=True)

    def test_new_thread_notifications_on_64k_stacks(self):
        self.invoke('notify',65536,16)

    def test_first_load_on_64k_thread(self):
        self.invoke('load',65536,1)

    def test_repeated_concurrent_load_and_detach_on_128k_stacks(self):
        self.invoke('load',131072,16)


if __name__=='__main__':unittest.main()
