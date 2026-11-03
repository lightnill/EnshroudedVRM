"""Inspect or explicitly execute the independently reviewed/scanned GPU helper.

Default is inspection only. --execute-reviewed requires a local review receipt
with exact current source, build-artifact and Bitdefender-scanned hashes. It is
never called by unittest discovery. The process has a finite outer timeout.
"""
from __future__ import annotations
import argparse
import ctypes
import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import time

SOURCE=Path(__file__).resolve().parents[1]
REVIEW_FILES=[SOURCE/'tests/native_surface_benchmark.cpp',SOURCE/'tests/native_surface_probe.hlsl',
              SOURCE/'tests/native_surface_fixture_export.cpp',SOURCE/'tests/generated_material_fixtures.py',
              SOURCE/'tests/run_native_surface_benchmark.py',SOURCE/'tests/test_native_surface_probe_gate.py',
              SOURCE/'shaders/native_surface.hlsli',SOURCE/'src/native_surface_material.hpp',
              SOURCE/'src/native_avatar_material.hpp',SOURCE/'vendor/tinygltf/json.hpp',
              SOURCE/'tests/generated_surface_camera_fixtures.py',SOURCE/'tests/native_surface_camera_probe.hlsl',
              SOURCE/'shaders/native_surface_camera.hlsli',SOURCE/'shaders/native_material_radiance.hlsli',
              SOURCE/'tests/native_material_radiance_probe.hlsl']


def digest(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def pins(paths):
    return {str(Path(path).resolve()):digest(path) for path in paths}


def require_pins(record,expected,label):
    normalized={str(Path(path).resolve()).casefold():value.lower() for path,value in record.items()}
    for path,value in expected.items():
        if normalized.get(path.casefold())!=value:
            raise ValueError(f'{label} missing or mismatched SHA256 for {path}')


def child_file(folder,name):
    if not isinstance(name,str) or Path(name).name!=name or name in ('.','..') or ':' in name or '\\' in name or '/' in name:
        raise ValueError('packet filename must be a single local filename')
    return folder/name


def reviewed_record(review,name):
    """The independent reviewer binds raw provenance bytes, not just claims."""
    entry=review[name]
    path=Path(entry['path'])
    if not path.is_absolute():raise ValueError(f'{name} path must be absolute')
    raw=path.read_bytes()
    if hashlib.sha256(raw).hexdigest()!=entry['sha256'].lower():raise ValueError(f'{name} raw receipt bytes changed after independent review')
    return path,json.loads(raw)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('packet',type=Path)
    parser.add_argument('--binary',required=True,type=Path)
    parser.add_argument('--basic',type=Path)
    parser.add_argument('--enhanced',type=Path)
    parser.add_argument('--camera',type=Path,help='camera-only actual helper SPIR-V; material --basic/--enhanced then unused')
    parser.add_argument('--exporter',type=Path,help='CPU packet producer whose exact bytes are also reviewed; never executed by this runner')
    parser.add_argument('--report',required=True,type=Path)
    parser.add_argument('--load-context',default='NOT OBSERVED; inspection only')
    parser.add_argument('--device',type=int,default=0)
    parser.add_argument('--samples',type=int,default=64)
    parser.add_argument('--width',type=int,default=512)
    parser.add_argument('--height',type=int,default=512)
    parser.add_argument('--mode',choices=['all','Basic','Auto','Enhanced'],default='all')
    parser.add_argument('--trials',type=int,default=3,help='balanced rotated mode-order trials; each process retains raw timings')
    parser.add_argument('--receipt',type=Path)
    parser.add_argument('--execute-reviewed',action='store_true')
    args=parser.parse_args()
    if not (0<=args.device<32 and 32<=args.samples<=256 and 16<=args.width<=1024 and 1<=args.height<=1024):
        parser.error('device/sample/resolution outside bounded helper limits')
    if not (1<=args.trials<=9):parser.error('trial count must be 1-9')
    packet_bytes=args.packet.read_bytes();packet_sha256=hashlib.sha256(packet_bytes).hexdigest();packet=json.loads(packet_bytes)
    camera_only=packet.get('format')=='EVRM camera packet v1'
    if not camera_only and packet.get('format')!='EVRM surface packet v1':raise ValueError('unknown packet format')
    if not camera_only and args.mode=='all' and args.trials%3!=0:
        parser.error('material all-mode trials must be 3, 6 or 9 for balanced order')
    if camera_only:
        if not args.camera:raise ValueError('camera packet requires --camera actual helper SPIR-V')
        args.basic=args.enhanced=args.camera;artifacts=[args.binary,args.camera];roles=['helper','camera']
    else:
        if not args.basic or not args.enhanced:raise ValueError('material packet requires --basic and --enhanced SPIR-V')
        exporter=args.exporter or args.binary.parent/'fixture-export.exe'
        artifacts=[args.binary,args.basic,args.enhanced,exporter];roles=['helper','basic','enhanced','exporter']
    source_pins=pins(REVIEW_FILES);artifact_pins=pins(artifacts)
    packet_files=[args.packet]
    names=[packet['inputs']] if camera_only else [packet['constants'],packet['inputs']]+[entry['file'] for entry in packet['textures']]
    for name in names:
        packet_files.append(child_file(args.packet.parent,name))
    provenance=packet.get('provenance',{})
    if provenance:
        packet_files += [child_file(args.packet.parent,provenance['fixture_file']),child_file(args.packet.parent,provenance['reference_file'])]
    before={'source_files':source_pins,'artifacts':artifact_pins,'packet_files':pins(packet_files)}
    before['packet_files'][str(args.packet.resolve())]=packet_sha256
    args.report.parent.mkdir(parents=True,exist_ok=True)
    if not args.execute_reviewed:
        print(json.dumps({'executed':False,'required_receipt_schema':{
            'review':{'status':'PENDING independent exact-byte re-review','source_files':source_pins,'artifacts':artifact_pins,'packet_files':before['packet_files'],
                'build_receipt':{'path':'ABSOLUTE build-pins.json path','sha256':'reviewer-approved raw build receipt SHA256'},
                'scan_receipt':{'path':'ABSOLUTE Bitdefender receipt path','sha256':'reviewer-approved raw scan receipt SHA256'}}},
            'build_receipt_required_fields':{'source_files':source_pins,'artifacts':artifact_pins,'artifact_roles':dict(zip(roles,map(lambda p:str(p.resolve()),artifacts)))},
            'scan_receipt_required_fields':{'status':'clean','scanned_files':artifact_pins},'current_pins':before,'packet_provenance':provenance},indent=2))
        return 0
    if os.name!='nt':raise ValueError('reviewed helper is Windows-only')
    if not args.receipt:raise ValueError('--execute-reviewed requires --receipt from independent source review and exact Bitdefender scans')
    if args.load_context.startswith('NOT OBSERVED') or len(args.load_context)<8:
        raise ValueError('record observed GPU/game/system load context before execution; do not assume idle hardware')
    receipt_bytes=args.receipt.read_bytes();receipt_sha256=hashlib.sha256(receipt_bytes).hexdigest()
    receipt=json.loads(receipt_bytes)
    if receipt.get('review',{}).get('status')!='passed':raise ValueError('independent source review has not passed')
    review=receipt['review']
    require_pins(review['source_files'],source_pins,'source review')
    require_pins(review['artifacts'],artifact_pins,'independent artifact review')
    require_pins(review['packet_files'],before['packet_files'],'independent packet/reference review')
    expected_provenance='python-generated-verified' if camera_only else 'actual-import-verified'
    if provenance.get('status')!=expected_provenance:raise ValueError('packet requires verified producer output after fixture changes')
    build_path,build=reviewed_record(review,'build_receipt')
    scan_path,scan=reviewed_record(review,'scan_receipt')
    if scan.get('status')!='clean':raise ValueError('exact artifact Bitdefender scan is not clean')
    require_pins(build['source_files'],source_pins,'build source')
    require_pins(build['artifacts'],artifact_pins,'build artifact')
    for role,path in zip(roles,artifacts):
        if Path(build['artifact_roles'][role]).resolve()!=path.resolve():raise ValueError(f'build provenance role mismatch: {role}')
    require_pins(scan['scanned_files'],artifact_pins,'Bitdefender scan')
    provenance_files=[args.receipt,build_path,scan_path]
    before['provenance_files']={str(args.receipt.resolve()):receipt_sha256,str(build_path.resolve()):review['build_receipt']['sha256'].lower(),str(scan_path.resolve()):review['scan_receipt']['sha256'].lower()}
    ctypes.windll.kernel32.SetErrorMode(ctypes.windll.kernel32.GetErrorMode()|0x8003)
    started=time.monotonic()
    receipt_output=args.report.with_suffix('.execution.json')
    execution={'executed':True,'pins_before':before,'review_receipt':str(args.receipt.resolve()),
               'review_receipt_sha256':receipt_sha256,'load_context':args.load_context,'timeout_seconds_per_trial':240,'trials':[]}
    for trial in range(args.trials):
        current={'source_files':pins(REVIEW_FILES),'artifacts':pins(artifacts),'packet_files':pins(packet_files),'provenance_files':pins(provenance_files)}
        if current!=before:raise ValueError('reviewed bytes changed before trial launch')
        report=args.report if trial==0 else args.report.with_name(args.report.stem+f'.trial{trial+1}'+args.report.suffix)
        mode='all' if camera_only else args.mode if args.mode!='all' or trial%3==0 else f'rotate{trial%3}'
        order=['Basic','Auto','Enhanced'];order=order[trial%3:]+order[:trial%3] if args.mode=='all' else [args.mode]
        if camera_only:order=['Camera']
        command=[str(args.binary.resolve()),str(args.packet.resolve()),str(args.basic.resolve()),str(args.enhanced.resolve()),
                 str(report.resolve()),args.load_context,str(args.device),str(args.samples),str(args.width),str(args.height),mode]
        outcome={'trial':trial+1,'command':command,'mode_order':order,'report':str(report.resolve())}
        try:
            result=subprocess.run(command,capture_output=True,text=True,timeout=240,creationflags=subprocess.CREATE_NO_WINDOW)
            outcome.update(returncode=result.returncode,stdout=result.stdout,stderr=result.stderr)
        except subprocess.TimeoutExpired as error:
            outcome.update(returncode=124,timed_out=True,stdout=str(error.stdout or ''),stderr=str(error.stderr or ''))
        execution['trials'].append(outcome);execution['returncode']=outcome['returncode']
        if outcome['returncode']!=0:break
    execution['elapsed_seconds']=time.monotonic()-started
    execution['pins_after']={'source_files':pins(REVIEW_FILES),'artifacts':pins(artifacts),'packet_files':pins(packet_files),'provenance_files':pins(provenance_files)}
    execution['pins_unchanged']=execution['pins_after']==before
    receipt_output.write_text(json.dumps(execution,indent=2)+'\n',encoding='utf-8')
    for outcome in execution['trials']:
        print(outcome.get('stdout',''),end='')
        print(outcome.get('stderr',''),file=sys.stderr,end='')
    if not execution['pins_unchanged']:raise ValueError('source/artifact/packet changed during GPU execution; result is not accepted')
    return execution['returncode']


if __name__=='__main__':
    try:sys.exit(main())
    except (ValueError,KeyError,OSError) as error:
        print(f'Not executed/accepted: {error}',file=sys.stderr)
        sys.exit(1)
