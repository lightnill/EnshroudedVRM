"""Native menu/settings/input fixtures; no game, microphone or global input."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest
from native_voice_fixture import stage_voice_library


class ModMenuTests(unittest.TestCase):
    def run_native(self, name, sources, flags=()):
        compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        if not compiler or os.name != 'nt':
            self.skipTest('Windows x64 GCC required')
        root = Path(__file__).resolve().parents[1]
        with tempfile.TemporaryDirectory(prefix='evrm-menu-', ignore_cleanup_errors=True) as folder:
            if 'voice_analyser.cpp' in sources:
                stage_voice_library(folder)
            binary = Path(folder) / (name + '.exe')
            command = [compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', str(root / 'tests' / (name + '.cpp'))]
            command.extend(str(root / 'src' / source) for source in sources)
            if name in ('mod_present_lease', 'mod_menu_gpu', 'mod_overlay', 'mod_overlay_faults'):
                command.append('-I' + os.environ['EVRM_VULKAN_INCLUDE_DIRECTORY'])
            if name in ('mod_menu_gpu', 'mod_overlay', 'mod_overlay_faults'):
                command.append('-I' + str(root / 'vendor/imgui'))
            if name in ('mod_menu', 'avatar_menu', 'mod_menu_gpu', 'mod_overlay', 'mod_overlay_faults'):
                upstream_sources = ['imgui.cpp', 'imgui_draw.cpp', 'imgui_widgets.cpp', 'imgui_tables.cpp']
                if name in ('mod_menu_gpu', 'mod_overlay', 'mod_overlay_faults'):
                    upstream_sources.append('backends/imgui_impl_vulkan.cpp')
                for upstream in upstream_sources:
                    obj = Path(folder) / (Path(upstream).stem + '.o')
                    build_flags = ['-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES', '-I' + str(root / 'vendor/imgui')]
                    if name in ('mod_menu_gpu', 'mod_overlay', 'mod_overlay_faults'):
                        build_flags += ['-I' + os.environ['EVRM_VULKAN_INCLUDE_DIRECTORY']]
                    built = subprocess.run([compiler, '-std=c++17', '-O2', '-c', *build_flags, str(root / 'vendor/imgui' / upstream),
                                            '-o', str(obj)], capture_output=True, text=True, timeout=90)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    command.append(str(obj))
            if name in ('mod_settings', 'mod_settings_service', 'mod_menu_gpu'):
                command.append('-municode')
            result = subprocess.run(command + list(flags) + ['-o', str(binary)], capture_output=True, text=True, timeout=180)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            if name == 'mod_overlay_faults':
                for scenario in ('reset', 'font', 'recovery'):
                    with self.subTest(scenario=scenario):
                        result = subprocess.run([str(binary), folder, scenario], capture_output=True, text=True, timeout=30)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn('PASS ', result.stdout)
                        print(result.stdout.strip())
                return
            result = subprocess.run([str(binary), folder], capture_output=True, text=True, timeout=60)
            self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
            self.assertIn('PASS ', result.stdout)
            print(result.stdout.strip())
            if name == 'mod_menu_gpu' and os.environ.get('EVRM_UI_CAPTURE_DIRECTORY'):
                destination = Path(os.environ['EVRM_UI_CAPTURE_DIRECTORY'])
                destination.mkdir(parents=True, exist_ok=True)
                for capture in Path(folder).glob('menu-*.ppm'):
                    shutil.copyfile(capture, destination / capture.name)
            if name == 'mod_input_win32':
                for scenario in ('--unexpected-import', '--cursor-snapshot-failure', '--cursor-visibility-failure',
                                 '--paired-raw-keys', '--paired-raw-release'):
                    with self.subTest(scenario=scenario):
                        result = subprocess.run([str(binary), folder, scenario], capture_output=True, text=True, timeout=30)
                        self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                        self.assertIn('PASS ', result.stdout)
                        print(result.stdout.strip())

    def test_settings(self):
        self.run_native('mod_settings', ('mod_settings.cpp',))

    def test_deferred_startup(self):
        self.run_native('voice_setup', ('voice_setup.cpp', 'voice_worker.cpp', 'voice_analyser.cpp', 'voice_automatic.cpp', 'voice_features.cpp',
                                       'voice_profiles.cpp', 'voice_stream.cpp', 'voice_clock.cpp'))

    def test_settings_service(self):
        self.run_native('mod_settings_service', ('mod_settings.cpp', 'mod_settings_service.cpp'))

    def test_input_state(self):
        self.run_native('mod_input_state', ('mod_input_state.cpp',))

    def test_queue_dispatch(self):
        self.run_native('queue_dispatch', ())

    def test_win32_input(self):
        self.run_native('mod_input_win32', ('mod_input_win32.cpp', 'mod_input_state.cpp'),
                        ('-DEVRM_MOD_INPUT_TESTING=1', '-luser32'))

    def test_menu_interaction(self):
        self.run_native('mod_menu', ('mod_menu.cpp',))

    def test_avatar_menu(self):
        self.run_native('avatar_menu', ('mod_menu.cpp',))

    def test_present_ownership(self):
        self.run_native('mod_present_lease', ())

    def test_real_gpu_menu(self):
        self.run_native('mod_menu_gpu', ('mod_menu.cpp', 'mod_menu_gpu.cpp'), ('-luser32',))

    def test_overlay_adapter(self):
        self.run_native('mod_overlay', ('mod_menu.cpp', 'mod_menu_gpu.cpp', 'mod_overlay.cpp'), ('-luser32',))

    def test_overlay_failure_paths(self):
        self.run_native('mod_overlay_faults', ('mod_menu.cpp', 'mod_menu_gpu.cpp', 'mod_overlay.cpp'), ('-luser32',))


if __name__ == '__main__':
    unittest.main()
