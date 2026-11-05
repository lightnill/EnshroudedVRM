# Developer-only synthetic child for test_script_launcher.py. This is not a player launcher.
param(
    [Parameter(Mandatory)][string]$Compiler,
    [Parameter(Mandatory)][string]$OutputDirectory,
    [string]$Python=$env:EVRM_PYTHON
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
$taskCompiler = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Compiler)
$taskOutput = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputDirectory)
if ($taskCompiler.StartsWith('\\') -or $taskOutput.StartsWith('\\')) { throw 'Use local compiler and output paths.' }
if (-not (Test-Path -LiteralPath $taskCompiler -PathType Leaf)) { throw "Compiler not found: $taskCompiler" }
if (-not $Python) { $Python=(Get-Command python -ErrorAction Stop).Source }
if (-not (Test-Path -LiteralPath $Python -PathType Leaf)) { throw "Python not found: $Python" }
$taskAdapter=Join-Path $PSScriptRoot '../portable/native_compiler.py'
$taskSource = Join-Path $PSScriptRoot 'portable_child.cpp'
$taskChild = Join-Path $taskOutput 'portable_child.exe'
New-Item -ItemType Directory -Path $taskOutput -Force | Out-Null
$taskSavedPath = $env:PATH
try {
    $env:PATH = (Split-Path -Parent $taskCompiler) + ';' + $env:PATH
    $taskFlags = @('-std=c++17', '-O2', '-Wall', '-Wextra', '-Werror', '-static', '-municode', '-mwindows', '-Wl,--nxcompat,--dynamicbase,--high-entropy-va,--no-insert-timestamp')
    & $Python $taskAdapter $taskCompiler @taskFlags $taskSource '-o' $taskChild
    if ($LASTEXITCODE -ne 0) { throw "Synthetic child fixture compilation failed: $LASTEXITCODE" }
    Write-Output $taskChild
} finally {
    $env:PATH = $taskSavedPath
}
