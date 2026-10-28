param(
    [string]$OutputDirectory,
    [string]$CompilerDirectory=$env:EVRM_COMPILER_DIRECTORY,
    [string]$VulkanIncludeDirectory=$env:EVRM_VULKAN_INCLUDE_DIRECTORY,
    [string]$ShaderCompiler=$env:EVRM_SHADER_COMPILER,
    [ValidateSet('All','GPU','Reuse')][string]$Fixture='All'
)
$ErrorActionPreference='Stop'
$taskProject=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
if(-not $OutputDirectory){$OutputDirectory=Join-Path ([IO.Path]::GetTempPath()) 'evrm-native-skinning-gpu'}
if($CompilerDirectory){$taskCompiler=Join-Path $CompilerDirectory 'g++.exe'}else{
    $taskCompilerCommand=Get-Command 'g++' -ErrorAction SilentlyContinue
    if($taskCompilerCommand){$taskCompiler=$taskCompilerCommand.Source}
}
if(-not $VulkanIncludeDirectory -and $env:VULKAN_SDK){$VulkanIncludeDirectory=Join-Path $env:VULKAN_SDK 'Include'}
if(-not $ShaderCompiler){$taskDxc=Get-Command 'dxc' -ErrorAction SilentlyContinue;if($taskDxc){$ShaderCompiler=$taskDxc.Source}}
if(-not $taskCompiler -or -not (Test-Path -LiteralPath $taskCompiler)){throw 'Set CompilerDirectory or EVRM_COMPILER_DIRECTORY, or put g++ on PATH.'}
if(-not $VulkanIncludeDirectory -or -not (Test-Path -LiteralPath (Join-Path $VulkanIncludeDirectory 'vulkan/vulkan.h'))){throw 'Set VulkanIncludeDirectory, EVRM_VULKAN_INCLUDE_DIRECTORY, or VULKAN_SDK.'}
if(-not $ShaderCompiler -or -not (Test-Path -LiteralPath $ShaderCompiler)){throw 'Set ShaderCompiler or EVRM_SHADER_COMPILER, or put dxc on PATH.'}
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
[void][IO.Directory]::CreateDirectory($OutputDirectory)
foreach($taskStage in @(@('cs_skin','native_skinning.comp.spv'),@('cs_finalize','native_skinning.finalize.spv'))){
    & $ShaderCompiler '-spirv' '-fspv-target-env=vulkan1.1' '-WX' '-T' 'cs_6_0' '-E' $taskStage[0] (Join-Path $taskProject 'shaders/native_skinning.hlsl') '-Fo' (Join-Path $OutputDirectory $taskStage[1])
    if($LASTEXITCODE -ne 0){throw 'Skin shader compilation failed'}
}
$taskSources=@('native_skinning_gpu.cpp','native_skinning.cpp','native_avatar_pose.cpp','native_material_plan.cpp')|ForEach-Object{Join-Path $taskProject ('src/'+$_)}
$taskFixtures=if($Fixture -eq 'GPU'){@('native_skinning_gpu')}elseif($Fixture -eq 'Reuse'){@('native_skinning_gpu_reuse')}else{@('native_skinning_gpu','native_skinning_gpu_reuse')}
foreach($taskName in $taskFixtures){
    $taskExe=Join-Path $OutputDirectory ($taskName+'.exe')
    & $taskCompiler '-std=c++17' '-O2' '-Wall' '-Wextra' '-Werror' '-static' ('-I'+$VulkanIncludeDirectory) (Join-Path $PSScriptRoot ($taskName+'.cpp')) @taskSources '-o' $taskExe
    if($LASTEXITCODE -ne 0){throw ($taskName+' compilation failed')}
    $taskSaved=@{}
    foreach($taskKey in @('VK_INSTANCE_LAYERS','VK_LAYER_PATH','VK_ADD_LAYER_PATH','VK_LOADER_LAYERS_DISABLE')){$taskSaved[$taskKey]=[Environment]::GetEnvironmentVariable($taskKey,'Process')}
    try{
        foreach($taskKey in @('VK_INSTANCE_LAYERS','VK_LAYER_PATH','VK_ADD_LAYER_PATH')){[Environment]::SetEnvironmentVariable($taskKey,$null,'Process')}
        $env:VK_LOADER_LAYERS_DISABLE='*'
        & $taskExe $OutputDirectory
        if($LASTEXITCODE -ne 0){throw ($taskName+' failed')}
    }finally{foreach($taskKey in $taskSaved.Keys){[Environment]::SetEnvironmentVariable($taskKey,$taskSaved[$taskKey],'Process')}}
}
