param([Parameter(Mandatory)][string]$CompilerDirectory,[Parameter(Mandatory)][string]$OutputDirectory,[switch]$Test)
$ErrorActionPreference='Stop'
$taskPortableRoot=$PSScriptRoot
$taskProject=Split-Path -Parent $taskPortableRoot
$taskCompiler=Join-Path $CompilerDirectory 'g++.exe'
$taskResources=Join-Path $CompilerDirectory 'windres.exe'
$taskOutput=[IO.Path]::GetFullPath($OutputDirectory)
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskFlags=@('-std=c++17','-O2','-Wall','-Wextra','-Werror','-static','-fstack-protector-strong',('-ffile-prefix-map='+[IO.Path]::GetFullPath($taskProject)+'=.'),'-Wl,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp')
if($Test){
    & $taskCompiler @taskFlags (Join-Path $taskProject 'tests/portable_launcher.cpp') (Join-Path $taskPortableRoot 'launcher_core.cpp') '-lbcrypt' '-o' (Join-Path $taskOutput 'portable_launcher_tests.exe')
    if($LASTEXITCODE -ne 0){throw 'Portable launcher tests did not compile'}
    & (Join-Path $taskOutput 'portable_launcher_tests.exe') (Join-Path $taskOutput 'synthetic-fixture')
    if($LASTEXITCODE -ne 0){throw 'Portable launcher tests failed'}
}
Push-Location -LiteralPath $taskPortableRoot
try{
    & $taskResources 'launcher.rc' '-O' 'coff' '-o' (Join-Path $taskOutput 'launcher-resource.o')
    if($LASTEXITCODE -ne 0){throw 'Launcher version/manifest compilation failed'}
    & $taskCompiler @taskFlags '-municode' '-mwindows' 'launcher_core.cpp' 'launcher_win.cpp' (Join-Path $taskOutput 'launcher-resource.o') '-lbcrypt' '-lcomdlg32' '-lshell32' '-ladvapi32' '-lgdi32' '-luser32' '-o' (Join-Path $taskOutput 'EnshroudedVRM.exe')
    if($LASTEXITCODE -ne 0){throw 'Portable launcher compilation failed'}
}finally{Pop-Location}
