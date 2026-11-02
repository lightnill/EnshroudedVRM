"""Explicit, reproducible standalone package builder; never a player launcher.

Reads one accepted base, explicit native inputs and exact source pins. No source
walk, download, test audio/model inclusion, scan-claim inheritance or installation.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import stat
import struct
import sys
import tempfile
import zipfile

from package_script_release import SHADER_NAMES as BASE_SHADER_NAMES, SCRIPT_NAMES, VOICE_SDK_SHA256
from package_script_release import VOICE_RUNTIME_INPUTS, MOUTH_RUNTIME_INPUTS, MENU_RUNTIME_INPUTS

ACCEPTED_BASE_SHA256='6905577b1e5afeb48659de6b7875ec6b449a625175184cd887aebc3b61483a26'
OWNED_SHADER_SHA256={'native_volume_snapshot.comp.spv':'31305bfdccb504f29b719e08d06aa4557c2402bbd5da3b63b9da36bfaf938fd5'}
SHADER_NAMES=BASE_SHADER_NAMES+('debug_report_capture.comp.spv',)
_ROOT=Path(__file__).resolve().parents[1]
TARGET_MEMBERS=tuple(re.findall(r'^\s*"([^"\r\n]+)"',(_ROOT/'src/standalone_package_members.hpp').read_text(),re.MULTILINE))
SAMPLE_MEMBER='EnshroudedVRM/Avatars/vroid-sample.vrm'
SAMPLE_SOURCE='portable/standalone_assets/vroid-sample.vrm'
SAMPLE_SHA256='da1e68ca997415de3ff55ba1a8468257c523c6d5db65d193f4110e37861bc098'
SAMPLE_BYTES=17491388
BASE_TARGET_MEMBERS=tuple(name for name in TARGET_MEMBERS if name not in (SAMPLE_MEMBER,'EnshroudedVRM/Runtime/shaders/debug_report_capture.comp.spv'))
DEBUG_REPORT_INPUTS=('src/debug_report_types.hpp','src/debug_report_policy.hpp','src/debug_report_policy.cpp',
    'src/debug_report_archive.hpp','src/debug_report_archive.cpp','src/debug_report_log.hpp','src/debug_report_sources.hpp','src/debug_report_sources.cpp',
    'src/debug_report_service.hpp','src/debug_report_service.cpp','src/debug_report_gpu.hpp','src/debug_report_gpu.cpp','shaders/debug_report_capture.hlsl')
LOCAL_MEMBERS={
    'EnshroudedVRM/README.txt':'portable/standalone_docs/README.txt',
    'EnshroudedVRM/KNOWN-ISSUES.txt':'portable/standalone_docs/KNOWN-ISSUES.txt',
    'EnshroudedVRM/MOD-SETTINGS.txt':'portable/standalone_docs/MOD-SETTINGS.txt',
    'EnshroudedVRM/RELEASE-NOTES.md':'portable/standalone_docs/RELEASE-NOTES.md',
    'EnshroudedVRM/SECURITY.md':'portable/standalone_docs/SECURITY.md',
    'EnshroudedVRM/THIRD_PARTY_NOTICES.md':'portable/standalone_docs/THIRD_PARTY_NOTICES.md',
    'EnshroudedVRM/Avatars/README.txt':'portable/standalone_docs/Avatars-README.txt',
    'EnshroudedVRM/Runtime/enshrouded_vrm_layer.json':'portable/standalone_layer.json',
}
STANDALONE_INPUTS=(
    '.gitattributes','portable/package_standalone_release.py','portable/bootstrap/build_bootstrap.py',
    'portable/build_standalone.py','portable/package_script_release.py','portable/package_portable.py',
    'portable/bootstrap/generate_exports.py','portable/bootstrap/vulkan-exports.json','portable/bootstrap/bootstrap.rc',
    'src/standalone_bootstrap_policy.hpp','src/standalone_bootstrap_policy.cpp','src/standalone_bootstrap_main.cpp',
    'src/standalone_files.hpp','src/standalone_files.cpp','src/standalone_startup.hpp','src/standalone_startup.cpp',
    'src/standalone_startup_runtime.cpp','src/standalone_startup_environment.cpp','src/standalone_settings.hpp','src/standalone_settings.cpp',
    'src/standalone_package_members.hpp',
)

RELEASE_032_INPUTS=('src/avatar_appearance.hpp', 'src/avatar_device_request.hpp', 'src/avatar_model.cpp', 'src/avatar_model.hpp', 'src/avatar_renderer.cpp', 'src/avatar_renderer.hpp', 'src/avatar_runtime.cpp', 'src/avatar_runtime.hpp', 'src/avatar_service.cpp', 'src/avatar_service.hpp', 'src/avatar_service_types.hpp', 'src/avatar_settings.cpp', 'src/avatar_settings.hpp', 'src/avatar_switch_transaction.hpp', 'src/command_forwarders.generated.inl', 'src/command_trace.cpp', 'src/command_trace.hpp', 'src/command_trace_hooks.inl', 'src/command_trace_runtime.cpp', 'src/command_trace_runtime.hpp', 'src/debug_report_archive.cpp', 'src/debug_report_archive.hpp', 'src/debug_report_gpu.cpp', 'src/debug_report_gpu.hpp', 'src/debug_report_log.hpp', 'src/debug_report_policy.cpp', 'src/debug_report_policy.hpp', 'src/debug_report_service.cpp', 'src/debug_report_service.hpp', 'src/debug_report_sources.cpp', 'src/debug_report_sources.hpp', 'src/debug_report_types.hpp', 'src/deferred_rendering.cpp', 'src/deferred_rendering.hpp', 'src/deferred_runtime.cpp', 'src/deferred_runtime.hpp', 'src/device_dispatch_cache.hpp', 'src/device_features.cpp', 'src/device_features.hpp', 'src/enshrouded_bridge.cpp', 'src/enshrouded_bridge.hpp', 'src/fallback_diagnostics.hpp', 'src/gpu_probe.cpp', 'src/gpu_probe.hpp', 'src/gpu_probe_async.inl', 'src/gpu_probe_capture_frames.hpp', 'src/gpu_probe_inline.inl', 'src/graphics_hooks.inl', 'src/graphics_state.cpp', 'src/graphics_state.hpp', 'src/import_accessor.inl', 'src/import_images.inl', 'src/import_meshes.inl', 'src/import_mouth.inl', 'src/import_normals.inl', 'src/import_uv.inl', 'src/local_voice_copy.cpp', 'src/local_voice_copy.hpp', 'src/local_voice_observation.cpp', 'src/local_voice_observation.hpp', 'src/local_voice_observer.cpp', 'src/local_voice_observer.hpp', 'src/local_voice_pe_identity.hpp', 'src/microphone_audio.hpp', 'src/microphone_backend.cpp', 'src/microphone_backend.hpp', 'src/microphone_source.cpp', 'src/microphone_source.hpp', 'src/mod_input_state.cpp', 'src/mod_input_state.hpp', 'src/mod_input_win32.cpp', 'src/mod_input_win32.hpp', 'src/mod_menu.cpp', 'src/mod_menu.hpp', 'src/mod_menu_font.generated.inl', 'src/mod_menu_gpu.cpp', 'src/mod_menu_gpu.hpp', 'src/mod_menu_status.hpp', 'src/mod_overlay.cpp', 'src/mod_overlay.hpp', 'src/mod_present_lease.hpp', 'src/mod_settings.cpp', 'src/mod_settings.hpp', 'src/mod_settings_service.cpp', 'src/mod_settings_service.hpp', 'src/mouth_data.hpp', 'src/mouth_deformation.cpp', 'src/mouth_deformation.hpp', 'src/mouth_publication.hpp', 'src/mouth_types.hpp', 'src/native_avatar_assets.cpp', 'src/native_avatar_assets.hpp', 'src/native_avatar_material.hpp', 'src/native_avatar_pose.cpp', 'src/native_avatar_pose.hpp', 'src/native_backend.cpp', 'src/native_backend.hpp', 'src/native_body_coverage.cpp', 'src/native_body_coverage.hpp', 'src/native_body_selection.cpp', 'src/native_body_selection.hpp', 'src/native_buffer_memory.cpp', 'src/native_buffer_memory.hpp', 'src/native_clip_pipelines.cpp', 'src/native_clip_pipelines.hpp', 'src/native_clip_shader.cpp', 'src/native_clip_shader.hpp', 'src/native_clip_variants.generated.inl', 'src/native_draw.cpp', 'src/native_draw.hpp', 'src/native_image_memory.cpp', 'src/native_image_memory.hpp', 'src/native_instance_qualifiers.cpp', 'src/native_instance_qualifiers.hpp', 'src/native_material_plan.cpp', 'src/native_material_plan.hpp', 'src/native_menu_owner.hpp', 'src/native_mixed_backend.inl', 'src/native_mouth.inl', 'src/native_oit_draw.cpp', 'src/native_oit_draw.hpp', 'src/native_oit_window.cpp', 'src/native_oit_window.hpp', 'src/native_owner.cpp', 'src/native_owner.hpp', 'src/native_owner_process.cpp', 'src/native_owner_process.hpp', 'src/native_owner_profile.hpp', 'src/native_pose_owner.hpp', 'src/native_prepare_contract.hpp', 'src/native_preview_handoff.cpp', 'src/native_preview_handoff.hpp', 'src/native_preview_handoff_calls.hpp', 'src/native_preview_subject.hpp', 'src/native_preview_subject_calls.hpp', 'src/native_preview_trace.hpp', 'src/native_publication.hpp', 'src/native_relative_call.cpp', 'src/native_relative_call.hpp', 'src/native_scene_contracts.generated.inl', 'src/native_skinning.cpp', 'src/native_skinning.hpp', 'src/native_skinning_gpu.cpp', 'src/native_skinning_gpu.hpp', 'src/native_state.cpp', 'src/native_state.hpp', 'src/native_submit.hpp', 'src/native_submit_timing.hpp', 'src/native_tool_call_patch.cpp', 'src/native_tool_call_patch.hpp', 'src/native_tool_pose_window.hpp', 'src/native_tool_slot_target.cpp', 'src/native_tool_slots.cpp', 'src/native_tool_slots.hpp', 'src/native_tool_slots_runtime.cpp', 'src/native_transparency_trace.cpp', 'src/native_transparency_trace.hpp', 'src/native_visual_slot_write.cpp', 'src/native_visual_slot_write.hpp', 'src/native_volume_snapshot.cpp', 'src/native_volume_snapshot.hpp', 'src/native_volume_use.cpp', 'src/native_volume_use.hpp', 'src/queue_dispatch.hpp', 'src/retarget.cpp', 'src/retarget.hpp', 'src/runtime_hook_timing.hpp', 'src/runtime_hooks.inl', 'src/runtime_log.hpp', 'src/secondary_hand_contact.cpp', 'src/secondary_hand_contact.hpp', 'src/sha256.hpp', 'src/spring_bones.cpp', 'src/spring_bones.hpp', 'src/spring_frame.hpp', 'src/spring_import.inl', 'src/standalone_bootstrap_main.cpp', 'src/standalone_bootstrap_policy.cpp', 'src/standalone_bootstrap_policy.hpp', 'src/standalone_files.cpp', 'src/standalone_files.hpp', 'src/standalone_package_members.hpp', 'src/standalone_settings.cpp', 'src/standalone_settings.hpp', 'src/standalone_startup.cpp', 'src/standalone_startup.hpp', 'src/standalone_startup_environment.cpp', 'src/standalone_startup_runtime.cpp', 'src/tool_grip.cpp', 'src/tool_grip.hpp', 'src/voice_analyser.cpp', 'src/voice_analyser.hpp', 'src/voice_automatic.cpp', 'src/voice_automatic.hpp', 'src/voice_clock.cpp', 'src/voice_clock.hpp', 'src/voice_features.cpp', 'src/voice_features.hpp', 'src/voice_input_types.hpp', 'src/voice_inputs.cpp', 'src/voice_inputs.hpp', 'src/voice_profile_io.cpp', 'src/voice_profile_io.hpp', 'src/voice_profiles.cpp', 'src/voice_profiles.hpp', 'src/voice_setup.cpp', 'src/voice_setup.hpp', 'src/voice_stream.cpp', 'src/voice_stream.hpp', 'src/voice_worker.cpp', 'src/voice_worker.hpp', 'src/vrm_asset.cpp', 'src/vrm_asset.hpp')

def sha(data):return hashlib.sha256(data).hexdigest()
def encoded(value):return (json.dumps(value,indent=2,sort_keys=True,ensure_ascii=True)+'\n').encode('utf-8')
def digest(value):return isinstance(value,str) and re.fullmatch('[0-9a-f]{64}',value) is not None


def safe_name(name):
    return isinstance(name,str) and bool(name) and len(name)<512 and not name.startswith(('/','\\')) and not any(c in name for c in '\\:\0\r\n') and all(part not in ('','.','..') and not part.endswith((' ','.')) for part in name.split('/'))


def read_local(value,label,limit):
    path=Path(value)
    if not path.is_absolute() or str(path).startswith(('\\\\','//')):raise ValueError(label+' must be an absolute local path')
    for part in (path,*path.parents):
        info=part.lstat()
        if stat.S_ISLNK(info.st_mode) or getattr(info,'st_file_attributes',0)&0x400:raise ValueError(label+' has a linked path')
    info=path.stat()
    if not stat.S_ISREG(info.st_mode) or info.st_nlink!=1 or not 0<info.st_size<=limit:raise ValueError(label+' is not a bounded, unlinked regular file')
    with path.open('rb') as stream:data=stream.read(limit+1)
    if len(data)>limit or len(data)!=info.st_size:raise ValueError(label+' changed or exceeds its limit')
    return data


def unique_json(data,label):
    def pairs(items):
        result={}
        for key,value in items:
            if key in result:raise ValueError(label+' has duplicate keys')
            result[key]=value
        return result
    try:return json.loads(data,object_pairs_hook=pairs)
    except (ValueError,UnicodeError,RecursionError) as error:raise ValueError(label+' is invalid JSON') from error


def read_base(path,expected_hash):
    if not digest(expected_hash) or expected_hash!=ACCEPTED_BASE_SHA256:raise ValueError('Only the explicitly accepted 0.2.0/docs1 base is supported')
    data=read_local(path,'base archive',128*1024*1024)
    if sha(data)!=expected_hash:raise ValueError('base archive digest mismatch')
    import io
    with zipfile.ZipFile(io.BytesIO(data)) as archive:
        entries=archive.infolist()
        if not 1<=len(entries)<=128:raise ValueError('base archive member count')
        roots=[item.filename.rsplit('/',1)[0] for item in entries if item.filename.count('/')==1 and item.filename.endswith('/release-manifest.json')]
        if len(roots)!=1 or not safe_name(roots[0]):raise ValueError('base package root')
        prefix=roots[0]+'/'
        required={name.removeprefix('EnshroudedVRM/') for name in BASE_TARGET_MEMBERS if name!='vulkan-1.dll'}|set(SCRIPT_NAMES)|{'release-manifest.json','SHA256SUMS.txt'}
        parents={prefix.rstrip('/')}|{prefix+name.rsplit('/',1)[0] for name in required if '/' in name}
        selected={};seen=set();total=0
        for entry in entries:
            name=entry.filename
            if not safe_name(name.rstrip('/')) or name.casefold() in seen:raise ValueError('unsafe or aliased base member')
            seen.add(name.casefold());kind=stat.S_IFMT(entry.external_attr>>16)
            if entry.is_dir():
                if name.rstrip('/') not in parents or kind not in (0,stat.S_IFDIR):raise ValueError('unexpected base directory')
                continue
            if not name.startswith(prefix) or name[len(prefix):] not in required or kind not in (0,stat.S_IFREG) or entry.flag_bits&1:raise ValueError('unexpected/linked/encrypted base member')
            total+=entry.file_size
            if not 0<entry.file_size<=64*1024*1024 or total>128*1024*1024:raise ValueError('base member size')
            selected[name[len(prefix):]]=archive.read(entry)
        if set(selected)!=required:raise ValueError('missing base member')
    checksum_bytes=selected.pop('SHA256SUMS.txt');checksums={}
    try:
        for line in checksum_bytes.decode('ascii').splitlines():
            match=re.fullmatch(r'([0-9a-f]{64})  (.+)',line)
            if not match or match[2] in checksums:raise ValueError('base checksum line')
            checksums[match[2]]=match[1]
    except UnicodeError as error:raise ValueError('base checksum encoding') from error
    if checksums!={name:sha(value) for name,value in selected.items()}:raise ValueError('base checksum sidecar mismatch')
    manifest=unique_json(selected.pop('release-manifest.json'),'base manifest');info=unique_json(selected['BUILD-INFO.json'],'base build info')
    if manifest.get('schema')!='enshrouded-vrm-portable-v1' or manifest.get('version')!='0.2.0' or info.get('version')!='0.2.0' or not digest(manifest.get('game_sha256')):raise ValueError('unsupported base metadata')
    files=manifest.get('files')
    if not isinstance(files,list) or len(files)!=len(selected):raise ValueError('base manifest member count')
    hashes={}
    for item in files:
        if not isinstance(item,dict) or set(item)!={'path','sha256'} or item['path'] in hashes or not digest(item['sha256']):raise ValueError('base manifest entry')
        hashes[item['path']]=item['sha256']
    if hashes!={name:sha(value) for name,value in selected.items()}:raise ValueError('base manifest digest mismatch')
    if info.get('runtimeSha256')!=sha(selected['Runtime/enshrouded_vrm_runtime.dll']) or info.get('automaticBufferExports') is not False or info.get('bundledAvatars')!=0:raise ValueError('base native/privacy metadata mismatch')
    if info.get('acceptedShaderModules')!={name:sha(selected['Runtime/shaders/'+name]) for name in BASE_SHADER_NAMES}:raise ValueError('base shader metadata mismatch')
    if sha(selected['Runtime/OVRLipSync.dll'])!=VOICE_SDK_SHA256:raise ValueError('official voice SDK pin mismatch')
    return selected,info,manifest


def required_source_inputs(base_info):
    entries=base_info.get('sourceInputs')
    if not isinstance(entries,list) or not 1<=len(entries)<=512:raise ValueError('base source inputs missing')
    names=[]
    for item in entries:
        if not isinstance(item,dict) or not safe_name(item.get('path')) or not digest(item.get('sha256')):raise ValueError('base source input invalid')
        names.append(item['path'])
    result=set(names)|set(VOICE_RUNTIME_INPUTS)|set(MOUTH_RUNTIME_INPUTS)|set(MENU_RUNTIME_INPUTS)|set(STANDALONE_INPUTS)|set(LOCAL_MEMBERS.values())|set(DEBUG_REPORT_INPUTS)|set(RELEASE_032_INPUTS)
    if any(not safe_name(name) or name.lower().endswith(('.wav','.vrm','.glb','.exe')) or '/State/' in name for name in result):raise ValueError('unsafe source pin')
    return tuple(sorted(result))


def checked_sources(root,pin_file,base_info):
    expected=required_source_inputs(base_info);data=unique_json(read_local(pin_file,'source pins',512*1024),'source pins')
    if not isinstance(data,dict) or set(data)!={'schema','inputs'} or data['schema']!='enshrouded-vrm-source-pins-v1' or not isinstance(data['inputs'],list):raise ValueError('source pin schema')
    pins={}
    for item in data['inputs']:
        if not isinstance(item,dict) or set(item)!={'path','sha256'} or not safe_name(item['path']) or item['path'] in pins or not digest(item['sha256']):raise ValueError('source pin entry')
        pins[item['path']]=item['sha256']
    if set(pins)!=set(expected):raise ValueError('source pin set differs from complete required inputs')
    payload={}
    for name in expected:
        value=read_local(root/name,'source '+name,16*1024*1024)
        if sha(value)!=pins[name]:raise ValueError('source input changed: '+name)
        payload[name]=value
    return payload,[{'path':name,'sha256':pins[name]} for name in expected]


def checked_dll(path,expected_hash,label):
    if not digest(expected_hash):raise ValueError(label+' explicit digest missing')
    data=read_local(path,label,64*1024*1024)
    if sha(data)!=expected_hash or len(data)<256 or data[:2]!=b'MZ':raise ValueError(label+' hash or PE signature')
    offset=struct.unpack_from('<I',data,0x3c)[0]
    if not 64<=offset<=len(data)-26 or data[offset:offset+4]!=b'PE\0\0' or struct.unpack_from('<H',data,offset+4)[0]!=0x8664 or not struct.unpack_from('<H',data,offset+22)[0]&0x2000 or struct.unpack_from('<H',data,offset+24)[0]!=0x20b:raise ValueError(label+' must be an x64 DLL')
    if label=='bootstrap' and 'EnshroudedVRM standalone bootstrap'.encode('utf-16le') not in data:raise ValueError('bootstrap lacks project attribution; never package a Windows system DLL')
    return data


def checked_shaders(path,base_info):
    manifest=unique_json(read_local(path,'shader manifest',128*1024),'shader manifest')
    if not isinstance(manifest,dict) or manifest.get('schema')!='enshrouded-vrm-shaders-v1' or not isinstance(manifest.get('modules'),list):raise ValueError('shader manifest schema')
    result={}
    for item in manifest['modules']:
        if not isinstance(item,dict) or set(item)!={'name','path','sha256'} or item['name'] not in SHADER_NAMES or item['name'] in result or not digest(item['sha256']):raise ValueError('shader module entry')
        data=read_local(item['path'],'shader',2*1024*1024)
        if len(data)<20 or len(data)%4 or data[:4]!=b'\x03\x02\x23\x07' or sha(data)!=item['sha256']:raise ValueError('shader bytes or explicit digest mismatch')
        expected=OWNED_SHADER_SHA256.get(item['name'])
        if expected is None and item['name'] in BASE_SHADER_NAMES:expected=base_info['acceptedShaderModules'][item['name']]
        if expected is not None and item['sha256']!=expected:raise ValueError('shader bytes differ from the explicit release pin')
        result[item['name']]=data
    if set(result)!=set(SHADER_NAMES):raise ValueError('complete shader set required')
    return result


def build_release(base_package,base_sha256,source_root,output,runtime_dll,runtime_sha256,bootstrap_dll,bootstrap_sha256,shader_manifest,source_pins,version='standalone-dev',publication_status='private-development'):
    output=Path(output).absolute();root=Path(source_root).absolute()
    if output.exists():raise FileExistsError(output)
    if not re.fullmatch('[A-Za-z0-9][A-Za-z0-9._-]{0,63}',version):raise ValueError('version invalid')
    if publication_status not in ('private-development','preview','release'):raise ValueError('publication status invalid')
    base,accepted,original=read_base(Path(base_package).absolute(),base_sha256)
    sources,pins=checked_sources(root,Path(source_pins).absolute(),accepted)
    runtime=checked_dll(Path(runtime_dll).absolute(),runtime_sha256,'runtime');bootstrap=checked_dll(Path(bootstrap_dll).absolute(),bootstrap_sha256,'bootstrap')
    shaders=checked_shaders(Path(shader_manifest).absolute(),accepted)
    sample=read_local(root/SAMPLE_SOURCE,'authorized sample avatar',64*1024*1024)
    if len(sample)!=SAMPLE_BYTES or sha(sample)!=SAMPLE_SHA256 or sample[:4]!=b'glTF':raise ValueError('authorized sample avatar bytes changed')
    payload={'EnshroudedVRM/'+name:value for name,value in base.items() if name not in SCRIPT_NAMES}
    payload['vulkan-1.dll']=bootstrap;payload['EnshroudedVRM/Runtime/enshrouded_vrm_runtime.dll']=runtime
    payload[SAMPLE_MEMBER]=sample
    for member,source in LOCAL_MEMBERS.items():payload[member]=sources[source]
    for name,value in shaders.items():payload['EnshroudedVRM/Runtime/shaders/'+name]=value
    info={'schema':'enshrouded-vrm-standalone-build-v1','version':version,'publication_status':publication_status,
          'author':'lightnill','originalCodeLicense':'MIT','basePackageSha256':base_sha256,'runtimeSha256':runtime_sha256,'bootstrapSha256':bootstrap_sha256,
          'acceptedShaderModules':{name:sha(value) for name,value in sorted(shaders.items())},'voiceSdkSha256':VOICE_SDK_SHA256,
          'automaticBufferExports':False,'bundledAvatars':1,'bundledAudio':0,'sourceInputs':pins,
          'bundledAvatar':{'path':SAMPLE_MEMBER,'sha256':SAMPLE_SHA256,'bytes':SAMPLE_BYTES,'author':'Lightnill','license':'Embedded VRM 1.0 permissions; see Avatars/README.txt'},
          'startup':'normal Steam launch; standalone game-local bootstrap; F8 settings',
          'targetPlatform':'Windows x64; exact host/loader coverage is recorded separately for each candidate',
          'verificationStatus':'Not established by packaging; use separate evidence bound to these exact native/archive hashes',
          'nativeMetadataVersion':'0.3.2','bootstrapMetadataVersion':'0.2.0-standalone-dev'}
    payload['EnshroudedVRM/BUILD-INFO.json']=encoded(info)
    if set(payload)!=set(TARGET_MEMBERS):raise ValueError('standalone member contract mismatch')
    manifest={'schema':'enshrouded-vrm-standalone-v1','version':version,'publication_status':publication_status,'game_sha256':original['game_sha256'],
              'files':[{'path':name,'sha256':sha(value)} for name,value in sorted(payload.items())]}
    payload['EnshroudedVRM/release-manifest.json']=encoded(manifest)
    output.parent.mkdir(parents=True,exist_ok=True)
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(prefix='.standalone-',suffix='.zip',dir=output.parent,delete=False) as stream:temporary=Path(stream.name)
        with zipfile.ZipFile(temporary,'w',compression=zipfile.ZIP_DEFLATED,compresslevel=9,strict_timestamps=True) as archive:
            for name,value in sorted(payload.items()):
                entry=zipfile.ZipInfo(name,(1980,1,1,0,0,0));entry.create_system=3;entry.external_attr=(stat.S_IFREG|0o644)<<16;entry.compress_type=zipfile.ZIP_DEFLATED;archive.writestr(entry,value,compress_type=zipfile.ZIP_DEFLATED,compresslevel=9)
        # Exclusive atomic publication: an existing output is never replaced.
        os.link(temporary,output);temporary.unlink();temporary=None
    finally:
        if temporary is not None:temporary.unlink(missing_ok=True)
    return {'archiveSha256':sha(output.read_bytes()),'archiveSize':output.stat().st_size,'memberCount':len(payload),'runtimeSha256':runtime_sha256,
            'bootstrapSha256':bootstrap_sha256,'basePackageSha256':base_sha256,'sourceInputCount':len(pins)}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('base-package','base-sha256','source-root','output','runtime-dll','runtime-sha256','bootstrap-dll','bootstrap-sha256','shader-manifest','source-pins'):parser.add_argument('--'+name,required=True)
    parser.add_argument('--version',default='standalone-dev')
    parser.add_argument('--publication-status',choices=('private-development','preview','release'),default='private-development');args=vars(parser.parse_args())
    try:print(json.dumps(build_release(**args),indent=2))
    except (ValueError,OSError,zipfile.BadZipFile) as error:parser.exit(1,str(error)+'\n')


if __name__=='__main__':main()
