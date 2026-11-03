"""Synthetic importer/pose tests; never graphics, game, mod DLLs or live input."""
from pathlib import Path
import ctypes, os, shutil, subprocess, tempfile, unittest
class EnhancedSurfaceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler:raise unittest.SkipTest('explicit native C++ compiler required')
        if os.name=='nt':ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        cls.tmp=tempfile.TemporaryDirectory(prefix='evrm-surface-',ignore_cleanup_errors=True);cls.addClassCleanup(cls.tmp.cleanup)
        root=Path(__file__).resolve().parents[1];cls.binary=Path(cls.tmp.name)/'surface.exe'
        sources=['vrm_asset.cpp','native_avatar_pose.cpp','native_skinning.cpp','native_material_plan.cpp','mouth_deformation.cpp']
        result=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(root/'tests/enhanced_surface.cpp')]+[str(root/'src'/s) for s in sources]+['-o',str(cls.binary)],capture_output=True,text=True,timeout=180)
        if result.returncode:raise AssertionError(result.stdout+result.stderr)
    def run_mode(self,mode):
        result=subprocess.run([str(self.binary),mode],capture_output=True,text=True,timeout=30)
        self.assertEqual(result.returncode,0,result.stdout+result.stderr)
    def test_metadata(self):self.run_mode('metadata')
    def test_raw_reference(self):self.run_mode('invalid_reference')
    def test_legacy(self):self.run_mode('legacy')
    def test_independent_uv_and_skin(self):self.run_mode('geometry')
    def test_invalid_metadata(self):self.run_mode('invalid_surface')
    def test_no_maps(self):self.run_mode('no_maps')
    def test_mirrored_seam_mouth(self):self.run_mode('seam')
    def test_degenerate_uv(self):self.run_mode('degenerate')
    def test_cpu_pose(self):self.run_mode('cpu_pose')
    def test_invalid_raw_stream(self):self.run_mode('invalid_stream')
    def test_normal_uv_transform(self):self.run_mode('normal_uv_transform')
    def test_mixed_stream_alignment(self):self.run_mode('mixed_stream')
    def test_large_finite_tangent_basis(self):self.run_mode('large_finite_basis')
if __name__=='__main__':unittest.main()
