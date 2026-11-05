"""Actual shader arithmetic and fake pipeline creation; no game, DLL or GPU."""
from native_fixture_compiler import default_compiler, run_compiler
from pathlib import Path
import ctypes, os, shutil, subprocess, tempfile, unittest


class NativeOitClipTests(unittest.TestCase):
    def test_actual_depth_bounds_cover_clipped_fragments(self):
        compiler = default_compiler()
        headers = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if os.name != 'nt' or not compiler or not headers:
            self.skipTest('Windows MSVC 14.44 and explicit Vulkan headers required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode() | 0x8003)
        root = Path(__file__).resolve().parents[1]
        shader = (root / 'shaders/native_prepare.hlsl').read_text()
        marker = '    // EVRM_OIT_W_BOUNDS_BEGIN'
        first = shader.index(marker) if marker in shader else shader.index('    const float wMin=clipMin.w')
        bounds = shader[first:shader.index('    preparedFrame.Store2(312u', first)]
        first = shader.index('    if(wMin<1.f/1024.f')
        guard = shader[first:shader.index('\n', first)]
        with tempfile.TemporaryDirectory(prefix='evrm-oit-clip-', ignore_cleanup_errors=True) as directory:
            folder = Path(directory)
            (folder / 'oit_w_bounds.inc').write_text(bounds)
            (folder / 'oit_w_guard.inc').write_text(guard)
            binary = folder / 'oit-clip.exe'
            built = run_compiler([compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                                    '-I' + str(folder), '-I' + headers, str(root / 'tests/native_oit_clip.cpp'),
                                    str(root / 'src/native_oit_draw.cpp'), '-o', str(binary)],
                                   capture_output=True, text=True, timeout=120)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(binary)], capture_output=True, text=True, timeout=30)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            print(ran.stdout.strip())


if __name__ == '__main__':
    unittest.main()
