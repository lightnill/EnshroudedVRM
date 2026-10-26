param(
    [ValidateSet('All', 'Bridge', 'Test', 'Importer', 'Retarget', 'Springs', 'Pose', 'Graphics', 'Readback', 'Features', 'Selection', 'Trace', 'TraceRuntime', 'Log', 'Deferred', 'Forwarding', 'NativePass', 'DeferredRuntime', 'DeferredIntegration', 'InstanceSuppression', 'NativePose', 'NativeSkinning', 'NativeState', 'NativeOit', 'NativePrepare', 'NativeBackend', 'NativeReplacementCPU', 'ToolGrip')][string]$Target = 'All',
    [string]$CompilerDirectory,
    [string]$VulkanIncludeDirectory,
    [string]$ShaderCompiler,
    [string]$OutputDirectory,
    [switch]$PortableRelease
)
$ErrorActionPreference = 'Stop'
$taskProjectRoot = $PSScriptRoot
$taskWorkspaceRoot = [IO.Path]::GetFullPath((Join-Path $taskProjectRoot '../..'))
if ($Target -eq 'Selection') {
    & (Join-Path $taskProjectRoot 'tests/avatar_selection.ps1')
    if ($LASTEXITCODE -ne 0) { throw "Avatar-selection tests failed: $LASTEXITCODE" }
    return
}
if (-not $CompilerDirectory) { $CompilerDirectory = Join-Path $taskWorkspaceRoot 'work/toolchain/w64devkit/bin' }
if (-not $VulkanIncludeDirectory) { $VulkanIncludeDirectory = Join-Path $taskWorkspaceRoot 'work/research/Vulkan-Headers/include' }
if (-not $OutputDirectory) { $OutputDirectory = Join-Path $taskWorkspaceRoot 'work/runtime/bin' }
if (-not $ShaderCompiler) { $ShaderCompiler = Join-Path $taskWorkspaceRoot 'work/toolchain/dxc/bin/x64/dxc.exe' }
$taskCompiler = Join-Path $CompilerDirectory 'g++.exe'
if (-not (Test-Path -LiteralPath $taskCompiler)) { throw "Portable compiler not found: $taskCompiler" }
if ($Target -notin @('Importer','Retarget') -and -not (Test-Path -LiteralPath (Join-Path $VulkanIncludeDirectory 'vulkan/vulkan.h'))) { throw 'Vulkan headers not found.' }
New-Item -ItemType Directory -Path $OutputDirectory -Force | Out-Null
$taskSavedPath = $env:PATH
try {
    $env:PATH = $CompilerDirectory + ';' + $env:PATH
    $taskFlags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', ('-I' + $VulkanIncludeDirectory))
    if($PortableRelease){$taskFlags+=@('-fstack-protector-strong',('-ffile-prefix-map='+$taskWorkspaceRoot+'=.'),'-Wl,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp')}
    if ($Target -eq 'All' -or $Target -eq 'Bridge') {
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
        foreach($taskShadowStage in @(@('vs_shadow','vs_6_0','vert'),@('ps_shadow','ps_6_0','frag'))){
            & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-T' $taskShadowStage[1] '-E' $taskShadowStage[0] (Join-Path $taskProjectRoot 'shaders/avatar_native_shadow.hlsl') '-Fo' (Join-Path $taskShaderOutput ('avatar_native.shadow.'+$taskShadowStage[2]+'.spv'))
            if($LASTEXITCODE -ne 0){throw 'Native shadow shader compilation failed.'}
        }
        & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-T' 'cs_6_0' '-E' 'cs_snapshot' (Join-Path $taskProjectRoot 'shaders/native_volume_snapshot.hlsl') '-Fo' (Join-Path $taskShaderOutput 'native_volume_snapshot.comp.spv')
        if ($LASTEXITCODE -ne 0) { throw 'Native volume snapshot shader compilation failed.' }
        $taskSources = @('runtime_bridge.cpp','device_features.cpp','command_trace.cpp','command_trace_runtime.cpp','deferred_rendering.cpp','deferred_runtime.cpp','gpu_probe.cpp','graphics_state.cpp','avatar_renderer.cpp','enshrouded_bridge.cpp','vrm_asset.cpp','retarget.cpp','spring_bones.cpp','tool_grip.cpp','secondary_hand_contact.cpp','native_tool_slots.cpp','native_tool_slot_target.cpp','native_tool_slots_runtime.cpp','native_visual_slot_write.cpp','native_tool_call_patch.cpp','native_avatar_pose.cpp','native_state.cpp','native_draw.cpp','native_backend.cpp','native_owner.cpp','native_owner_process.cpp','native_clip_shader.cpp','native_clip_pipelines.cpp','native_buffer_memory.cpp','native_body_coverage.cpp','native_body_selection.cpp','native_instance_qualifiers.cpp','native_oit_window.cpp','native_transparency_trace.cpp','native_image_memory.cpp','native_material_plan.cpp','native_volume_snapshot.cpp','native_oit_draw.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/' + $_) }
        $taskSources += @('native_skinning.cpp','native_skinning_gpu.cpp') | ForEach-Object { Join-Path $taskProjectRoot ('src/'+$_) }
        if($PortableRelease){
            $taskResourceObject=Join-Path $OutputDirectory 'runtime-resource.o'
            & (Join-Path $CompilerDirectory 'windres.exe') (Join-Path $taskProjectRoot 'portable/runtime.rc') '-O' 'coff' '-o' $taskResourceObject
            if($LASTEXITCODE -ne 0){throw 'Runtime release metadata compilation failed'}
            $taskSources+=$taskResourceObject
        }
        & $taskCompiler @taskFlags '-shared' @taskSources '-lkernel32' '-lonecore' '-o' (Join-Path $OutputDirectory 'enshrouded_vrm_runtime.dll')
        if ($LASTEXITCODE -ne 0) { throw "Bridge compilation failed: $LASTEXITCODE" }
        Copy-Item -LiteralPath (Join-Path $taskProjectRoot 'enshrouded_vrm_layer.json') -Destination $OutputDirectory -Force
    }
    if ($Target -eq 'All' -or $Target -eq 'Test') {
        if (-not (Test-Path -LiteralPath $ShaderCompiler)) { throw 'Shader compiler not found.' }
        $taskProbeShaderOutput = Join-Path $OutputDirectory 'shaders'
        New-Item -ItemType Directory -Path $taskProbeShaderOutput -Force | Out-Null
        & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-T' 'cs_6_0' '-E' 'cs_main' (Join-Path $taskProjectRoot 'tests/probe_compute.hlsl') '-Fo' (Join-Path $taskProbeShaderOutput 'probe.comp.spv')
        if ($LASTEXITCODE -ne 0) { throw 'Compute forwarding fixture compilation failed.' }
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/runtime_smoke.cpp') '-o' (Join-Path $OutputDirectory 'runtime_smoke.exe')
        if ($LASTEXITCODE -ne 0) { throw "Smoke-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'runtime_smoke.exe') $OutputDirectory (Join-Path $OutputDirectory '../smoke.log')
        if ($LASTEXITCODE -ne 0) { throw "Runtime smoke test failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'ToolGrip') {
        $taskToolCases = @(
            @{Name='native_tool_pose_window';Sources=@();Flags=@()},
            @{Name='tool_grip';Sources=@('tool_grip.cpp');Flags=@()},
            @{Name='secondary_hand_contact';Sources=@('secondary_hand_contact.cpp','tool_grip.cpp');Flags=@()},
            @{Name='native_contact_hold';Sources=@('native_skinning.cpp','native_avatar_pose.cpp','native_material_plan.cpp');Flags=@()},
            @{Name='native_tool_slots';Sources=@('native_tool_slots.cpp');Flags=@()},
            @{Name='native_tool_slot_target';Sources=@('native_tool_slot_target.cpp');Flags=@()},
            @{Name='native_tool_call_patch';Sources=@('native_tool_call_patch.cpp');Flags=@()},
            @{Name='native_tool_slots_runtime';Sources=@('native_tool_slots_runtime.cpp','native_tool_slots.cpp','native_tool_slot_target.cpp','native_tool_call_patch.cpp','native_visual_slot_write.cpp');Flags=@('-DEVRM_NATIVE_TOOL_SLOTS_TESTING=1','-Wl,--wrap=__imp_WriteProcessMemory')},
            @{Name='native_visual_slot_write';Sources=@('native_visual_slot_write.cpp');Flags=@('-Wl,--wrap=__imp_VirtualQuery','-Wl,--wrap=K32QueryWorkingSetEx')}
        )
        foreach ($taskToolCase in $taskToolCases) {
            $taskToolSources = @((Join-Path $taskProjectRoot ('tests/'+$taskToolCase.Name+'.cpp')))
            $taskToolSources += @($taskToolCase.Sources | ForEach-Object { Join-Path $taskProjectRoot ('src/'+$_) })
            $taskToolExe = Join-Path $OutputDirectory ($taskToolCase.Name+'_tests.exe')
            & $taskCompiler @taskFlags @($taskToolCase.Flags) @taskToolSources '-lkernel32' '-lonecore' '-o' $taskToolExe
            if ($LASTEXITCODE -ne 0) { throw ($taskToolCase.Name+' compilation failed') }
            & $taskToolExe
            if ($LASTEXITCODE -ne 0) { throw ($taskToolCase.Name+' tests failed') }
        }
    }
    if ($Target -eq 'All' -or $Target -eq 'Importer') {
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/vrm_import.cpp') (Join-Path $taskProjectRoot 'src/vrm_asset.cpp') '-o' (Join-Path $OutputDirectory 'vrm_import.exe')
        if ($LASTEXITCODE -ne 0) { throw "Importer-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'vrm_import.exe') (Join-Path $taskWorkspaceRoot 'work/models/VRM1_Constraint_Twist_Sample.vrm')
        if ($LASTEXITCODE -ne 0) { throw "Importer tests failed: $LASTEXITCODE" }
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/import_compatibility.cpp') (Join-Path $taskProjectRoot 'src/vrm_asset.cpp') '-o' (Join-Path $OutputDirectory 'import_compatibility.exe')
        if ($LASTEXITCODE -ne 0) { throw "Compatibility-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'import_compatibility.exe')
        if ($LASTEXITCODE -ne 0) { throw "Compatibility tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'Retarget') {
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/retarget.cpp') (Join-Path $taskProjectRoot 'src/retarget.cpp') (Join-Path $taskProjectRoot 'src/vrm_asset.cpp') '-o' (Join-Path $OutputDirectory 'retarget.exe')
        if ($LASTEXITCODE -ne 0) { throw "Retarget-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'retarget.exe') (Join-Path $taskWorkspaceRoot 'work/models/VRM1_Constraint_Twist_Sample.vrm')
        if ($LASTEXITCODE -ne 0) { throw "Retarget tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'Springs') {
        foreach ($taskSpringGuard in @('spring_optional','spring_parent','spring_relaxed')) {
            $taskSpringGuardExe = Join-Path $OutputDirectory ($taskSpringGuard+'_tests.exe')
            & $taskCompiler @taskFlags (Join-Path $taskProjectRoot ('tests/'+$taskSpringGuard+'.cpp')) (Join-Path $taskProjectRoot 'src/spring_bones.cpp') '-o' $taskSpringGuardExe
            if ($LASTEXITCODE -ne 0) { throw 'Spring optional guard compilation failed.' }
            & $taskSpringGuardExe
            if ($LASTEXITCODE -ne 0) { throw 'Spring optional guard tests failed.' }
        }
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/spring_bones.cpp') (Join-Path $taskProjectRoot 'src/spring_bones.cpp') (Join-Path $taskProjectRoot 'src/retarget.cpp') (Join-Path $taskProjectRoot 'src/vrm_asset.cpp') '-o' (Join-Path $OutputDirectory 'spring_bones_tests.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Spring-test compilation failed.' }
        $taskSpringModels = @('VRM1_Constraint_Twist_Sample.vrm','Seed-san.vrm','AliciaSolid_vrm-0.51.vrm','three-vrm-girl-v0.6.11.vrm') | ForEach-Object { Join-Path $taskWorkspaceRoot ('work/models/'+$_) }
        & (Join-Path $OutputDirectory 'spring_bones_tests.exe') @taskSpringModels
        if ($LASTEXITCODE -ne 0) { throw 'Spring tests failed.' }
    }
    if ($Target -eq 'All' -or $Target -eq 'Pose') {
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/pose_layout.cpp') (Join-Path $taskProjectRoot 'src/enshrouded_bridge.cpp') '-o' (Join-Path $OutputDirectory 'pose_layout.exe')
        if ($LASTEXITCODE -ne 0) { throw "Pose-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'pose_layout.exe') (Join-Path $taskWorkspaceRoot 'work/rig/menu-profile.json') (Join-Path $taskWorkspaceRoot 'work/rig/player-catalog.json') (Join-Path $taskWorkspaceRoot 'work/runtime/capture-20261022-120521')
        if ($LASTEXITCODE -ne 0) { throw "Pose tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'Graphics') {
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/graphics_state.cpp') (Join-Path $taskProjectRoot 'src/graphics_state.cpp') '-o' (Join-Path $OutputDirectory 'graphics_state.exe')
        if ($LASTEXITCODE -ne 0) { throw "Graphics-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'graphics_state.exe')
        if ($LASTEXITCODE -ne 0) { throw "Graphics tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'Readback') {
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/gpu_readback.cpp') (Join-Path $taskProjectRoot 'src/gpu_probe.cpp') '-o' (Join-Path $OutputDirectory 'gpu_readback.exe')
        if ($LASTEXITCODE -ne 0) { throw "Readback-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'gpu_readback.exe')
        if ($LASTEXITCODE -ne 0) { throw "Readback tests failed: $LASTEXITCODE" }
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/gpu_async_readback.cpp') (Join-Path $taskProjectRoot 'src/gpu_probe.cpp') '-o' (Join-Path $OutputDirectory 'gpu_async_readback.exe')
        if ($LASTEXITCODE -ne 0) { throw "Async-readback compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'gpu_async_readback.exe')
        if ($LASTEXITCODE -ne 0) { throw "Async-readback tests failed: $LASTEXITCODE" }
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/native_pose_freshness.cpp') '-o' (Join-Path $OutputDirectory 'native_pose_freshness.exe')
        if ($LASTEXITCODE -ne 0) { throw "Pose-freshness compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'native_pose_freshness.exe')
        if ($LASTEXITCODE -ne 0) { throw "Pose-freshness tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'Features') {
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/device_features.cpp') (Join-Path $taskProjectRoot 'src/device_features.cpp') '-o' (Join-Path $OutputDirectory 'device_features.exe')
        if ($LASTEXITCODE -ne 0) { throw "Device-feature-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'device_features.exe')
        if ($LASTEXITCODE -ne 0) { throw "Device-feature tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'Selection') {
        & (Join-Path $taskProjectRoot 'tests/avatar_selection.ps1')
        if ($LASTEXITCODE -ne 0) { throw "Avatar-selection tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'Trace') {
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/command_trace.cpp') (Join-Path $taskProjectRoot 'src/command_trace.cpp') '-o' (Join-Path $OutputDirectory 'command_trace.exe')
        if ($LASTEXITCODE -ne 0) { throw "Command-trace-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'command_trace.exe')
        if ($LASTEXITCODE -ne 0) { throw "Command-trace tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'TraceRuntime') {
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/command_trace_runtime.cpp') (Join-Path $taskProjectRoot 'src/command_trace_runtime.cpp') (Join-Path $taskProjectRoot 'src/command_trace.cpp') '-o' (Join-Path $OutputDirectory 'command_trace_runtime.exe')
        if ($LASTEXITCODE -ne 0) { throw "Command-trace-runtime-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'command_trace_runtime.exe')
        if ($LASTEXITCODE -ne 0) { throw "Command-trace-runtime tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'Log') {
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/runtime_log.cpp') '-o' (Join-Path $OutputDirectory 'runtime_log.exe')
        if ($LASTEXITCODE -ne 0) { throw "Runtime-log-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'runtime_log.exe')
        if ($LASTEXITCODE -ne 0) { throw "Runtime-log tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'Deferred') {
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/deferred_rendering.cpp') (Join-Path $taskProjectRoot 'src/deferred_rendering.cpp') '-o' (Join-Path $OutputDirectory 'deferred_rendering.exe')
        if ($LASTEXITCODE -ne 0) { throw "Deferred-rendering-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'deferred_rendering.exe')
        if ($LASTEXITCODE -ne 0) { throw "Deferred-rendering tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'NativePass') {
        & (Join-Path $taskProjectRoot 'tests/native_pass_fixture.ps1') -OutputDirectory (Join-Path $OutputDirectory 'native-pass-fixture') -CompilerDirectory $CompilerDirectory -VulkanIncludeDirectory $VulkanIncludeDirectory -ShaderCompiler $ShaderCompiler
        if ($LASTEXITCODE -ne 0) { throw "Native-pass output fixture failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'Forwarding') {
        & $taskCompiler @taskFlags ('-I' + (Join-Path $taskProjectRoot 'src')) (Join-Path $taskProjectRoot 'tests/command_forwarding.cpp') (Join-Path $taskProjectRoot 'src/deferred_rendering.cpp') '-o' (Join-Path $OutputDirectory 'command_forwarding.exe')
        if ($LASTEXITCODE -ne 0) { throw "Command-forwarding-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'command_forwarding.exe')
        if ($LASTEXITCODE -ne 0) { throw "Command-forwarding tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'DeferredRuntime') {
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/deferred_runtime.cpp') (Join-Path $taskProjectRoot 'src/deferred_runtime.cpp') (Join-Path $taskProjectRoot 'src/deferred_rendering.cpp') '-o' (Join-Path $OutputDirectory 'deferred_runtime.exe')
        if ($LASTEXITCODE -ne 0) { throw "Deferred-runtime-test compilation failed: $LASTEXITCODE" }
        & (Join-Path $OutputDirectory 'deferred_runtime.exe')
        if ($LASTEXITCODE -ne 0) { throw "Deferred-runtime tests failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'DeferredIntegration') {
        & (Join-Path $taskProjectRoot 'tests/deferred_integration.ps1') -RuntimeDirectory $OutputDirectory -NativeFixtureDirectory (Join-Path $OutputDirectory 'native-pass-fixture') -OutputDirectory (Join-Path $OutputDirectory 'deferred-integration')
    }
    if ($Target -eq 'All' -or $Target -eq 'InstanceSuppression') {
        & (Join-Path $taskProjectRoot 'tests/instance_suppression_fixture.ps1') -OutputDirectory (Join-Path $OutputDirectory 'instance-suppression-fixture') -CompilerDirectory $CompilerDirectory -VulkanIncludeDirectory $VulkanIncludeDirectory -ShaderCompiler $ShaderCompiler
        if ($LASTEXITCODE -ne 0) { throw "Instance-suppression fixture failed: $LASTEXITCODE" }
    }
    if ($Target -eq 'All' -or $Target -eq 'NativePose') {
        & $taskCompiler @taskFlags ('-I' + (Join-Path $taskProjectRoot 'src')) (Join-Path $taskProjectRoot 'tests/native_avatar_pose.cpp') (Join-Path $taskProjectRoot 'src/native_avatar_pose.cpp') '-o' (Join-Path $OutputDirectory 'native_avatar_pose.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Native model-space geometry compilation failed.' }
        & (Join-Path $OutputDirectory 'native_avatar_pose.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Native model-space geometry tests failed.' }
    }
    if ($Target -eq 'All' -or $Target -eq 'NativeSkinning') {
        $taskViewExe=Join-Path $OutputDirectory 'native_view_publication_tests.exe'
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/native_view_publication.cpp') '-o' $taskViewExe
        if($LASTEXITCODE -ne 0){throw 'Native view publication test compilation failed.'}
        & $taskViewExe
        if($LASTEXITCODE -ne 0){throw 'Native view publication tests failed.'}
        $taskSkinSources=@('native_skinning.cpp','native_avatar_pose.cpp','native_material_plan.cpp')|ForEach-Object{Join-Path $taskProjectRoot ('src/'+$_)}
        $taskSkinExe=Join-Path $OutputDirectory 'native_skinning_tests.exe'
        & $taskCompiler @taskFlags (Join-Path $taskProjectRoot 'tests/native_skinning.cpp') @taskSkinSources '-o' $taskSkinExe
        if($LASTEXITCODE -ne 0){throw 'Native skin packing compilation failed.'}
        & $taskSkinExe
        if($LASTEXITCODE -ne 0){throw 'Native skin packing tests failed.'}
        & (Join-Path $taskProjectRoot 'tests/native_skinning_gpu.ps1') -OutputDirectory (Join-Path $OutputDirectory 'native-skinning-gpu') -CompilerDirectory $CompilerDirectory -VulkanIncludeDirectory $VulkanIncludeDirectory -ShaderCompiler $ShaderCompiler
    }
    if ($Target -eq 'All' -or $Target -eq 'NativeState') {
        & $taskCompiler @taskFlags ('-I' + (Join-Path $taskProjectRoot 'src')) (Join-Path $taskProjectRoot 'tests/native_state.cpp') (Join-Path $taskProjectRoot 'src/native_state.cpp') (Join-Path $taskProjectRoot 'src/native_oit_window.cpp') '-o' (Join-Path $OutputDirectory 'native_state.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Native metadata compilation failed.' }
        & (Join-Path $OutputDirectory 'native_state.exe')
        if ($LASTEXITCODE -ne 0) { throw 'Native metadata tests failed.' }
    }
    if ($Target -eq 'All' -or $Target -eq 'NativePrepare') {
        & (Join-Path $taskProjectRoot 'tests/native_prepare_fixture.ps1') -OutputDirectory (Join-Path $OutputDirectory 'native-prepare-fixture') -CompilerDirectory $CompilerDirectory -VulkanIncludeDirectory $VulkanIncludeDirectory -ShaderCompiler $ShaderCompiler
    }
    if ($Target -eq 'All' -or $Target -eq 'NativeBackend') {
        & (Join-Path $taskProjectRoot 'tests/native_backend_fixture.ps1') -OutputDirectory (Join-Path $OutputDirectory 'native-backend-fixture') -CompilerDirectory $CompilerDirectory -VulkanIncludeDirectory $VulkanIncludeDirectory -ShaderCompiler $ShaderCompiler
    }
    if ($Target -eq 'All' -or $Target -eq 'NativeReplacementCPU') {
        & (Join-Path $taskProjectRoot 'tests/native_replacement_cpu.ps1') -OutputDirectory (Join-Path $OutputDirectory 'native-replacement-cpu') -CompilerDirectory $CompilerDirectory -VulkanIncludeDirectory $VulkanIncludeDirectory
        & (Join-Path $taskProjectRoot 'tests/native_replacement_backend.ps1') -OutputDirectory (Join-Path $OutputDirectory 'native-replacement-backend-cpu') -CompilerDirectory $CompilerDirectory -VulkanIncludeDirectory $VulkanIncludeDirectory
        & (Join-Path $taskProjectRoot 'tests/native_launch.ps1')
        & (Join-Path $taskProjectRoot 'tests/native_oit_window.ps1') -OutputDirectory (Join-Path $OutputDirectory 'native-oit-cpu') -CompilerDirectory $CompilerDirectory -VulkanIncludeDirectory $VulkanIncludeDirectory
        & (Join-Path $taskProjectRoot 'tests/native_transparency_trace.ps1') -OutputDirectory (Join-Path $OutputDirectory 'native-transparency-trace-cpu') -CompilerDirectory $CompilerDirectory -VulkanIncludeDirectory $VulkanIncludeDirectory
        & (Join-Path $taskProjectRoot 'tests/native_image_memory.ps1') -OutputDirectory (Join-Path $OutputDirectory 'native-image-memory-cpu') -CompilerDirectory $CompilerDirectory -VulkanIncludeDirectory $VulkanIncludeDirectory
        & (Join-Path $taskProjectRoot 'tests/native_image_hooks.ps1') -OutputDirectory (Join-Path $OutputDirectory 'native-image-hooks-cpu') -CompilerDirectory $CompilerDirectory -VulkanIncludeDirectory $VulkanIncludeDirectory
    }
    if ($Target -eq 'NativeOit') {
        & (Join-Path $taskProjectRoot 'tests/native_oit_window.ps1') -OutputDirectory (Join-Path $OutputDirectory 'native-oit-cpu') -CompilerDirectory $CompilerDirectory -VulkanIncludeDirectory $VulkanIncludeDirectory
    }
} finally {
    $env:PATH = $taskSavedPath
}
