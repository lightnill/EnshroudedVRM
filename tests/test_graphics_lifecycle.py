"""Production graphics lifecycle checks with fake callbacks; no GPU or game."""
from pathlib import Path
import ctypes
import os
import shutil
import subprocess
import tempfile
import unittest


class GraphicsLifecycleTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.compiler = os.environ.get('EVRM_TEST_COMPILER') or shutil.which('g++')
        cls.headers = os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY')
        if os.name != 'nt' or not cls.compiler or not cls.headers:
            raise unittest.SkipTest('Windows GCC and explicit Vulkan headers required')
        ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode() | 0x8003)
        cls.root = Path(__file__).resolve().parents[1]

    def run_native(self, name, sources=(), scenarios=(None,), imgui=False, temporary_argument=False):
        with tempfile.TemporaryDirectory(prefix='evrm-lifecycle-', ignore_cleanup_errors=True) as folder:
            binary = Path(folder) / (name + '.exe')
            if name == 'native_owner_refresh':
                backend = (self.root / 'src/native_backend.cpp').read_text(encoding='utf-8')
                start = backend.index('    bool refresh_census_owner(')
                end = backend.index('    unsigned owner_failure_notices=', start)
                (Path(folder) / 'native_owner_refresh.inc').write_text(backend[start:end], encoding='utf-8')
                start = backend.index('            NativeOwnerObservationStats owner_stats;bool owner_failed=false;')
                end = backend.index('            if(ready&&b.pose->pose_owner){', start)
                (Path(folder) / 'native_owner_submit.inc').write_text(backend[start:end], encoding='utf-8')
            if name == 'runtime_hook_wiring':
                runtime = (self.root / 'src/runtime_bridge.cpp').read_text(encoding='utf-8')
                sections = (
                    ('template<class Handle> std::shared_ptr<DeviceState> device_state(', 'template<class Chain>'),
                    ('template<class Function> Function device_proc(', 'PFN_vkSetDeviceLoaderData device_loader_callback'),
                    ('template<class Function> void observe_graphics(', '// This proof moves only independent binding commands'))
                code = []
                for begin, end in sections:
                    start = runtime.index(begin)
                    code.append(runtime[start:runtime.index(end, start)])
                (Path(folder) / 'runtime_hook_helpers.inc').write_text('\n'.join(code), encoding='utf-8')
            if name == 'device_dispatch_cache':
                runtime = (self.root / 'src/runtime_bridge.cpp').read_text(encoding='utf-8')
                start = runtime.index('template<class Function> Function device_proc(')
                end = runtime.index('template<class Function> void observe(', start)
                (Path(folder) / 'device_dispatch_helper.inc').write_text(runtime[start:end], encoding='utf-8')
            command = [self.compiler, '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static',
                       '-I' + self.headers, '-I' + folder, str(self.root / 'tests' / (name + '.cpp'))]
            command.extend(str(self.root / 'src' / source) for source in sources)
            if name=='mod_overlay_busy_gpu':
                command.extend([str(self.root/'src/debug_report_gpu.cpp'),str(self.root/'tests/report_service_fixture.cpp')])
            if imgui:
                imgui_root = self.root / 'vendor/imgui'
                command.append('-I' + str(imgui_root))
                for upstream in ('imgui.cpp', 'imgui_draw.cpp', 'imgui_widgets.cpp', 'imgui_tables.cpp',
                                 'backends/imgui_impl_vulkan.cpp'):
                    obj = Path(folder) / (Path(upstream).stem + '.o')
                    built = subprocess.run([self.compiler, '-std=c++17', '-O2', '-c',
                                            '-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES',
                                            '-I' + self.headers, '-I' + str(imgui_root),
                                            str(imgui_root / upstream), '-o', str(obj)],
                                           capture_output=True, text=True, timeout=90)
                    self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
                    command.append(str(obj))
            built = subprocess.run(command + ['-o', str(binary)],
                                   capture_output=True, text=True, timeout=180)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            for scenario in scenarios:
                with self.subTest(scenario=scenario):
                    args = [str(binary)] + ([folder] if temporary_argument else []) + ([scenario] if scenario else [])
                    env = os.environ.copy()
                    log = Path(folder) / ((scenario or name) + '.log')
                    env['ENSHROUDED_VRM_LOG'] = str(log)
                    result = subprocess.run(args, capture_output=True, text=True, timeout=30, env=env)
                    self.assertEqual(result.returncode, 0, result.stdout + result.stderr)
                    self.assertIn('PASS ', result.stdout)
                    if scenario in ('stale-retire', 'stale-close'):
                        self.assertNotIn('mod-menu.close.', log.read_text() if log.exists() else '')
                    print(result.stdout.strip())

    def test_pose_snapshot_lifetime(self):
        self.run_native('gpu_readback_lifetime', ('gpu_probe.cpp',))

    def test_overlay_busy_recovery(self):
        self.run_native('mod_overlay_busy', ('mod_overlay.cpp',),
                        ('present', 'gpu', 'draw', 'fatal', 'present-fatal', 'consumed', 'retry',
                         'stale-retire', 'stale-close'))

    def test_menu_fence_readiness(self):
        self.run_native('mod_overlay_busy_gpu', ('mod_menu.cpp',), imgui=True)

    def test_native_volume_snapshot(self):
        self.run_native('native_volume_snapshot', ('native_volume_snapshot.cpp',),
                        ('laptop', '1440p', 'small', 'over-budget', 'actual-allocation',
                         'packed-filter', 'view-failure'))

    def test_pose_ambiguity_detail(self):
        self.run_native('pose_ambiguity', ('enshrouded_bridge.cpp',),
                        ('palette', 'groups', 'generic', 'valid', 'guid-bounds', 'many-groups', 'many-parts'), temporary_argument=True)

    def test_local_pose_owner(self):
        self.run_native('local_pose_owner', ('enshrouded_bridge.cpp',),
                        ('foreign-palette', 'foreign-actor', 'foreign-missing', 'foreign-bad-mesh',
                         'owned-palette', 'owned-two-groups', 'not-drawn-owned', 'truncated-owned',
                         'wrong-source', 'wrong-epoch', 'empty-owner', 'no-guards', 'duplicate-owner',
                         'bad-handle', 'source-range', 'source-generation', 'menu', 'unfiltered-two', 'shared-menu-source',
                         'owned-catalog-count', 'owned-catalog-geometry', 'owned-catalog-duplicates', 'unfiltered-catalog', 'map-world', 'map-menu'), temporary_argument=True)

    def test_local_pose_owner_sampler(self):
        self.run_native('local_pose_owner_sampler', (),
                        ('cold', 'reuse', 'generation', 'descriptor', 'epoch', 'retry', 'empty', 'backing',
                         'zero-epoch', 'same', 'entity', 'handle', 'record', 'allocation', 'submission-epoch',
                         'owner-root', 'owner-service', 'census', 'submission-descriptor', 'consumer-drop', 'lifecycle-pending',
                         'lifecycle-gap', 'negative-owner', 'benchmark'))

    def test_native_owner_census_storage(self):
        self.run_native('native_owner_census_storage', ('native_owner.cpp',),
                        ('allocations', 'selected-header', 'selected-alias', 'empty'))

    def test_native_owner_refresh(self):
        self.run_native('native_owner_refresh', ('native_body_selection.cpp', 'native_instance_qualifiers.cpp'),
                        ('latest-null', 'latest-other-view', 'source-publication-missing', 'source-publication-asset',
                         'expired-pose', 'render-source', 'mixed-source', 'excluded-source', 'mask', 'qualifier',
                         'owner-changed', 'verify-failed', 'capture-failed', 'allocation', 'backing',
                         'deadline', 'deadline-after-verify', 'clock-regressed', 'clock-quantization',
                         'clock-true-deadline', 'fresh-success'))

    def test_runtime_hook_timing(self):
        self.run_native('runtime_hook_timing', (),
                        ('aggregate', 'sampling', 'phase-coverage', 'present', 'post-present', 'attach', 'nested', 'regressed',
                         'no-target', 'threads', 'per-device', 'benchmark'))

    def test_runtime_hook_wiring(self):
        self.run_native('runtime_hook_wiring')

    def test_device_dispatch_cache(self):
        self.run_native('device_dispatch_cache', (),
                        ('actual-known', 'aliases', 'null', 'unknown', 'uninitialized', 'device', 'resolver',
                         'owned-names', 'invalid', 'capture-failed', 'duplicates', 'reseed', 'handle-reuse', 'threads', 'benchmark'))

    def test_native_submit_timing(self):
        self.run_native('native_submit_timing', (),
                        ('correlated-worst', 'threshold', 'nested', 'scope', 'inactive', 'early-return',
                         'exception', 'invalid', 'double-begin', 'counters', 'bounded', 'benchmark'))

    def test_native_local_selector_absent(self):
        self.run_native('native_local_selector', ('native_owner.cpp',), ('zero',))

    def test_native_local_selector_explicit(self):
        self.run_native('native_local_selector', ('native_owner.cpp',), ('selected',))

    def test_native_local_selector_guards(self):
        self.run_native('native_local_selector', ('native_owner.cpp',),
                        ('changed', 'stale-selector', 'generation', 'unrelated', 'missing-input', 'missing-equipment',
                         'header', 'binding', 'predicate', 'service-alias', 'empty', 'missing-key', 'racing'))

    def test_native_owner_census(self):
        self.run_native('native_owner_census', ('native_owner.cpp',),
                        ('reuse', 'unchanged', 'adapter', 'cleared', 'image', 'consumer', 'prefix-change', 'equipment', 'ambiguous',
                         'prefix-race', 'read-fault', 'calls', 'bytes', 'deadline', 'clock-regressed', 'identity',
                         'batched', 'batched-budget', 'split-clock', 'split-deadline', 'benchmark', 'benchmark-batched'))

    def test_native_owner_read_batch(self):
        self.run_native('native_owner_read_batch', ('native_owner.cpp',),
                        ('dense', 'disabled', 'fresh', 'mismatch', 'ordered', 'invalid-later', 'read-fault',
                         'gap-fault', 'all-gaps', 'call-limit', 'byte-limit', 'exhausted', 'deadline', 'regressed',
                         'allocation', 'cap', 'groups-cap', 'wide', 'separate', 'boundary', 'benchmark'))


if __name__ == '__main__':
    unittest.main()
