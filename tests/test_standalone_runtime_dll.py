"""Actual rebuilt DLL through the system loader; unrelated/synthetic clients only."""
from pathlib import Path
import ctypes
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]


class StandaloneRuntimeDllTests(unittest.TestCase):
    def test_actual_gpu_compute_forwarding_without_game_hooks(self):
        runtime=os.environ.get('EVRM_RUNTIME_TEST_DIRECTORY');compiler=os.environ.get('EVRM_TEST_COMPILER');include=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY');dxc=os.environ.get('EVRM_SHADER_COMPILER')
        if os.name!='nt' or not all([runtime,compiler,include,dxc]):self.skipTest('explicit actual DLL/compiler/Vulkan/DXC inputs required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        with tempfile.TemporaryDirectory(prefix='evrm-standalone-gpu-',ignore_cleanup_errors=True) as raw:
            folder=Path(raw);shader=folder/'owned.spv';binary=folder/'owned_gpu.exe'
            compile_shader=subprocess.run([dxc,'-spirv','-fspv-target-env=vulkan1.1','-T','cs_6_0','-E','cs_main',str(ROOT/'tests/standalone_gpu_smoke.hlsl'),'-Fo',str(shader)],capture_output=True,text=True,timeout=60)
            self.assertEqual(compile_shader.returncode,0,compile_shader.stdout+compile_shader.stderr)
            built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-municode','-I'+include,str(ROOT/'tests/standalone_gpu_smoke.cpp'),'-o',str(binary)],capture_output=True,text=True,timeout=90)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            env={k:v for k,v in os.environ.items() if not k.startswith(('VK_','ENSHROUDED_VRM_'))};env.update(VK_LAYER_PATH=str(Path(runtime).resolve()),VK_INSTANCE_LAYERS='VK_LAYER_LOCAL_enshrouded_vrm')
            run=subprocess.run([str(binary),str(shader)],env=env,capture_output=True,text=True,timeout=60)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr);print(run.stdout.strip())

    def test_declined_startup_preserves_vulkan_and_ignores_inherited_activation(self):
        runtime=os.environ.get('EVRM_RUNTIME_TEST_DIRECTORY')
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        include=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if os.name!='nt' or not runtime or not compiler or not include:
            self.skipTest('explicit rebuilt DLL, compiler and Vulkan headers required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        runtime=Path(runtime).resolve()
        self.assertTrue((runtime/'enshrouded_vrm_runtime.dll').exists())
        with tempfile.TemporaryDirectory(prefix='evrm-runtime-admission-',ignore_cleanup_errors=True) as raw:
            folder=Path(raw);child=folder/'assets.exe'
            built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-municode','-DEVRM_NO_BOOTSTRAP',
                                  '-I'+include,str(ROOT/'tests/bootstrap_vulkan_child.cpp'),'-o',str(child)],capture_output=True,text=True,timeout=90)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            for name in ['assets.exe','enshrouded.exe']:
                target=folder/name
                if target!=child:shutil.copyfile(child,target)
                untouched=folder/'untrusted-log.txt';untouched.write_text('owned fixture remains unchanged\n')
                blocker=folder/'blocked-capture-folder';blocker.write_text('file, never a directory')
                env={k:v for k,v in os.environ.items() if not k.startswith(('VK_','ENSHROUDED_VRM_'))}
                env.update(VK_LAYER_PATH=str(runtime),VK_INSTANCE_LAYERS='VK_LAYER_LOCAL_enshrouded_vrm',
                           ENSHROUDED_VRM_LOG=str(untouched),ENSHROUDED_VRM_CAPTURE_DIR=str(blocker),
                           ENSHROUDED_VRM_NATIVE_REPLACEMENT='1',ENSHROUDED_VRM_DEFER_RENDERING_PROOF='1',
                           ENSHROUDED_VRM_PROFILE='poison-profile.json',ENSHROUDED_VRM_CAPTURE_FRAMES='1',
                           ENSHROUDED_VRM_VOICE_ENABLED='1',ENSHROUDED_VRM_TOOL_SLOTS='1')
                run=subprocess.run([str(target),'declined'],env=env,capture_output=True,text=True,timeout=60)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
                self.assertIn('instance/device created and destroyed',run.stdout)
                self.assertEqual(untouched.read_text(),'owned fixture remains unchanged\n','declined runtime wrote to inherited log path')
                self.assertEqual(blocker.read_text(),'file, never a directory')


if __name__=='__main__':unittest.main()
