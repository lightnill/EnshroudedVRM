"""Local model/appearance fixtures; no game, actual dialogs or audio capture."""
from pathlib import Path
import os,shutil,subprocess,tempfile,unittest,re,importlib.util,sys
class AvatarSwitchTests(unittest.TestCase):
    def native(self,name,sources=(),flags=()):
        compiler=os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler or os.name!='nt':self.skipTest('Windows native compiler required')
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-avatar-',ignore_cleanup_errors=True) as folder:
            if name=='avatar_renderer_startup':
                renderer=(root/'src/avatar_renderer.cpp').read_text(encoding='utf-8');start=renderer.index('AvatarRenderer::AvatarRenderer(');end=renderer.index('AvatarRenderer::~AvatarRenderer()',start)
                (Path(folder)/'avatar_renderer_startup_methods.inc').write_text(renderer[start:end],encoding='utf-8')
            if name=='avatar_renderer_switch':
                renderer=(root/'src/avatar_renderer.cpp').read_text(encoding='utf-8')
                start=renderer.index('bool AvatarRenderer::stage_candidate(');end=renderer.index('bool AvatarRenderer::before_present(',start)
                (Path(folder)/'avatar_renderer_switch_methods.inc').write_text(renderer[start:end],encoding='utf-8')
                message=renderer.index('Model cannot use the current game pose:');start=renderer.rfind('}catch(',0,message)
                (Path(folder)/'avatar_candidate_failure.inc').write_text(renderer[start:renderer.index('\n',message)],encoding='utf-8')
            if name=='avatar_backend_switch':
                backend=(root/'src/native_backend.cpp').read_text(encoding='utf-8');start=backend.index('bool NativeBackend::activate_model(');end=backend.index('void NativeBackend::publish(',start)
                (Path(folder)/'avatar_backend_switch_methods.inc').write_text(backend[start:end],encoding='utf-8')
            binary=Path(folder)/(name+'.exe')
            built=subprocess.run([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-I'+folder,str(root/'tests'/(name+'.cpp')),
                *[str(root/'src'/source) for source in sources],*flags,'-o',str(binary)],capture_output=True,text=True,timeout=180)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            result=subprocess.run([str(binary),folder],capture_output=True,text=True,timeout=60)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr);print(result.stdout.strip())
    def test_preferences(self):self.native('avatar_settings',('avatar_settings.cpp',),('-municode',))
    model_sources=('avatar_model.cpp','avatar_settings.cpp','vrm_asset.cpp','retarget.cpp','native_avatar_pose.cpp','native_skinning.cpp','native_material_plan.cpp','mouth_deformation.cpp')
    def test_prepared_model(self):self.native('avatar_model',self.model_sources,('-municode',))
    def test_service(self):self.native('avatar_service',(*self.model_sources,'avatar_service.cpp'),('-municode','-lole32','-luuid'))
    def test_shader_persistence(self):self.native('avatar_shader_persistence',(*self.model_sources,'avatar_service.cpp'),('-municode','-lole32','-luuid'))
    def test_transaction(self):self.native('avatar_transaction')
    def test_material(self):self.native('avatar_material')
    def test_renderer_transaction(self):self.native('avatar_renderer_switch')
    def test_model_free_renderer_startup(self):self.native('avatar_renderer_startup')
    def test_backend_transaction(self):self.native('avatar_backend_switch')
    def test_raster_shader_paths(self):
        root=Path(__file__).resolve().parents[1];sys.path.insert(0,str(root/'portable'));spec=importlib.util.spec_from_file_location('avatar_shader_package',root/'portable/package_script_release.py');module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        renderer=(root/'src/avatar_renderer.cpp').read_text(encoding='utf-8');selected=re.findall(r'make_shader\("([^\"]+\.spv)"',renderer)
        self.assertEqual(len(selected),2);self.assertFalse(set(selected)-set(module.SHADER_NAMES),'renderer asks for shaders absent from packaged artifacts')
        self.assertNotIn('log_event("model->avatar.',renderer,'renderer refactor changed the established diagnostic prefix')
    def test_device_selection(self):self.native('avatar_device_selection',(*self.model_sources,'avatar_service.cpp'),('-municode','-lole32','-luuid'))
if __name__=='__main__':unittest.main()
