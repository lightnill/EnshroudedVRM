"""Deterministic, personal-data-free VRM surface fixtures and independent oracles.

Only the standard library is used.  This module never runs a GPU program.
Pixel expectations are derived from literal PNG pixels and the documented
VRM0/VRM1 equations, independently of production parser/shader helpers.
"""
from __future__ import annotations
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import zlib

BASE = [(64, 96, 128, 255), (64, 96, 128, 128), (64, 96, 128, 0), (64, 96, 128, 255)]
CAP = (128, 64, 192, 255)
MASK = [(0, 0, 0, 255), (255, 255, 255, 255), (128, 128, 128, 255), (64, 128, 192, 255)]
NORMAL = [(128, 128, 255, 255), (204, 192, 230, 255)]
RIM = (0.25, 0.5, 0.75)
DIFFUSE = (0.5, 0.75, 1.0)


def png(width: int, height: int, pixels: bytes) -> bytes:
    assert len(pixels) == width * height * 4
    def chunk(kind, data):
        return struct.pack('>I', len(data)) + kind + data + struct.pack('>I', zlib.crc32(kind + data) & 0xffffffff)
    rows = b''.join(b'\0' + pixels[y * width * 4:(y + 1) * width * 4] for y in range(height))
    return b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', width, height, 8, 6, 0, 0, 0)) + chunk(b'IDAT', zlib.compress(rows, 9)) + chunk(b'IEND', b'')


def srgb(byte: int) -> float:
    value = byte / 255
    return value / 12.92 if value <= 0.04045 else ((value + 0.055) / 1.055) ** 2.4


def unit(value):
    length = math.sqrt(sum(x*x for x in value))
    return [x/length for x in value]


def make_case(version=1, kind='masked', size=4):
    """Return a GLB, independent reference metadata, and literal pixel samples."""
    data = bytearray()
    doc = {'asset': {'version': '2.0', 'generator': 'EVRM procedural material fixture v1'},
           'scene': 0, 'scenes': [{'nodes': [0, 15]}], 'nodes': [], 'skins': [],
           'buffers': [], 'bufferViews': [], 'accessors': [], 'images': [],
           'samplers': [{'magFilter': 9728, 'minFilter': 9728, 'wrapS': 33071, 'wrapT': 33071}], 'textures': []}
    def raw(payload):
        while len(data) % 4: data.append(0)
        index = len(doc['bufferViews'])
        doc['bufferViews'].append({'buffer': 0, 'byteOffset': len(data), 'byteLength': len(payload)})
        data.extend(payload)
        return index
    def accessor(values, fmt, shape, count, minimum=None, maximum=None):
        index = len(doc['accessors'])
        value = {'bufferView': raw(struct.pack('<' + fmt * len(values), *values)), 'componentType': {'f':5126, 'H':5123}[fmt], 'count':count, 'type':shape}
        if minimum is not None: value['min'] = minimum
        if maximum is not None: value['max'] = maximum
        doc['accessors'].append(value)
        return index
    def texture(width, height, pixels, name):
        image = len(doc['images'])
        doc['images'].append({'name':name, 'bufferView':raw(png(width, height, pixels)), 'mimeType':'image/png'})
        doc['textures'].append({'source':image, 'sampler':0})
        return image
    # One hips-weighted quad, fifteen required humanoid bones and one skin.
    bones = ['hips','spine','head','leftUpperLeg','leftLowerLeg','leftFoot','rightUpperLeg','rightLowerLeg','rightFoot','leftUpperArm','leftLowerArm','leftHand','rightUpperArm','rightLowerArm','rightHand']
    parents = [-1,0,1,0,3,4,0,6,7,1,9,10,1,12,13]
    for i, name in enumerate(bones):
        doc['nodes'].append({'name':name, 'translation':[0, 1 if i == 0 else .1, 0], 'children':[]})
        if parents[i] >= 0: doc['nodes'][parents[i]]['children'].append(i)
    doc['nodes'].append({'name':'Procedural quad', 'mesh':0, 'skin':0})
    position = accessor([-.5,0,0,.5,0,0,.5,1,0,-.5,1,0], 'f','VEC3',4,[-.5,0,0],[.5,1,0])
    normal = accessor([0,0,1]*4, 'f','VEC3',4)
    uv = accessor([0,0,1,0,1,1,0,1], 'f','VEC2',4)
    uv1 = accessor([.125,0,.875,0,.875,1,.125,1], 'f','VEC2',4)
    tangent = accessor([1,0,0,1]*4,'f','VEC4',4)
    joints = accessor([0,0,0,0]*4,'H','VEC4',4)
    weights = accessor([1,0,0,0]*4,'f','VEC4',4)
    indices = accessor([0,1,2,0,2,3],'H','SCALAR',6)
    ibm = accessor([1,0,0,0,0,1,0,0,0,0,1,0,0,0,0,1],'f','MAT4',1)
    doc['skins'] = [{'joints':[0], 'inverseBindMatrices':ibm, 'skeleton':0}]
    attrs = {'POSITION':position,'NORMAL':normal,'TEXCOORD_0':uv,'TEXCOORD_1':uv1,'TANGENT':tangent,'JOINTS_0':joints,'WEIGHTS_0':weights}
    primitives = [{'attributes':attrs,'indices':indices,'material':0,'mode':4}]
    # Large case repeats the literal four regions at 1024 square, exercising
    # decoded storage/upload/cache footprint without including downloaded art.
    width, height = (size, size) if size > 4 else (4, 1)
    def stripes(colors):
        return bytes(c for _y in range(height) for x in range(width) for c in colors[min(len(colors)-1, x*len(colors)//width)])
    base = texture(width,height,stripes(BASE),'Base alpha witnesses')
    matcap = texture(width,height,bytes(CAP)*(width*height),'Constant sRGB matcap')
    mask = texture(width,height,stripes(MASK),'Black white gray colored sRGB mask')
    normalmap = texture(2,1,bytes(c for p in NORMAL for c in p),'Linear normal controls')
    material = {'name':'Surface fixture', 'doubleSided':True, 'alphaMode':'BLEND' if kind=='mixed_alpha' else 'MASK', 'alphaCutoff':.5,
                'pbrMetallicRoughness':{'baseColorFactor':[1,1,1,1], 'baseColorTexture':{'index':base},'metallicFactor':0,'roughnessFactor':1}}
    has_normal = kind in ('normal','neutral')
    has_cap = kind not in ('map_free','rim_only','neutral')
    has_rim = kind not in ('map_free','matcap_only','neutral')
    has_mask = kind not in ('map_free','matcap_only','rim_only','neutral','missing_mask')
    mix = .25 if kind != 'defaults' else (0 if version==0 else 1)
    factor = (1,1,1) if version==0 else (.5,.75,.25)
    if has_normal: material['normalTexture'] = {'index':normalmap,'scale':0 if kind=='neutral' else .75}
    used = ['VRM' if version==0 else 'VRMC_vrm']
    human = {bone:{'node':i} for i,bone in enumerate(bones)}
    if version==0:
        props = {'name':material['name'],'shader':'VRM/MToon','renderQueue':3000 if kind=='mixed_alpha' else 2450,
                 'floatProperties':{'_RimFresnelPower':2,'_RimLift':.1,'_BumpScale':.75},
                 'vectorProperties':{'_Color':[1,1,1,1], '_RimColor':list(RIM if has_rim else (0,0,0))+[1]},
                 'textureProperties':{'_MainTex':base}}
        if kind!='defaults': props['floatProperties']['_RimLightingMix']=mix
        if has_normal: props['textureProperties']['_BumpMap']=normalmap;props['floatProperties']['_BumpScale']=0 if kind=='neutral' else .75
        if has_cap: props['textureProperties']['_SphereAdd']=matcap
        if has_mask: props['textureProperties']['_RimTexture']=mask
        doc['extensions'] = {'VRM':{'specVersion':'0.0','meta':{'title':'Procedural material fixture','author':'Local generated test','version':'1','allowedUserName':'Everyone','violentUssageName':'Disallow','sexualUssageName':'Disallow','commercialUssageName':'Disallow','licenseName':'CC0'},
                                   'humanoid':{'humanBones':[{'bone':k,'node':v['node']} for k,v in human.items()]},'materialProperties':[props]}}
    else:
        mtoon = {'specVersion':'1.0', 'matcapFactor':list(factor), 'parametricRimColorFactor':list(RIM if has_rim else (0,0,0)), 'parametricRimFresnelPowerFactor':2,'parametricRimLiftFactor':.1}
        if kind!='defaults': mtoon['rimLightingMixFactor']=mix
        if has_cap: mtoon['matcapTexture']={'index':matcap}
        if has_mask: mtoon['rimMultiplyTexture']={'index':mask,'texCoord':1}
        if kind=='transformed': mtoon['rimMultiplyTexture']['extensions']={'KHR_texture_transform':{'offset':[.1,.2],'scale':[.5,.75],'rotation':.25}};used.append('KHR_texture_transform')
        if kind!='map_free': material['extensions']={'VRMC_materials_mtoon':mtoon};used.append('VRMC_materials_mtoon')
        doc['extensions'] = {'VRMC_vrm':{'specVersion':'1.0','meta':{'name':'Procedural material fixture','version':'1','authors':['Local generated test'],'licenseUrl':'https://creativecommons.org/publicdomain/zero/1.0/','avatarPermission':'everyone','allowExcessivelyViolentUsage':False,'allowExcessivelySexualUsage':False,'commercialUsage':'personalNonProfit','allowPoliticalOrReligiousUsage':False,'allowAntisocialOrHateUsage':False,'creditNotation':'unnecessary','allowRedistribution':True,'modification':'allowModificationRedistribution'},'humanoid':{'humanBones':human}}}
    doc['materials']=[material]
    if kind=='mixed_alpha':
        witness={'name':'Opaque alpha witness','alphaMode':'OPAQUE','pbrMetallicRoughness':{'baseColorFactor':[.2,.4,.6,1],'metallicFactor':0,'roughnessFactor':1}}
        witness_attrs=dict(attrs)
        witness_attrs['POSITION']=accessor([-.5,0,-.01,.5,0,-.01,.5,1,-.01,-.5,1,-.01], 'f','VEC3',4,[-.5,0,-.01],[.5,1,-.01])
        doc['materials'].append(witness);primitives.append({'attributes':witness_attrs,'indices':indices,'material':1,'mode':4})
        if version==0: doc['extensions']['VRM']['materialProperties'].append({'name':witness['name'],'shader':'VRM/MToon'})
    doc['meshes']=[{'name':'Generated skinned quad','primitives':primitives}]
    doc['extensionsUsed']=used
    doc['buffers']=[{'byteLength':len(data)}]
    while len(data)%4:data.append(0)
    encoded=json.dumps(doc,sort_keys=True,separators=(',',':'),ensure_ascii=True).encode()
    encoded+=b' '*((-len(encoded))%4)
    glb=struct.pack('<III',0x46546c67,2,28+len(encoded)+len(data))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(data),0x004e4942)+data
    samples=[]
    # Test both tangent handedness signs with neutral and tilted texels, four
    # independent mask coordinates and alpha witnesses. These are shader inputs,
    # not an assertion that rasterization samples these exact locations.
    for i in range(16):
        normal_index=(i//4)%2
        handedness=1 if i<8 else -1
        n=[0.,0.,1.]
        if has_normal and kind!='neutral':
            texel=NORMAL[normal_index]
            n=unit([(texel[0]/255*2-1)*.75,(texel[1]/255*2-1)*.75*handedness,texel[2]/255*2-1])
        view=[math.sqrt(.75),0,.5]
        fresnel=min(1,max(0,1-sum(a*b for a,b in zip(n,view))+.1))**2
        cap=[srgb(CAP[c])*factor[c] if has_cap else 0 for c in range(3)]
        rim=[RIM[c]*fresnel if has_rim else 0 for c in range(3)]
        mask_value=[srgb(x) for x in MASK[i%4][:3]] if has_mask else [1,1,1]
        if kind=='transformed':
            # Probe mask UV inputs are already transformed by the CPU exporter;
            # the independent metadata retains source coordinates and transform.
            u=(i%4+.5)/4;v=.5
            u=.1+math.cos(.25)*(.5*u)-math.sin(.25)*(.75*v)
            mask_value=[srgb(x) for x in MASK[min(3,max(0,int(u*4)))][:3]]
        if version==0:
            lit=[mix*rim[c]*mask_value[c] for c in range(3)]
            unlit=[cap[c]+(1-mix)*rim[c]*mask_value[c] for c in range(3)]
        else:
            lit=[mix*(cap[c]+rim[c])*mask_value[c] for c in range(3)]
            unlit=[(1-mix)*(cap[c]+rim[c])*mask_value[c] for c in range(3)]
        rgba=[(srgb(BASE[i%4][c])+lit[c])*DIFFUSE[c]+unlit[c] for c in range(3)]+[BASE[i%4][3]/255]
        samples.append({'normal':n,'lit':lit,'unlit':unlit,'rgba':rgba,'base_uv':[(i%4+.5)/4,.5], 'normal_uv':[(normal_index+.5)/2,.5], 'rim_source_uv':[(i%4+.5)/4,.5], 'tangent':[1,0,0,handedness], 'view':view,'camera_up':[0,1,0]})
    meta={'version':version,'kind':kind,'size':[width,height],'diffuse':DIFFUSE,'samples':samples,
          'expected_features':(1 if has_normal and kind!='neutral' else 0)|(2 if has_cap else 0)|(4 if has_rim else 0)|(8 if has_mask and (has_rim or version==1 and has_cap) else 0),
          'oracle':'Literal PNG bytes; IEC sRGB piecewise transfer; normal decode/normalize; power(clamp(1-dot(N,V)+lift),power); generation-specific VRM mask and light split. No production helper calls.',
          'limit':'Material-kernel pixel inputs and compute timings only; no raster/game FPS, visibility, silhouette, native GBuffer or in-game alpha acceptance.'}
    return glb,meta


def generate(output: Path):
    output.mkdir(parents=True,exist_ok=True)
    records=[]
    for version in (0,1):
        for kind in ('masked','defaults','normal','neutral','map_free','missing_mask','matcap_only','rim_only','mixed_alpha','transformed','large'):
            if kind=='transformed' and version==0:continue
            name=f'vrm{version}-{kind}'
            payload,reference=make_case(version,kind,1024 if kind=='large' else 4)
            (output/(name+'.vrm')).write_bytes(payload)
            (output/(name+'.reference.json')).write_text(json.dumps(reference,indent=2,sort_keys=True)+'\n',encoding='utf-8')
            records.append({'name':name,'bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest(),'reference_sha256':hashlib.sha256((output/(name+'.reference.json')).read_bytes()).hexdigest()})
    for kind in ('normal-reference','normal-type','rim-uv','rim-mix'):
        original,_=make_case(1,'masked')
        json_size=struct.unpack_from('<I',original,12)[0]
        doc=json.loads(original[20:20+json_size])
        binary=original[28+json_size:]
        material=doc['materials'][0]
        if kind=='normal-reference':material['normalTexture']={'index':999}
        elif kind=='normal-type':material['normalTexture']={'index':True}
        elif kind=='rim-uv':material['extensions']['VRMC_materials_mtoon']['rimMultiplyTexture']['texCoord']=7
        else:material['extensions']['VRMC_materials_mtoon']['rimLightingMixFactor']=1.5
        encoded=json.dumps(doc,sort_keys=True,separators=(',',':')).encode();encoded+=b' '*((-len(encoded))%4)
        payload=struct.pack('<III',0x46546c67,2,28+len(encoded)+len(binary))+struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(binary),0x004e4942)+binary
        name='invalid-'+kind
        (output/(name+'.vrm')).write_bytes(payload)
        records.append({'name':name,'bytes':len(payload),'sha256':hashlib.sha256(payload).hexdigest(),'expected_import':'reject'})
    manifest={'format':'EVRM generated material fixtures v1','cases':records,'personal_assets':False,'gpu_execution':False}
    (output/'manifest.json').write_text(json.dumps(manifest,indent=2,sort_keys=True)+'\n',encoding='utf-8')
    return manifest


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('output',type=Path)
    args=parser.parse_args()
    manifest=generate(args.output)
    print(f"Generated {len(manifest['cases'])} procedural VRMs; no native/GPU program executed.")
