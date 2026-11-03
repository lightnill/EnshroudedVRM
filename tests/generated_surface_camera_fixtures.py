"""Python-only literal world/camera fixtures; no production math/helper calls."""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct


def unit(v):
    length=math.sqrt(sum(x*x for x in v))
    return [x/length for x in v]


def cross(a,b):return [a[1]*b[2]-a[2]*b[1],a[2]*b[0]-a[0]*b[2],a[0]*b[1]-a[1]*b[0]]
def multiply(matrix,vector):return [sum(a*b for a,b in zip(row,vector)) for row in matrix]
def transpose(matrix):return list(map(list,zip(*matrix)))


def rotation_x(angle):
    c,s=math.cos(angle),math.sin(angle)
    return [[1,0,0],[0,c,-s],[0,s,c]]


def wrapped_difference(root,camera):
    # Physical periodic coordinates; independent of shader packed uint math.
    return (root-camera+16384)%32768-16384


def fixture_cases():
    identity=[[1,0,0],[0,1,0],[0,0,1]]
    reverse=[[-1,0,0],[0,1,0],[0,0,-1]]
    cases=[]
    def add(name,root=(16,32,48),origin=(8,16,24),camera=(10,20,30),position=(.5,1,2),scale=1,offset=0,
            rotation=None,up=(0,1,0),forward=(0,0,1),jitter=0,row_scale=1,degenerate=None):
        r=rotation or identity
        up,forward=unit(up),unit(forward)
        # The chosen world axes are orthogonal by construction. Row1 is
        # independently perturbed along forward to simulate perspective jitter.
        assert abs(sum(a*b for a,b in zip(up,forward)))<1e-10
        right=unit(cross(up,forward))
        rows=[]
        for axis in (right,[up[i]+forward[i]*jitter for i in range(3)],forward,forward):
            rows.append([x*row_scale for x in multiply(transpose(r),axis)]+[0])
        if degenerate=='zero':rows=[[0,0,0,0] for _ in range(4)]
        if degenerate=='parallel':rows[1]=rows[3][:]
        if degenerate=='zero-forward':rows[3]=[0,0,0,0]
        expected_up=[0,1,0] if degenerate in ('zero','parallel') else up
        delta=[wrapped_difference(root[i],camera[i])+(offset if i==0 else 0) for i in range(3)]
        rotated=multiply(r,position)
        relative=[delta[i]+scale*rotated[i] for i in range(3)]
        cases.append({'name':name,'root_world':root,'origin_world':origin,'camera_world':camera,'offset_x':offset,
            'position':position,'scale':scale,'rotation':r,'projection_rows':rows,
            'expected':{'normal':delta,'lit':[-x for x in relative],'unlit':expected_up,'rgba':relative+[0]}})
    add('translated-world')
    add('remote-actor',root=(512,32,96),origin=(496,16,80),camera=(500,28,90),position=(-1,2,.5),scale=1.75)
    add('menu-origin-camera',root=(0,0,0),origin=(0,0,0),camera=(0,1.25,3),position=(.25,.75,0),offset=.125)
    add('vrm0-reversed-basis',rotation=reverse)
    add('actor-pitched-basis',rotation=rotation_x(.61),scale=.65)
    roll=.72
    rolled_up=(-math.sin(roll),math.cos(roll),0)
    add('rolled-camera',up=rolled_up)
    add('rolled-jittered-camera',up=rolled_up,jitter=.013,row_scale=2.75)
    pitch=.63
    pitched_up=(0,math.cos(pitch),-math.sin(pitch));pitched_forward=(0,math.sin(pitch),math.cos(pitch))
    add('pitched-camera',up=pitched_up,forward=pitched_forward)
    add('pitched-jittered-reversed-basis',rotation=reverse,up=pitched_up,forward=pitched_forward,jitter=-.021,row_scale=.125)
    add('wrap-root-positive',root=(.5,1,2),origin=(32760,0,0),camera=(32767.5,1,2))
    add('wrap-root-negative',root=(32767.5,1,2),origin=(.25,0,0),camera=(.5,1,2))
    add('negative-physical-coordinates',root=(-.5,-2,-3),origin=(-8,-8,-8),camera=(.5,-1,-2),offset=-.375)
    add('offset-x',offset=1.25)
    add('camera-pole',up=(0,0,-1),forward=(0,1,0),jitter=.03)
    add('zero-projection-fallback',degenerate='zero')
    add('parallel-up-forward-fallback',degenerate='parallel')
    add('zero-forward-fallback',degenerate='zero-forward')
    add('small-projection-scale',up=rolled_up,row_scale=1e-7,jitter=.01)
    add('large-projection-scale',up=rolled_up,row_scale=1e7,jitter=-.01)
    add('quarter-meter-wrap-camera',root=(32767.75,2,3),origin=(32760,0,0),camera=(-.5,2,3),rotation=reverse,offset=.25)
    return cases


def generate(output):
    output=Path(output);output.mkdir(parents=True,exist_ok=True)
    cases=fixture_cases();payload=bytearray()
    for case in cases:
        for name in ('root_world','origin_world'):
            payload+=struct.pack('<4I',*[round(float(value)*131072)%2**32 for value in case[name]],0)
        payload+=struct.pack('<4f',*case['camera_world'],case['offset_x'])
        payload+=struct.pack('<4f',*case['position'],case['scale'])
        for row in case['rotation']:payload+=struct.pack('<4f',*row,0)
        for row in case['projection_rows']:payload+=struct.pack('<4f',*row)
    assert len(payload)==176*len(cases)
    (output/'inputs.bin').write_bytes(payload)
    source={'cases':cases,'packed_unit_meters':1/131072,'coordinate_period_meters':32768,
        'expected_method':'Literal physical positions nearest periodic difference plus offsetX; independent R*modelPosition*scale translation. Independently chosen camera axes, row1 jitter along forward; expected up is chosen axis or documented world-Y fallback. No production helpers.'}
    text=json.dumps(source,indent=2,sort_keys=True)+'\n'
    (output/'source.camera.json').write_text(text,encoding='utf-8')
    reference={'samples':[case['expected'] for case in cases],'case_names':[case['name'] for case in cases]}
    (output/'source.reference.json').write_text(json.dumps(reference,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    packet={'format':'EVRM camera packet v1','fixture':'generated-camera-cases','inputs':'inputs.bin','sample_count':len(cases),'diffuse':[1,1,1],
        'reference':reference,'provenance':{'status':'python-generated-verified','fixture_file':'source.camera.json','reference_file':'source.reference.json'}}
    (output/'packet.json').write_text(json.dumps(packet,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    manifest={str(path.name):hashlib.sha256(path.read_bytes()).hexdigest() for path in sorted(output.iterdir()) if path.is_file() and path.name!='pins.json'}
    (output/'pins.json').write_text(json.dumps(manifest,indent=2)+'\n',encoding='utf-8')
    return packet


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('output',type=Path)
    args=parser.parse_args();packet=generate(args.output)
    print(f"Generated {packet['sample_count']} camera cases; no native/GPU execution.")
