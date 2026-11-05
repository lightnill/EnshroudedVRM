"""Actual worker/selection/settings behavior using owned models and files."""
from native_fixture_compiler import default_compiler, run_compiler
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest,ctypes
class PlayerModelServiceTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        compiler=default_compiler()
        if os.name!='nt' or not compiler:raise unittest.SkipTest('Windows compiler required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
        cls.root=Path(__file__).resolve().parents[1];cls.folder=tempfile.TemporaryDirectory(prefix='evrm-player-models-',ignore_cleanup_errors=True);cls.addClassCleanup(cls.folder.cleanup);cls.exe=Path(cls.folder.name)/'player-models.exe'
        sources=[];flags=[]
        if (cls.root/'src/avatar_player_types.hpp').exists():
            sources=['player_assignments.cpp','avatar_model.cpp','avatar_settings.cpp','vrm_asset.cpp','retarget.cpp','native_avatar_pose.cpp','native_skinning.cpp','native_material_plan.cpp','mouth_deformation.cpp'];flags=['-DEVRM_PLAYER_SAVE_FIXTURE','-municode','-lole32','-luuid','-lbcrypt','-luser32']
        cls.report_headers=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if cls.report_headers:
            sources.extend(('debug_report_sources.cpp','debug_report_policy.cpp','native_body_coverage.cpp'))
            flags.extend(('-DEVRM_PLAYER_REPORT_PRIVACY','-I'+cls.report_headers,'-ladvapi32'))
        built=run_compiler([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(cls.root/'tests/player_model_service.cpp'),*[str(cls.root/'src'/name) for name in sources],*flags,'-o',str(cls.exe)],capture_output=True,text=True,timeout=240)
        if built.returncode:raise RuntimeError(built.stdout+built.stderr)
    def run_case(self,mode):
        with tempfile.TemporaryDirectory(prefix='evrm-player-model-state-',ignore_cleanup_errors=True) as folder:
            ran=subprocess.run([str(self.exe),mode,folder],capture_output=True,text=True,timeout=30);self.assertEqual(ran.returncode,0,ran.stdout+ran.stderr)
    def test_targeted_choices_do_not_follow_ui_selection(self):self.run_case('targeting')
    def test_account_choice_and_none_survive_new_connection_and_restart(self):self.run_case('restart')
    def test_pending_picker_and_import_cannot_target_recycled_player(self):self.run_case('pending')
    def test_same_model_shares_assets_and_shader_profiles(self):self.run_case('sharing')
    def test_runtime_actions_keep_explicit_player_target(self):self.run_case('routing')
    def test_capacity_rejection_survives_unrelated_successful_save(self):self.run_case('capacity')
    def test_capacity_rejection_survives_stale_save_completion(self):self.run_case('stale-save')
    def test_corrupt_store_keeps_session_choice_private_and_reports_save_error(self):self.run_case('corrupt')
    def test_saved_assignment_snapshot_is_omitted_from_report(self):
        if not self.report_headers:self.skipTest('Vulkan headers required for actual report exporter')
        self.run_case('privacy')
if __name__=='__main__':unittest.main()
