"""Run the complete discoverable local CPU suite with the pinned MSVC tools.

Real Vulkan/GPU checks are listed separately and require --include-gpu. Their
original assertions and additional input gates remain in the individual tests.
No game is launched and no installation or remote machine is changed here.
"""
from __future__ import annotations

import argparse
from contextlib import redirect_stdout
import json
import os
from pathlib import Path
import sys
import time
import unittest

from native_fixture_compiler import DEFAULT_COMPILER
from native_compiler import prepare_msvc_environment, toolchain_identity

ROOT = Path(__file__).resolve().parents[1]
WORK = Path(os.environ.get('EVRM_NATIVE_TOOLS_ROOT', 'C:/Tools'))
HARDWARE_MODULES = {'test_avatar_assets', 'test_standalone_runtime_dll', 'test_vulkan_bootstrap_dispatch'}
HARDWARE_TESTS = {
    'test_mouth_animation.MouthAnimationTests.test_gpu',
    'test_mod_menu.ModMenuTests.test_real_gpu_menu',
    'test_mod_menu.ModMenuTests.test_overlay_adapter',
    'test_mod_menu.ModMenuTests.test_overlay_failure_paths',
    'test_standalone_bootstrap.StandaloneBootstrapTests.test_actual_implicit_vulkan_discovery_and_optional_failures',
    'test_standalone_bootstrap.StandaloneBootstrapTests.test_control_malformed_manifest_without_bootstrap_or_probe_dll',
}


def flatten(suite):
    for item in suite:
        if isinstance(item, unittest.TestSuite):
            yield from flatten(item)
        else:
            yield item


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--compiler', default=os.environ.get('EVRM_TEST_COMPILER') or DEFAULT_COMPILER)
    parser.add_argument('--vulkan-include', type=Path, default=os.environ.get('EVRM_VULKAN_INCLUDE_DIRECTORY') or WORK / 'research/Vulkan-Headers/include')
    parser.add_argument('--shader-compiler', type=Path, default=os.environ.get('EVRM_SHADER_COMPILER') or WORK / 'toolchain/dxc/bin/x64/dxc.exe')
    parser.add_argument('--spirv-validator', type=Path, default=os.environ.get('EVRM_SPIRV_VALIDATOR') or WORK / 'toolchain/spirv-tools-build/tools/spirv-val.exe')
    parser.add_argument('--evidence', type=Path)
    parser.add_argument('--pattern', default='test_*.py')
    hardware_selection = parser.add_mutually_exclusive_group()
    hardware_selection.add_argument('--include-gpu', action='store_true')
    hardware_selection.add_argument('--gpu-only', action='store_true')
    parser.add_argument('--list', action='store_true', help='List selections without compiling or executing fixtures')
    args = parser.parse_args()
    if args.evidence is None:
        leaf = 'gpu-suite' if args.gpu_only else 'cpu-and-gpu-suite' if args.include_gpu else 'cpu-suite'
        args.evidence = ROOT.parent / 'private/native-fixtures' / leaf
    prepare_msvc_environment(args.compiler)
    for path in (args.vulkan_include / 'vulkan/vulkan.h', args.shader_compiler, args.spirv_validator):
        if not path.is_file():
            parser.error(f'Required native CPU suite input is missing: {path}')
    args.evidence.mkdir(parents=True, exist_ok=True)
    os.environ.update(EVRM_TEST_COMPILER=args.compiler, EVRM_FIXTURE_COMPILER=args.compiler,
                      EVRM_VULKAN_INCLUDE_DIRECTORY=str(args.vulkan_include), EVRM_VULKAN_INCLUDE=str(args.vulkan_include),
                      EVRM_SHADER_COMPILER=str(args.shader_compiler), EVRM_SPIRV_VALIDATOR=str(args.spirv_validator),
                      EVRM_NATIVE_BUILD_EVIDENCE=str(args.evidence / 'native-compiler-invocations.jsonl'))
    suite = unittest.defaultTestLoader.discover(str(ROOT / 'tests'), pattern=args.pattern)
    selected, hardware = [], []
    for test in flatten(suite):
        name = test.id()
        if name.partition('.')[0] in HARDWARE_MODULES or name in HARDWARE_TESTS:
            hardware.append(test)
        else:
            selected.append(test)
    if args.gpu_only:
        selected = hardware
    elif args.include_gpu:
        selected.extend(hardware)
    if args.list:
        print(json.dumps({'selected': [test.id() for test in selected],
                          'hardware_checks': [test.id() for test in hardware]}, indent=2))
        return 0
    started = time.monotonic()
    print(f'Running {len(selected)} tests; {len(hardware)} Vulkan/GPU checks ' +
          ('selected explicitly.' if args.include_gpu or args.gpu_only else 'listed in receipt for separate execution.'))
    with (args.evidence / 'tests.txt').open('w', encoding='utf-8') as stream, redirect_stdout(stream):
        result = unittest.TextTestRunner(stream=stream, verbosity=2).run(unittest.TestSuite(selected))
    receipt = {'identity': toolchain_identity(), 'elapsed_seconds': time.monotonic()-started,
               'run': result.testsRun, 'failures': len(result.failures), 'errors': len(result.errors),
               'skipped': [{'test': test.id(), 'reason': reason} for test, reason in result.skipped],
               'failed_tests': [test.id() for test, _ in result.failures + result.errors],
               'selection': 'gpu' if args.gpu_only else 'all' if args.include_gpu else 'cpu',
               'hardware_included': args.include_gpu or args.gpu_only, 'hardware_checks': [test.id() for test in hardware],
               'success': result.wasSuccessful()}
    (args.evidence / 'summary.json').write_text(json.dumps(receipt, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(receipt, indent=2))
    print(f'Full test output: {args.evidence / "tests.txt"}')
    return 0 if result.wasSuccessful() else 1


if __name__ == '__main__':
    sys.exit(main())
