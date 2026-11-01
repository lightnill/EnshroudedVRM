# Local, per-run game voice observation. Audio is never saved.
$ErrorActionPreference = 'Stop'
$taskCandidate = Join-Path $PSScriptRoot 'live-candidate\EnshroudedVRM-voice-observer-r3'
$taskCache = Join-Path $taskCandidate 'State\Cache'
New-Item -ItemType Directory -Path $taskCache -Force | Out-Null
$taskRequest = Join-Path $taskCache 'voice-observer.request'
[IO.File]::WriteAllText($taskRequest, '', [Text.UTF8Encoding]::new($false))
$taskCode = 1
try {
    # A child host contains the saved launcher's exit so this finally always runs.
    $taskHost = Join-Path $env:SystemRoot 'System32\WindowsPowerShell\v1.0\powershell.exe'
    & $taskHost -NoLogo -NoProfile -STA -File (Join-Path $taskCandidate 'Launch-EnshroudedVRM.ps1') -Play @args
    $taskCode = $LASTEXITCODE
} finally {
    # Consumed requests are already gone; rejected/unfinished launches leave none.
    [IO.File]::Delete($taskRequest)
}
exit $taskCode
