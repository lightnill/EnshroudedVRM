"""Real PNG/GLB decode, no game, GPU, microphone, DLL loading or file dialogs."""
from pathlib import Path
import ctypes
import os
import shutil
import subprocess
import tempfile
import unittest

class ImportTextureDepthTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:raise unittest.SkipTest('explicit native C++ compiler required')
        if os.name=='nt':ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        cls.temporary=tempfile.TemporaryDirectory(prefix='evrm-texture-depth-',ignore_cleanup_errors=True)
        cls.addClassCleanup(cls.temporary.cleanup)
        root=Path(__file__).resolve().parents[1]
        cls.binary=Path(cls.temporary.name)/'texture_depth_tests.exe'
        built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',
                              '-include',str(root/'tests/texture_alloc_hooks.hpp'),
                              '-DSTBI_MALLOC=texture_fixture_malloc','-DSTBI_REALLOC=texture_fixture_realloc','-DSTBI_FREE=texture_fixture_free',
                              str(root/'tests/import_texture_depth.cpp'),str(root/'src/vrm_asset.cpp'),
                              '-o',str(cls.binary)],capture_output=True,text=True,timeout=180)
        if built.returncode:raise AssertionError(built.stdout+built.stderr)
    def check_mode(self,mode):
        result=subprocess.run([str(self.binary),mode],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_rgba16(self):self.check_mode('rgba16')
    def test_rgb16(self):self.check_mode('rgb16')
    def test_gray16(self):self.check_mode('gray16')
    def test_gray_alpha16(self):self.check_mode('grayalpha16')
    def test_rgb16_transparency(self):self.check_mode('rgb16_transparency')
    def test_rgba8_unchanged(self):self.check_mode('rgba8')
    def test_unused16_placeholder(self):self.check_mode('unused16')
    def test_high_depth_working_budget(self):self.check_mode('working_budget')
    def test_high_depth_cumulative_budget(self):self.check_mode('cumulative_budget')
    def test_adam7_workspace_and_pixels(self):self.check_mode('adam7')
    def test_no_leaking_stb_narrowing_allocation(self):self.check_mode('no_stb_narrow_allocation')
    def test_failed_rgba8_allocation_releases_rgba16(self):self.check_mode('vector_allocation_failure')
    def test_dimensions_still_checked(self):self.check_mode('dimensions')
    def test_truncated16_is_atomic_failure(self):self.check_mode('truncated16')

if __name__=='__main__':unittest.main()
