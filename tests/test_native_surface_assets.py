"""Actual immutable asset stager against owned fake device resources; no GPU."""
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest

class NativeSurfaceAssetsTests(unittest.TestCase):
    def test_selection_lifetime_cancel_and_failure(self):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:self.skipTest('Set EVRM_TEST_COMPILER')
        root=Path(__file__).resolve().parents[1]
        include=Path(os.environ.get('EVRM_VULKAN_INCLUDE','C:/Tools/Vulkan-Headers/include'))
        with tempfile.TemporaryDirectory(prefix='evrm-surface-assets-',ignore_cleanup_errors=True) as folder:
            binary=Path(folder)/'surface_assets.exe'
            # The owned fake loader accepts these headers; no Vulkan driver sees them.
            for shader in ('native_skinning.comp.spv','native_skinning.finalize.spv','native_skinning.surface.comp.spv'):
                (Path(folder)/shader).write_bytes(struct.pack('<5I',0x07230203,0,0,0,0))
            result=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-I'+str(include),
                str(root/'tests/native_surface_assets.cpp'),str(root/'src/native_avatar_assets.cpp'),str(root/'src/native_skinning_gpu.cpp'),str(root/'src/mouth_deformation.cpp'),'-o',str(binary)],capture_output=True,text=True,timeout=180)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            for case in ('dimensions','selection','cancel','failure','dedup','malformed','generation','identity','completion','vector-key','skin-identity','skin-owner','skin-switch','failure-life-success','failure-life-idle','failure-life-device-idle','failure-life-cancel'):
                with self.subTest(case=case):
                    result=subprocess.run([str(binary),case,str(folder)],capture_output=True,text=True,timeout=30)
                    self.assertEqual(result.returncode,0,result.stdout+result.stderr)
                    print(result.stdout.strip())

if __name__=='__main__':unittest.main()
