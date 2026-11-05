"""CPU-only subject publication fixtures; no game or Vulkan execution."""
from native_fixture_compiler import default_compiler, run_compiler
from pathlib import Path
import os, shutil, subprocess, tempfile, unittest
class NativePlayerPublicationTests(unittest.TestCase):
    def test_subject_publication(self):
        compiler=default_compiler()
        if not compiler:self.skipTest('C++17 compiler required')
        root=Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-player-publication-',ignore_cleanup_errors=True) as folder:
            binary=Path(folder)/'publication.exe'
            built=run_compiler([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static',str(root/'tests/native_player_publication.cpp'),'-o',str(binary)],capture_output=True,text=True,timeout=180)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            print(run.stdout.strip())
    def test_actual_cpu_subject_builds(self):
        compiler=default_compiler()
        if not compiler:self.skipTest('C++17 compiler required')
        root=Path(__file__).resolve().parents[1]
        source=(root/'src/avatar_renderer.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='evrm-player-cpu-',ignore_cleanup_errors=True) as folder:
            start=source.index('struct AvatarCpuModel {');end=source.index('struct AvatarRenderer::Impl {',start)
            (Path(folder)/'player_cpu_model.inc').write_text(source[start:end])
            binary=Path(folder)/'cpu.exe'
            sources=('retarget.cpp','vrm_asset.cpp','spring_bones.cpp','secondary_hand_contact.cpp','tool_grip.cpp','native_avatar_pose.cpp','native_skinning.cpp','native_material_plan.cpp','mouth_deformation.cpp','native_instance_qualifiers.cpp')
            include=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
            if not include:self.skipTest('EVRM_VULKAN_INCLUDE_DIRECTORY required')
            built=run_compiler([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-DEVRM_PLAYER_CPU_FIXTURE','-I'+folder,'-I'+include,str(root/'tests/native_player_publication.cpp'),*[str(root/'src'/name) for name in sources],'-o',str(binary)],capture_output=True,text=True,timeout=180)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            print(run.stdout.strip())
    def test_player_renderer_methods(self):
        compiler=default_compiler()
        if not compiler:self.skipTest('C++17 compiler required')
        root=Path(__file__).resolve().parents[1]
        source=(root/'src/avatar_renderer.cpp').read_text()
        with tempfile.TemporaryDirectory(prefix='evrm-player-renderer-',ignore_cleanup_errors=True) as folder:
            if 'AvatarRenderer::prepared_player_model(' in source:
                start=source.index('std::shared_ptr<const PreparedAvatar> AvatarRenderer::prepared_player_model(')
                end=source.index('bool AvatarRenderer::before_present_players(',start)
                (Path(folder)/'player_renderer_methods.inc').write_text(source[start:end])
            binary=Path(folder)/'renderer.exe'
            built=run_compiler([compiler,'-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-DEVRM_PLAYER_RENDERER_FIXTURE','-I'+folder,str(root/'tests/native_player_publication.cpp'),'-o',str(binary)],capture_output=True,text=True,timeout=180)
            self.assertEqual(built.returncode,0,built.stdout+built.stderr)
            run=subprocess.run([str(binary)],capture_output=True,text=True,timeout=30)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            print(run.stdout.strip())
if __name__=='__main__':unittest.main()
