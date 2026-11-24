param(
    [Parameter(Mandatory)][string]$ReleaseZip,
    [Parameter(Mandatory)][string]$VulkanIncludeDirectory,
    [Parameter(Mandatory)][string]$ShaderCompiler,
    [string]$OutputDirectory = (Join-Path $PSScriptRoot 'build'),
    [string]$CompilerDirectory = 'C:/Program Files/Microsoft Visual Studio/2022/Community/VC/Tools/MSVC/14.44.35207/bin/Hostx64/x64'
)
$ErrorActionPreference = 'Stop'
$taskProjectRoot = $PSScriptRoot
$ReleaseZip = (Resolve-Path -LiteralPath $ReleaseZip).Path
$PortableRelease = $true
$CompilerDirectory = [IO.Path]::GetFullPath($CompilerDirectory)
$VulkanIncludeDirectory = [IO.Path]::GetFullPath($VulkanIncludeDirectory)
$ShaderCompiler = [IO.Path]::GetFullPath($ShaderCompiler)
$OutputDirectory = [IO.Path]::GetFullPath($OutputDirectory)
$taskCompiler = Join-Path $CompilerDirectory 'cl.exe'
if (-not (Test-Path -LiteralPath $taskCompiler)) { throw "Pinned MSVC compiler not found: $taskCompiler" }
$taskPython = if ($env:EVRM_PYTHON) { $env:EVRM_PYTHON } else { (Get-Command python.exe -ErrorAction Stop).Source }
$taskCompilerDriver = Join-Path $taskProjectRoot 'portable/native_compiler.py'
# Basic function accepts existing compiler-style arguments without PowerShell
# interpreting their switches. The driver rejects unreviewed options and compilers.
function Invoke-TaskCompiler {
    $taskRelativeArguments = foreach ($taskArgument in $args) {
        $taskArgument = [string]$taskArgument
        if ($taskArgument.StartsWith($taskProjectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            '.' + $taskArgument.Substring($taskProjectRoot.Length)
        } elseif ($taskArgument.StartsWith('-I' + $taskProjectRoot + [IO.Path]::DirectorySeparatorChar, [StringComparison]::OrdinalIgnoreCase)) {
            '-I.' + $taskArgument.Substring($taskProjectRoot.Length + 2)
        } else { $taskArgument }
    }
    & $taskPython $taskCompilerDriver $taskCompiler @taskRelativeArguments
    $global:LASTEXITCODE = $LASTEXITCODE
}
if (-not (Test-Path -LiteralPath (Join-Path $VulkanIncludeDirectory 'vulkan/vulkan.h'))) { throw 'Vulkan headers not found.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$taskInputs = Join-Path $OutputDirectory 'release-inputs'
& $taskPython (Join-Path $taskProjectRoot 'portable/release_inputs.py') $ReleaseZip $taskInputs
if ($LASTEXITCODE -ne 0) { throw 'Could not read the pinned release inputs.' }
$taskSavedPath = $env:PATH
$taskSavedInclude = $env:INCLUDE
Push-Location -LiteralPath $taskProjectRoot
try {
    # Obtain the same pinned SDK environment as the compiler for rc.exe too.
    $taskPreparedEnvironment = & $taskPython '-c' 'import json,sys; from portable.native_compiler import prepare_msvc_environment; env=prepare_msvc_environment(sys.argv[1]); print(json.dumps({k:env[k] for k in ("PATH","INCLUDE")}))' $taskCompiler
    if ($LASTEXITCODE -ne 0) { throw 'MSVC toolchain validation failed.' }
    $taskPreparedEnvironment = $taskPreparedEnvironment | ConvertFrom-Json
    $env:PATH = $taskPreparedEnvironment.PATH
    $env:INCLUDE = $taskPreparedEnvironment.INCLUDE
    $taskFlags = @(('-I'+$taskInputs), '-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', ('-I' + $VulkanIncludeDirectory))
    $taskTrimSourceRoot = '/d1trimfile:' + $taskProjectRoot + [IO.Path]::DirectorySeparatorChar
    if($PortableRelease){$taskFlags+=@('-fstack-protector-strong','/Z7','/Brepro',$taskTrimSourceRoot,'-Wl,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp')}
    if (-not (Test-Path -LiteralPath $ShaderCompiler)) { throw 'Shader compiler not found.' }
    $taskShaderOutput = Join-Path $OutputDirectory 'shaders'
    New-Item -ItemType Directory -Path $taskShaderOutput -Force | Out-Null
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-T' 'vs_6_0' '-E' 'vs_main' (Join-Path $taskProjectRoot 'shaders/avatar.hlsl') '-Fo' (Join-Path $taskShaderOutput 'avatar.vert.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Avatar vertex shader compilation failed.' }
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-T' 'ps_6_0' '-E' 'ps_main' (Join-Path $taskProjectRoot 'shaders/avatar.hlsl') '-Fo' (Join-Path $taskShaderOutput 'avatar.frag.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Avatar fragment shader compilation failed.' }
    foreach ($taskNativeStage in @(@('vs_native','vs_6_0','avatar_native.pull.vert.spv'),@('ps_visibility','ps_6_0','avatar_native.pull.visibility.spv'),@('ps_gbuffer','ps_6_0','avatar_native.pull.gbuffer.spv'))) {
        & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-DEVRM_NATIVE_MATERIAL_UBO=1' '-DEVRM_NATIVE_VERTEX_PULL=1' '-T' $taskNativeStage[1] '-E' $taskNativeStage[0] (Join-Path $taskProjectRoot 'shaders/avatar_native.hlsl') '-Fo' (Join-Path $taskShaderOutput $taskNativeStage[2])
        if ($LASTEXITCODE -ne 0) { throw 'Native output shader compilation failed.' }
    }
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-T' 'cs_6_0' '-E' 'cs_prepare' (Join-Path $taskProjectRoot 'shaders/native_prepare.hlsl') '-Fo' (Join-Path $taskShaderOutput 'native_prepare.comp.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Native preparation shader compilation failed.' }
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-T' 'cs_6_0' '-E' 'cs_prepare' '-WX' (Join-Path $taskProjectRoot 'shaders/native_menu_prepare.hlsl') '-Fo' (Join-Path $taskShaderOutput 'native_menu_prepare.comp.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Native menu preparation shader compilation failed.' }
    foreach($taskSkinStage in @(@('cs_skin','native_skinning.comp.spv'),@('cs_finalize','native_skinning.finalize.spv'))){
        & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-T' 'cs_6_0' '-E' $taskSkinStage[0] (Join-Path $taskProjectRoot 'shaders/native_skinning.hlsl') '-Fo' (Join-Path $taskShaderOutput $taskSkinStage[1])
        if($LASTEXITCODE -ne 0){throw 'Native skinning shader compilation failed.'}
    }
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-DEVRM_NATIVE_MATERIAL_UBO=1' '-DEVRM_NATIVE_VERTEX_PULL=1' '-T' 'ps_6_0' '-E' 'ps_oit' (Join-Path $taskProjectRoot 'shaders/avatar_native_oit.hlsl') '-Fo' (Join-Path $taskShaderOutput 'avatar_native.pull.oit.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Native transparency shader compilation failed.' }
    foreach($taskSurfaceStage in @(@('vs_native','vs_6_0','avatar_native.hlsl','avatar_native.surface.vert.spv'),@('ps_gbuffer','ps_6_0','avatar_native.hlsl','avatar_native.surface.gbuffer.spv'),@('ps_oit','ps_6_0','avatar_native_oit.hlsl','avatar_native.surface.oit.spv'))){
        & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-DEVRM_NATIVE_MATERIAL_UBO=1' '-DEVRM_NATIVE_VERTEX_PULL=1' '-DEVRM_NATIVE_SURFACE=1' '-T' $taskSurfaceStage[1] '-E' $taskSurfaceStage[0] (Join-Path $taskProjectRoot ('shaders/'+$taskSurfaceStage[2])) '-Fo' (Join-Path $taskShaderOutput $taskSurfaceStage[3])
        if($LASTEXITCODE -ne 0){throw 'Enhanced material shader compilation failed.'}
    }
    foreach($taskSurfacePrepare in @(@('native_prepare.hlsl','native_prepare.surface.comp.spv'),@('native_menu_prepare.hlsl','native_menu_prepare.surface.comp.spv'))){
        & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-DEVRM_NATIVE_SURFACE=1' '-T' 'cs_6_0' '-E' 'cs_prepare' (Join-Path $taskProjectRoot ('shaders/'+$taskSurfacePrepare[0])) '-Fo' (Join-Path $taskShaderOutput $taskSurfacePrepare[1])
        if($LASTEXITCODE -ne 0){throw 'Enhanced camera shader compilation failed.'}
    }
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-DEVRM_SKIN_SURFACE=1' '-T' 'cs_6_0' '-E' 'cs_skin' (Join-Path $taskProjectRoot 'shaders/native_skinning.hlsl') '-Fo' (Join-Path $taskShaderOutput 'native_skinning.surface.comp.spv')
    if($LASTEXITCODE -ne 0){throw 'Enhanced skin shader compilation failed.'}
    foreach($taskShadowStage in @(@('vs_shadow','vs_6_0','vert'),@('ps_shadow','ps_6_0','frag'))){
        & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-T' $taskShadowStage[1] '-E' $taskShadowStage[0] (Join-Path $taskProjectRoot 'shaders/avatar_native_shadow.hlsl') '-Fo' (Join-Path $taskShaderOutput ('avatar_native.shadow.'+$taskShadowStage[2]+'.spv'))
        if($LASTEXITCODE -ne 0){throw 'Native shadow shader compilation failed.'}
    }
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-T' 'cs_6_0' '-E' 'cs_probe_frame' (Join-Path $taskProjectRoot 'shaders/native_mtoon_probe_frame.hlsl') '-Fo' (Join-Path $taskShaderOutput 'native_mtoon_probe_frame.comp.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Native MToon probe frame shader compilation failed.' }
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.2' '-fvk-use-dx-layout' '-WX' '-T' 'cs_6_0' '-E' 'cs_shade' (Join-Path $taskProjectRoot 'shaders/native_mtoon_shade.hlsl') '-Fo' (Join-Path $taskShaderOutput 'native_mtoon_shade.comp.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Native MToon shading shader compilation failed.' }
    foreach($taskMToonWork in @(@('cs_init','native_mtoon_work.init.comp.spv'),@('cs_bounds','native_mtoon_work.bounds.comp.spv'),@('cs_arguments','native_mtoon_work.arguments.comp.spv'),@('cs_finish','native_mtoon_work.finish.comp.spv'))){
        & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-fvk-use-dx-layout' '-WX' '-T' 'cs_6_0' '-E' $taskMToonWork[0] (Join-Path $taskProjectRoot 'shaders/native_mtoon_work.hlsl') '-Fo' (Join-Path $taskShaderOutput $taskMToonWork[1])
        if ($LASTEXITCODE -ne 0) { throw 'Native MToon bounded work shader compilation failed.' }
    }
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-fvk-use-dx-layout' '-WX' '-T' 'cs_6_0' '-E' 'cs_composite' (Join-Path $taskProjectRoot 'shaders/native_mtoon_composite.hlsl') '-Fo' (Join-Path $taskShaderOutput 'native_mtoon_composite.comp.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Native MToon composite shader compilation failed.' }
    foreach($taskForwardStage in @(@('vs_native','vs_6_0','avatar_native_mtoon_forward_capture.hlsl','avatar_native.mtoon-forward.vert.spv'),@('ps_forward_capture','ps_6_0','avatar_native_mtoon_forward_capture.hlsl','avatar_native.mtoon-forward.capture.spv'),@('cs_shade','cs_6_0','native_mtoon_forward_shade.hlsl','native_mtoon_forward_shade.comp.spv'),@('cs_shade','cs_6_0','native_mtoon_forward_finish.hlsl','native_mtoon_forward_finish.comp.spv'))){
        & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.2' '-fvk-use-dx-layout' '-WX' '-T' $taskForwardStage[1] '-E' $taskForwardStage[0] (Join-Path $taskProjectRoot ('shaders/'+$taskForwardStage[2])) '-Fo' (Join-Path $taskShaderOutput $taskForwardStage[3])
        if($LASTEXITCODE -ne 0){throw 'Native MToon forward shader compilation failed'}
    }
    foreach($taskMToonRaster in @(@('avatar_native_mtoon_main.hlsl','vs_native','vs_6_0','avatar_native.mtoon.vert.spv'),@('avatar_native_mtoon_main.hlsl','ps_visibility','ps_6_0','avatar_native.mtoon.visibility.spv'),@('avatar_native_mtoon_main.hlsl','ps_gbuffer','ps_6_0','avatar_native.mtoon.gbuffer.spv'),@('avatar_native_mtoon_outline.hlsl','vs_outline','vs_6_0','avatar_native.mtoon-outline.vert.spv'),@('avatar_native_mtoon_outline.hlsl','ps_outline_visibility','ps_6_0','avatar_native.mtoon-outline.visibility.spv'),@('avatar_native_mtoon_outline.hlsl','ps_outline_gbuffer','ps_6_0','avatar_native.mtoon-outline.gbuffer.spv'),@('avatar_native_mtoon_shadow.hlsl','vs_shadow','vs_6_0','avatar_native.mtoon-shadow.vert.spv'),@('avatar_native_mtoon_shadow.hlsl','ps_shadow','ps_6_0','avatar_native.mtoon-shadow.frag.spv'))){
        & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.2' '-fvk-use-dx-layout' '-WX' '-T' $taskMToonRaster[2] '-E' $taskMToonRaster[1] (Join-Path $taskProjectRoot ('shaders/'+$taskMToonRaster[0])) '-Fo' (Join-Path $taskShaderOutput $taskMToonRaster[3])
        if ($LASTEXITCODE -ne 0) { throw 'Native MToon raster shader compilation failed.' }
    }
    foreach($taskMToonFamily in @('fallback','ordinary','enhanced')){
        $taskMToonDefines=@('-DEVRM_NATIVE_MATERIAL_UBO=1','-DEVRM_NATIVE_VERTEX_PULL=1','-DEVRM_NATIVE_SURFACE_ID=1')
        if($taskMToonFamily -eq 'fallback'){$taskMToonDefines+='-DEVRM_NATIVE_MTOON_FALLBACK=1'}
        if($taskMToonFamily -eq 'enhanced'){$taskMToonDefines+='-DEVRM_NATIVE_SURFACE=1'}
        foreach($taskMToonStage in @(@('vs_native','vs_6_0','vert'),@('ps_visibility','ps_6_0','visibility'),@('ps_gbuffer','ps_6_0','gbuffer'))){
            & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.2' '-fvk-use-dx-layout' '-WX' @taskMToonDefines '-T' $taskMToonStage[1] '-E' $taskMToonStage[0] (Join-Path $taskProjectRoot 'shaders/avatar_native.hlsl') '-Fo' (Join-Path $taskShaderOutput ('avatar_native.mtoon-'+$taskMToonFamily+'.'+$taskMToonStage[2]+'.spv'))
            if($LASTEXITCODE -ne 0){throw 'Native MToon fallback/identity shader compilation failed.'}
        }
    }
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-T' 'cs_6_0' '-E' 'cs_snapshot' (Join-Path $taskProjectRoot 'shaders/native_volume_snapshot.hlsl') '-Fo' (Join-Path $taskShaderOutput 'native_volume_snapshot.comp.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Native volume snapshot shader compilation failed.' }
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-T' 'cs_6_0' '-E' 'main' (Join-Path $taskProjectRoot 'shaders/debug_report_capture.hlsl') '-Fo' (Join-Path $taskShaderOutput 'debug_report_capture.comp.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Debug report capture shader compilation failed.' }
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-T' 'cs_6_0' '-E' 'main' (Join-Path $taskProjectRoot 'shaders/native_player_commit.hlsl') '-Fo' (Join-Path $taskShaderOutput 'native_player_commit.comp.spv')
    if ($LASTEXITCODE -ne 0) { throw 'Shared player commit shader compilation failed.' }
    $taskSources = @('runtime_bridge.cpp','device_features.cpp','command_trace.cpp','command_trace_runtime.cpp','deferred_rendering.cpp','deferred_runtime.cpp','gpu_probe.cpp','graphics_state.cpp','avatar_renderer.cpp','enshrouded_bridge.cpp','vrm_asset.cpp','retarget.cpp','spring_bones.cpp','tool_grip.cpp','secondary_hand_contact.cpp','native_tool_slots.cpp','native_tool_slot_target.cpp','native_tool_slots_runtime.cpp','native_visual_slot_write.cpp','native_tool_call_patch.cpp','native_avatar_pose.cpp','native_state.cpp','native_draw.cpp','native_backend.cpp','native_owner.cpp','native_owner_process.cpp','native_clip_shader.cpp','native_clip_pipelines.cpp','native_buffer_memory.cpp','native_body_coverage.cpp','native_body_selection.cpp','native_instance_qualifiers.cpp','native_oit_window.cpp','native_transparency_trace.cpp','native_image_memory.cpp','native_material_plan.cpp','native_volume_snapshot.cpp','native_oit_draw.cpp','native_mtoon_scene.cpp','native_mtoon_composite.cpp','native_mtoon_shade.cpp','native_mtoon_work.cpp','native_mtoon_shade_program_cache.cpp','native_mtoon_raster_program.cpp','native_mtoon_frame.cpp','native_mtoon_composite_program_cache.cpp','native_mtoon_probe_frame.cpp','native_mtoon_probe_program.cpp','native_mtoon_probe_program_cache.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/' + $_) }
    $taskSources += @('native_skinning.cpp','native_skinning_gpu.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/'+$_) }
    $taskSources += Join-Path $taskProjectRoot 'src/mouth_deformation.cpp'
    $taskSources += @('local_voice_observation.cpp','local_voice_copy.cpp','local_voice_observer.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/'+$_) }
    $taskSources += Join-Path $taskProjectRoot 'src/native_preview_handoff.cpp'
    $taskSources += Join-Path $taskProjectRoot 'src/native_relative_call.cpp'
    $taskSources += @('voice_clock.cpp','voice_stream.cpp','voice_features.cpp','voice_profiles.cpp','voice_analyser.cpp','voice_automatic.cpp','voice_worker.cpp','voice_profile_io.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/'+$_) }
    $taskSources += @('voice_setup.cpp','mod_settings.cpp','mod_settings_service.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/'+$_) }
    $taskSources += @('microphone_backend.cpp','microphone_source.cpp','voice_inputs.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/'+$_) }
    $taskSources += @('avatar_settings.cpp','avatar_model.cpp','avatar_service.cpp','avatar_runtime.cpp','native_avatar_assets.cpp','player_assignments.cpp','native_player_commit.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/'+$_) }
    $taskSources += @('steam_game_build.cpp','standalone_startup.cpp','standalone_startup_runtime.cpp','standalone_startup_environment.cpp','standalone_files.cpp','standalone_settings.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/'+$_) }
    $taskSources += @('mod_input_state.cpp','mod_input_win32.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/'+$_) }
    $taskSources += @('mod_menu.cpp','mod_menu_gpu.cpp','mod_overlay.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/'+$_) }
    $taskSources += @('debug_report_policy.cpp','debug_report_archive.cpp','debug_report_sources.cpp','debug_report_service.cpp','debug_report_gpu.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/'+$_) }
    $taskFlags+=('-I'+(Join-Path $taskProjectRoot 'vendor/imgui'))
    foreach($taskGuiSource in @('imgui.cpp','imgui_draw.cpp','imgui_tables.cpp','imgui_widgets.cpp','backends/imgui_impl_vulkan.cpp')){
        $taskGuiObject=Join-Path $OutputDirectory ('gui-'+[IO.Path]::GetFileNameWithoutExtension($taskGuiSource)+'.o')
        $taskGuiFlags=@('-std=c++17','-O2','-fstack-protector-strong','-DIMGUI_IMPL_VULKAN_NO_PROTOTYPES',('-I'+$VulkanIncludeDirectory),('-I'+(Join-Path $taskProjectRoot 'vendor/imgui')))
        if($PortableRelease){$taskGuiFlags+=@('/Z7','/Brepro',$taskTrimSourceRoot)}
        Invoke-TaskCompiler @taskGuiFlags '-c' (Join-Path $taskProjectRoot ('vendor/imgui/'+$taskGuiSource)) '-o' $taskGuiObject
        if($LASTEXITCODE -ne 0){throw 'Pinned UI dependency compilation failed'}
        $taskSources+=$taskGuiObject
    }
    # Keep pinned upstream C in its own warning policy; project C++ remains Werror.
    foreach($taskHookSource in @('buffer','hook','trampoline','hde/hde64')){
        $taskHookObject=Join-Path $OutputDirectory ('minhook-'+[IO.Path]::GetFileName($taskHookSource)+'.o')
        $taskHookFlags=@('-std=c11','-O2','-fstack-protector-strong')
        if($PortableRelease){$taskHookFlags+=@('/Z7','/Brepro',$taskTrimSourceRoot)}
        Invoke-TaskCompiler @taskHookFlags '-c' (Join-Path $taskProjectRoot ('vendor/minhook/src/'+$taskHookSource+'.c')) '-o' $taskHookObject
        if($LASTEXITCODE -ne 0){throw 'MinHook source compilation failed'}
        $taskSources+=$taskHookObject
    }
    if($PortableRelease){
        $taskResourceObject=Join-Path $OutputDirectory 'runtime-resource.res'
        $taskResourceCompiler='C:/Program Files (x86)/Windows Kits/10/bin/10.0.28000.0/x64/rc.exe'
        & $taskResourceCompiler '/nologo' ('/fo'+$taskResourceObject) 'portable/runtime.rc'
        if($LASTEXITCODE -ne 0){throw 'Runtime release metadata compilation failed'}
        $taskSources+=$taskResourceObject
    }
    $taskLinkSymbols = @()
    if ($PortableRelease) {
        $taskSymbolDirectory = Join-Path $OutputDirectory 'symbols'
        New-Item -ItemType Directory -Path $taskSymbolDirectory -Force | Out-Null
        $taskLinkSymbols = @('/link','/DEBUG:FULL',('/PDB:'+(Join-Path $taskSymbolDirectory 'enshrouded_vrm_runtime.pdb')),'/PDBALTPATH:%_PDB%',('/MAP:'+(Join-Path $taskSymbolDirectory 'enshrouded_vrm_runtime.map')),'/OPT:REF','/OPT:ICF')
    }
    Invoke-TaskCompiler @taskFlags '-shared' @taskSources '-Wl,--image-base,0x190000000' '-lkernel32' '-lonecore' '-luser32' '-lole32' '-luuid' '-lshell32' '-ladvapi32' '-lbcrypt' '-o' (Join-Path $OutputDirectory 'enshrouded_vrm_runtime.dll') @taskLinkSymbols
    if ($LASTEXITCODE -ne 0) { throw "Bridge compilation failed: $LASTEXITCODE" }
    $taskVoiceSdk = Join-Path $taskInputs 'OVRLipSync.dll'
    if ((Get-FileHash -LiteralPath $taskVoiceSdk -Algorithm SHA256).Hash.ToLowerInvariant() -ne 'f20e7bcb50f1591e1c4fdf5fb16ceda876ec599c1731fd857779b977b4e585b7') { throw 'Official voice detector pin mismatch' }
    Copy-Item -LiteralPath $taskVoiceSdk -Destination (Join-Path $OutputDirectory 'OVRLipSync.dll') -Force
    Copy-Item -LiteralPath (Join-Path $taskProjectRoot 'enshrouded_vrm_layer.json') -Destination $OutputDirectory -Force
    & $taskPython 'portable/bootstrap/build_input_bootstrap.py' $taskProjectRoot (Join-Path $OutputDirectory 'bootstrap') $CompilerDirectory
    if ($LASTEXITCODE -ne 0) { throw 'Input loader compilation failed.' }
} finally {
    $env:PATH = $taskSavedPath
    $env:INCLUDE = $taskSavedInclude
    Pop-Location
}
