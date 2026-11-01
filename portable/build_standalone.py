"""Developer build/freeze entry point. Never installs or starts the game."""
import argparse
import importlib.util
import json
import os
import re
from pathlib import Path
import shutil
import subprocess
import sys
import time

import package_standalone_release as package


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    for name in ('compiler-directory','bootstrap-compiler-directory','vulkan-include-directory','shader-compiler','base-package','output-directory'):parser.add_argument('--'+name,required=True)
    parser.add_argument('--powershell',default=shutil.which('pwsh'))
    parser.add_argument('--version',default='standalone-dev')
    parser.add_argument('--publication-status',choices=('private-development','preview','release'),default='private-development')
    args=parser.parse_args();root=Path(__file__).resolve().parents[1]
    if not re.fullmatch('[A-Za-z0-9][A-Za-z0-9._-]{0,63}',args.version):parser.error('version invalid')
    output=Path(args.output_directory).absolute();output.mkdir(parents=True,exist_ok=False)
    compiler=Path(args.compiler_directory).absolute();bootstrap_compiler=Path(args.bootstrap_compiler_directory).absolute();headers=Path(args.vulkan_include_directory).absolute();shader_compiler=Path(args.shader_compiler).absolute()
    if not args.powershell:parser.error('explicit PowerShell 7 developer build host required')
    # The private handoff documentation deliberately claims only this tested
    # Windows-directory layout. Do not produce differently targeted bytes under
    # those same installation instructions without a new compatibility proof.
    if str(Path(os.environ['SystemRoot']).resolve()).casefold()!=r'c:\windows':
        parser.error('this private preview supports only the validated C:\\Windows layout')
    base=Path(args.base_package).absolute();_,base_info,_=package.read_base(base,package.ACCEPTED_BASE_SHA256)
    names=package.required_source_inputs(base_info)
    pins={'schema':'enshrouded-vrm-source-pins-v1','inputs':[{'path':name,'sha256':package.sha(package.read_local(root/name,'source '+name,16*1024*1024))} for name in names]}
    pin_file=output/'source-pins.json';pin_file.write_bytes(package.encoded(pins))
    runtime_directory=output/'Runtime'
    command=[str(args.powershell),'-NoLogo','-NoProfile','-NonInteractive','-File',str(root/'build.ps1'),'-Target','Bridge',
             '-CompilerDirectory',str(compiler),'-VulkanIncludeDirectory',str(headers),'-ShaderCompiler',str(shader_compiler),
             '-OutputDirectory',str(runtime_directory),'-PortableRelease']
    begin=time.perf_counter()
    with (output/'native-build.log').open('wb') as stream:
        run=subprocess.run(command,stdout=stream,stderr=subprocess.STDOUT,cwd=root)
    if run.returncode:raise RuntimeError('native build failed; see native-build.log')
    runtime_seconds=time.perf_counter()-begin
    repeated_runtime=output/'Runtime-repeat'
    repeated_command=list(command);repeated_command[repeated_command.index('-OutputDirectory')+1]=str(repeated_runtime)
    begin=time.perf_counter()
    with (output/'native-build-repeat.log').open('wb') as stream:
        repeated=subprocess.run(repeated_command,stdout=stream,stderr=subprocess.STDOUT,cwd=root)
    if repeated.returncode:raise RuntimeError('repeated native build failed; see native-build-repeat.log')
    repeated_runtime_seconds=time.perf_counter()-begin
    if (runtime_directory/'enshrouded_vrm_runtime.dll').read_bytes()!=(repeated_runtime/'enshrouded_vrm_runtime.dll').read_bytes():raise ValueError('runtime rebuild is not deterministic')
    spec=importlib.util.spec_from_file_location('frozen_bootstrap',root/'portable/bootstrap/build_bootstrap.py')
    bootstrap_module=importlib.util.module_from_spec(spec);spec.loader.exec_module(bootstrap_module)
    begin=time.perf_counter()
    bootstrap=bootstrap_module.build_bootstrap(root,output/'bootstrap',bootstrap_compiler,bootstrap_module.system_provider())
    second=bootstrap_module.build_bootstrap(root,output/'bootstrap-repeat',bootstrap_compiler,bootstrap_module.system_provider())
    if bootstrap['dll'].read_bytes()!=second['dll'].read_bytes():raise ValueError('bootstrap rebuild is not deterministic')
    bootstrap_seconds=time.perf_counter()-begin
    modules=[{'name':name,'path':str(runtime_directory/'shaders'/name),'sha256':package.sha((runtime_directory/'shaders'/name).read_bytes())} for name in package.SHADER_NAMES]
    shader_manifest=output/'shader-manifest.private.json';shader_manifest.write_bytes(package.encoded({'schema':'enshrouded-vrm-shaders-v1','modules':modules}))
    # Re-read the complete pin set after compilation. Concurrent source changes
    # cannot be silently attributed to a frozen native build.
    package.checked_sources(root,pin_file,base_info)
    runtime=runtime_directory/'enshrouded_vrm_runtime.dll'
    inputs=dict(base_package=base,base_sha256=package.ACCEPTED_BASE_SHA256,source_root=root,runtime_dll=runtime,
                runtime_sha256=package.sha(runtime.read_bytes()),bootstrap_dll=bootstrap['dll'],bootstrap_sha256=package.sha(bootstrap['dll'].read_bytes()),
                shader_manifest=shader_manifest,source_pins=pin_file,version=args.version,publication_status=args.publication_status)
    first=output/('EnshroudedVRM-standalone-test.zip' if args.version=='standalone-dev' else 'EnshroudedVRM-'+args.version+'.zip');second_zip=output/'repeat.zip'
    receipt=package.build_release(output=first,**inputs);other=package.build_release(output=second_zip,**inputs)
    if receipt['archiveSha256']!=other['archiveSha256']:raise ValueError('package rebuild is not deterministic')
    tool_files={'compiler':compiler/'g++.exe','shaderCompiler':shader_compiler,
                'bootstrapCompiler':bootstrap_compiler/'cl.exe','bootstrapLinker':bootstrap_compiler/'link.exe',
                'vulkanCoreHeader':headers/'vulkan/vulkan_core.h','vulkanLayerHeader':headers/'vulkan/vk_layer.h'}
    receipt.update(nativeBuildSeconds=runtime_seconds,repeatedNativeBuildSeconds=repeated_runtime_seconds,bootstrapTwoBuildSeconds=bootstrap_seconds,
                   deterministicRuntime=True,deterministicBootstrap=True,deterministicArchive=True,
                   bootstrapProviderModule=json.loads((output/'bootstrap/toolchain.json').read_bytes())['providerModule'],
                   toolInputs={name:package.sha(path.read_bytes()) for name,path in tool_files.items()},
                   sourcePinsSha256=package.sha(pin_file.read_bytes()),shaderHashes={item['name']:item['sha256'] for item in modules},
                   publicationStatus=args.publication_status,version=args.version,verificationStatus='build only; candidate tests/review/scans recorded separately')
    (output/'build-receipt.json').write_bytes(package.encoded(receipt))
    (output/'SHA256SUMS.txt').write_text(receipt['archiveSha256']+'  '+first.name+'\n'+inputs['runtime_sha256']+'  Runtime/enshrouded_vrm_runtime.dll\n'+inputs['bootstrap_sha256']+'  bootstrap/vulkan-1.dll\n',encoding='ascii')
    print(json.dumps(receipt,indent=2))


if __name__=='__main__':main()
