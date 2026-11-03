"""Python-only execution gate regressions; every process launch is a stub."""
import copy
import importlib.util
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest import mock

HERE=Path(__file__).resolve().parent
spec=importlib.util.spec_from_file_location('probe_runner',HERE/'run_native_surface_benchmark.py')
runner=importlib.util.module_from_spec(spec)
spec.loader.exec_module(runner)


class ProbeExecutionGateTests(unittest.TestCase):
    def setUp(self):
        self.tmp=tempfile.TemporaryDirectory(prefix='evrm-probe-gate-')
        self.addCleanup(self.tmp.cleanup)
        self.folder=Path(self.tmp.name)
        self.source=self.folder/'source.cpp';self.source.write_text('reviewed source\n')
        self.dependency=self.folder/'json.hpp';self.dependency.write_text('reviewed JSON dependency\n')
        self.sources=[self.source,self.dependency]
        self.artifacts=[self.folder/name for name in ['helper.exe','basic.spv','enhanced.spv','fixture-export.exe']]
        for index,path in enumerate(self.artifacts):path.write_bytes(b'never executed artifact '+bytes([index]))
        self.packet=self.folder/'packet.json'
        self.parts=[self.folder/name for name in ['constants.bin','inputs.bin','texture0.rgba','texture1.rgba','texture2.rgba','texture3.rgba','fixture.vrm','fixture.reference.json']]
        for index,path in enumerate(self.parts):path.write_bytes(b'known reviewed payload '+bytes([index]))
        self.packet.write_text(json.dumps({'format':'EVRM surface packet v1','fixture':'fixture.vrm','constants':'constants.bin','inputs':'inputs.bin',
            'textures':[{'file':f'texture{i}.rgba'} for i in range(4)],'reference':{'expected':'original'},
            'provenance':{'status':'actual-import-verified','fixture_file':'fixture.vrm','reference_file':'fixture.reference.json'}}))
        self.build_file=self.folder/'build.json';self.scan_file=self.folder/'scan.json';self.receipt_file=self.folder/'review.json'
        source_pins=runner.pins(self.sources);artifact_pins=runner.pins(self.artifacts)
        self.build={'source_files':source_pins,'artifacts':artifact_pins,'artifact_roles':dict(zip(['helper','basic','enhanced','exporter'],map(lambda p:str(p.resolve()),self.artifacts)))}
        self.scan={'status':'clean','scanned_files':artifact_pins}
        self.write_records()
        self.receipt={'review':{'status':'passed','source_files':source_pins,'artifacts':artifact_pins,'packet_files':runner.pins([self.packet]+self.parts),
            'build_receipt':{'path':str(self.build_file.resolve()),'sha256':runner.digest(self.build_file)},
            'scan_receipt':{'path':str(self.scan_file.resolve()),'sha256':runner.digest(self.scan_file)}},
            # Legacy inline records deliberately included: a source-only gate
            # accepts these even if the independently reviewed bytes changed.
            'build':copy.deepcopy(self.build),'bitdefender':copy.deepcopy(self.scan)}
        self.arguments=['probe-runner',str(self.packet),'--binary',str(self.artifacts[0]),'--basic',str(self.artifacts[1]),'--enhanced',str(self.artifacts[2]),
            '--report',str(self.folder/'report.json'),'--load-context','Observed test stub; no GPU execution','--receipt',str(self.receipt_file),'--execute-reviewed']

    def write_records(self):
        self.build_file.write_text(json.dumps(self.build,sort_keys=True)+'\n')
        self.scan_file.write_text(json.dumps(self.scan,sort_keys=True)+'\n')

    def invoke(self,should_launch):
        self.receipt_file.write_text(json.dumps(self.receipt))
        launcher=mock.Mock(return_value=SimpleNamespace(returncode=0,stdout='',stderr=''))
        system=SimpleNamespace(kernel32=SimpleNamespace(GetErrorMode=lambda:0,SetErrorMode=lambda _:0))
        with mock.patch.object(runner,'REVIEW_FILES',self.sources),mock.patch.object(runner.subprocess,'run',launcher),mock.patch.object(runner.ctypes,'windll',system,create=True),mock.patch.object(runner.sys,'argv',self.arguments):
            if should_launch:
                self.assertEqual(runner.main(),0)
                self.assertGreaterEqual(launcher.call_count,1)
            else:
                with self.assertRaises((ValueError,KeyError,OSError)):
                    runner.main()
                launcher.assert_not_called()
        return launcher

    def test_unchanged_exact_review_reaches_only_stub(self):self.invoke(True)

    def test_balanced_rotated_orders_reach_only_stub(self):
        launcher=self.invoke(True)
        self.assertEqual([call.args[0][-1] for call in launcher.call_args_list],['all','rotate1','rotate2'])

    def test_changed_source_rejected_before_launch(self):
        self.source.write_text('changed source\n');self.invoke(False)

    def test_changed_dependency_rejected_before_launch(self):
        self.dependency.write_text('changed JSON dependency\n');self.invoke(False)

    def test_reassembled_exe_and_matching_build_scan_cannot_reuse_review(self):self.reassemble(0)
    def test_reassembled_basic_spirv_cannot_reuse_review(self):self.reassemble(1)
    def test_reassembled_enhanced_spirv_cannot_reuse_review(self):self.reassemble(2)
    def test_reassembled_exporter_cannot_reuse_review(self):self.reassemble(3)

    def reassemble(self,index):
        path=self.artifacts[index];path.write_bytes(b'replacement bytes')
        # A new producer build+scan may describe the replacement truthfully;
        # that does not extend the independent approval of previous bytes.
        for record in (self.build['artifacts'],self.scan['scanned_files'],self.receipt['build']['artifacts'],self.receipt['bitdefender']['scanned_files']):
            record[str(path.resolve())]=runner.digest(path)
        self.write_records();self.invoke(False)

    def test_changed_constants_rejected_before_launch(self):self.change_part(0)
    def test_changed_inputs_rejected_before_launch(self):self.change_part(1)
    def test_changed_base_texture_rejected_before_launch(self):self.change_part(2)
    def test_changed_normal_texture_rejected_before_launch(self):self.change_part(3)
    def test_changed_cap_texture_rejected_before_launch(self):self.change_part(4)
    def test_changed_mask_texture_rejected_before_launch(self):self.change_part(5)
    def test_changed_fixture_rejected_before_launch(self):self.change_part(6)
    def test_changed_external_reference_rejected_before_launch(self):self.change_part(7)

    def change_part(self,index):self.parts[index].write_bytes(b'changed payload');self.invoke(False)

    def test_changed_inline_reference_rejected_before_launch(self):
        packet=json.loads(self.packet.read_text());packet['reference']['expected']='replaced';self.packet.write_text(json.dumps(packet));self.invoke(False)

    def test_changed_build_receipt_raw_bytes_rejected_before_launch(self):
        self.build_file.write_bytes(self.build_file.read_bytes()+b' ');self.invoke(False)

    def test_changed_scan_receipt_raw_bytes_rejected_before_launch(self):
        self.scan_file.write_bytes(self.scan_file.read_bytes()+b' ');self.invoke(False)

    def test_provenance_parses_the_same_bytes_that_were_hashed(self):
        with mock.patch.object(Path,'read_text',return_value='{"status":"unapproved second read"}'):
            _,record=runner.reviewed_record(self.receipt['review'],'build_receipt')
        self.assertEqual(record,self.build)

    def test_legacy_source_only_review_rejected_before_launch(self):
        self.receipt['review']={'status':'passed','source_files':runner.pins(self.sources)};self.invoke(False)

    def test_unverified_python_refreshed_packet_rejected(self):
        packet=json.loads(self.packet.read_text());packet['provenance']['status']='pending-native-export';self.packet.write_text(json.dumps(packet))
        self.receipt['review']['packet_files']=runner.pins([self.packet]+self.parts);self.invoke(False)

    def camera_packet(self):
        packet=json.loads(self.packet.read_text());packet['format']='EVRM camera packet v1';packet['provenance']['status']='python-generated-verified'
        self.packet.write_text(json.dumps(packet));self.receipt['review']['packet_files']=runner.pins([self.packet]+self.parts)
        self.build['artifact_roles']['camera']=str(self.artifacts[1].resolve());self.write_records()
        self.receipt['review']['build_receipt']['sha256']=runner.digest(self.build_file)
        self.arguments += ['--camera',str(self.artifacts[1])]

    def test_camera_exact_review_reaches_only_stub(self):
        self.camera_packet();launcher=self.invoke(True)
        self.assertEqual([call.args[0][-1] for call in launcher.call_args_list],['all','all','all'])

    def test_camera_spirv_replacement_rejected_before_launch(self):
        self.camera_packet();self.artifacts[1].write_bytes(b'replaced camera SPIR-V');self.invoke(False)

    def test_camera_single_trial_reaches_only_stub(self):
        self.camera_packet();self.arguments+=['--trials','1'];launcher=self.invoke(True)
        self.assertEqual(launcher.call_count,1)


class NormalHandednessOracleTests(unittest.TestCase):
    def test_tilted_green_has_large_opposite_handedness_signal(self):
        generator_spec=importlib.util.spec_from_file_location('probe_generator',HERE/'generated_material_fixtures.py')
        generator=importlib.util.module_from_spec(generator_spec);generator_spec.loader.exec_module(generator)
        for version in (0,1):
            with self.subTest(version=version):
                _,reference=generator.make_case(version,'normal')
                positive=reference['samples'][4]['normal'];negative=reference['samples'][12]['normal']
                self.assertGreater(positive[1],.2)
                self.assertLess(negative[1],-.2)
                self.assertGreater(positive[1]-negative[1],.4)
                self.assertAlmostEqual(positive[0],negative[0])
                self.assertAlmostEqual(positive[2],negative[2])


class CameraLiteralFixtureTests(unittest.TestCase):
    def test_camera_packet_is_deterministic_and_literal_wrap_cases_match(self):
        camera_spec=importlib.util.spec_from_file_location('camera_generator',HERE/'generated_surface_camera_fixtures.py')
        camera=importlib.util.module_from_spec(camera_spec);camera_spec.loader.exec_module(camera)
        with tempfile.TemporaryDirectory() as folder:
            a,b=Path(folder)/'a',Path(folder)/'b'
            packet=camera.generate(a);camera.generate(b)
            self.assertEqual((a/'inputs.bin').read_bytes(),(b/'inputs.bin').read_bytes())
            self.assertEqual((a/'inputs.bin').stat().st_size,packet['sample_count']*176)
            self.assertEqual((a/'packet.json').read_bytes(),(b/'packet.json').read_bytes())
            expected=dict(zip(packet['reference']['case_names'],packet['reference']['samples']))
            self.assertEqual(expected['wrap-root-positive']['normal'],[1,0,0])
            self.assertEqual(expected['wrap-root-negative']['normal'],[-1,0,0])
            self.assertEqual(expected['menu-origin-camera']['normal'],[.125,-1.25,-3])
            self.assertEqual(expected['zero-projection-fallback']['unlit'],[0,1,0])
            self.assertEqual(expected['parallel-up-forward-fallback']['unlit'],[0,1,0])
            self.assertEqual(expected['camera-pole']['unlit'],[0,0,-1])


if __name__=='__main__':unittest.main()
