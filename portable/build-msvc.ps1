param(
    [Parameter(Mandatory)][string]$VcToolsDirectory,
    [Parameter(Mandatory)][string]$WindowsSdkIncludeDirectory,
    [Parameter(Mandatory)][string]$WindowsSdkLibraryDirectory,
    [Parameter(Mandatory)][string]$ResourceCompiler,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Python='python',
    [switch]$Test
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$taskProject=Split-Path -Parent $PSScriptRoot
# Resolve against the caller's PowerShell location, which can differ from the
# process current directory after Set-Location in an interactive shell.
$taskVc=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($VcToolsDirectory)
$taskSdkInclude=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($WindowsSdkIncludeDirectory)
$taskSdkLib=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($WindowsSdkLibraryDirectory)
$taskRc=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($ResourceCompiler)
$taskOutput=$ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
$taskCompiler=Join-Path $taskVc 'bin/Hostx64/x64/cl.exe'
foreach($taskRequired in @($taskCompiler,$taskRc,(Join-Path $taskVc 'include/vector'),(Join-Path $taskVc 'lib/x64/libcmt.lib'),(Join-Path $taskSdkInclude 'um/Windows.h'),(Join-Path $taskSdkInclude 'ucrt/corecrt.h'),(Join-Path $taskSdkLib 'um/x64/kernel32.lib'),(Join-Path $taskSdkLib 'ucrt/x64/libucrt.lib'))){
    if(-not(Test-Path -LiteralPath $taskRequired -PathType Leaf)){throw "Missing Microsoft toolchain component: $taskRequired"}
}
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskOldInclude=$env:INCLUDE;$taskOldLib=$env:LIB;$taskOldPath=$env:PATH
function Invoke-Msvc([string[]]$BuildArguments){
    & $taskCompiler @BuildArguments
    if($LASTEXITCODE -ne 0){throw "MSVC compilation failed (exit $LASTEXITCODE)"}
}
try{
    $env:INCLUDE=@((Join-Path $taskVc 'include'),(Join-Path $taskSdkInclude 'ucrt'),(Join-Path $taskSdkInclude 'shared'),(Join-Path $taskSdkInclude 'um')) -join ';'
    $env:LIB=@((Join-Path $taskVc 'lib/x64'),(Join-Path $taskSdkLib 'ucrt/x64'),(Join-Path $taskSdkLib 'um/x64')) -join ';'
    $env:PATH=@((Split-Path -Parent $taskCompiler),(Split-Path -Parent $taskRc),$taskOldPath) -join ';'
    $taskFlags=@('/nologo','/std:c++17','/O2','/EHsc','/MT','/utf-8','/W4','/WX','/permissive-','/Zc:__cplusplus','/Gy','/Gw','/GS','/guard:cf','/Brepro','/DUNICODE','/D_UNICODE',('/Fo'+$taskOutput+'\'))
    # Release library assertions embed absolute header paths under MSVC.
    # Keep the core test executable's assertions enabled.
    $taskReleaseFlags=$taskFlags+@('/DNDEBUG')
    $taskLink=@('/link','/MACHINE:X64','/INCREMENTAL:NO','/OPT:REF','/OPT:ICF','/DYNAMICBASE','/NXCOMPAT','/HIGHENTROPYVA','/guard:cf','/Brepro')
    $taskLibraries=@('bcrypt.lib','comdlg32.lib','shell32.lib','advapi32.lib','gdi32.lib','user32.lib')
    Push-Location -LiteralPath $taskProject
    try{
        if($Test){
            Invoke-Msvc ($taskFlags+@('tests/portable_launcher.cpp','portable/launcher_core.cpp',('/Fe'+(Join-Path $taskOutput 'portable_launcher_tests.exe')))+$taskLink+@('bcrypt.lib'))
            & (Join-Path $taskOutput 'portable_launcher_tests.exe') (Join-Path $taskOutput 'synthetic-fixture')
            if($LASTEXITCODE -ne 0){throw 'Portable launcher core tests failed'}
        }
        Push-Location -LiteralPath $PSScriptRoot
        try{& $taskRc '/nologo' '/c65001' '/fo' (Join-Path $taskOutput 'launcher-resource.res') 'launcher.rc'
            if($LASTEXITCODE -ne 0){throw 'MSVC launcher resource compilation failed'}
        }finally{Pop-Location}
        Invoke-Msvc ($taskReleaseFlags+@('portable/launcher_core.cpp','portable/launcher_win.cpp',(Join-Path $taskOutput 'launcher-resource.res'),('/Fe'+(Join-Path $taskOutput 'EnshroudedVRM.exe')))+$taskLink+@('/SUBSYSTEM:WINDOWS','/MANIFEST:NO')+$taskLibraries)
        if($Test){
            Invoke-Msvc ($taskFlags+@('tests/portable_child.cpp',('/Fe'+(Join-Path $taskOutput 'portable_child.exe')))+$taskLink+@('/SUBSYSTEM:WINDOWS'))
            Invoke-Msvc ($taskReleaseFlags+@('portable/launcher_core.cpp','portable/launcher_win.cpp','tests/portable_test_wrappers.cpp',(Join-Path $taskOutput 'launcher-resource.res'),('/Fe'+(Join-Path $taskOutput 'portable_launcher_native_tests.exe')))+$taskLink+@('/SUBSYSTEM:WINDOWS','/MANIFEST:NO')+$taskLibraries)
            & $Python 'tests/test_portable_native.py' (Join-Path $taskOutput 'portable_launcher_native_tests.exe') (Join-Path $taskOutput 'portable_child.exe')
            if($LASTEXITCODE -ne 0){throw 'Portable launcher native process tests failed'}
        }
        $taskHash=(Get-FileHash -LiteralPath (Join-Path $taskOutput 'EnshroudedVRM.exe') -Algorithm SHA256).Hash.ToLowerInvariant()
        [ordered]@{compiler=$taskCompiler;compilerVersion=(Get-Item -LiteralPath $taskCompiler).VersionInfo.FileVersion;runtime='Microsoft static CRT (/MT)';architecture='x64';launcherSha256=$taskHash;testsRequested=[bool]$Test}|ConvertTo-Json|Set-Content -LiteralPath (Join-Path $taskOutput 'msvc-build.json') -Encoding utf8
        Write-Output "MSVC launcher built: $taskHash"
    }finally{Pop-Location}
}finally{
    $env:INCLUDE=$taskOldInclude;$env:LIB=$taskOldLib;$env:PATH=$taskOldPath
}
