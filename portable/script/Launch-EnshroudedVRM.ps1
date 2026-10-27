# Copyright (c) 2026 lightnill. MIT; see LICENSE.
# Run directly, or use Launch-EnshroudedVRM.cmd. Normal execution policy applies.
[CmdletBinding()]
param(
    [string]$Game,
    [string]$Model,
    [switch]$Check,
    [switch]$Launch,
    [switch]$Wait,
    [switch]$NoSprings,
    [switch]$EnableSprings,
    [switch]$Version,
    [string]$Report
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$interactive = -not ($Check -or $Launch -or $Version)
$result = $null
$code = 1
try {
    if ($Version) { [Console]::WriteLine('EnshroudedVRM 0.1.0-preview script launcher by lightnill'); exit 0 }
    if ($Check -and $Launch) { throw 'Choose either -Check or -Launch.' }
    if ($NoSprings -and $EnableSprings) { throw 'Choose either -NoSprings or -EnableSprings.' }
    if ($Wait -and -not $Launch) { throw '-Wait requires -Launch.' }
    Import-Module -Name (Join-Path $PSScriptRoot 'launcher_core.psm1') -Force -DisableNameChecking
    $defaults = Get-EvrmDefaults $PSScriptRoot
    if (-not $Game) { $Game = $defaults.Game }
    if (-not $Model) { $Model = $defaults.Model }
    $springs = $defaults.Springs -and -not $NoSprings
    if ($EnableSprings) { $springs = $true }
    if ($interactive) {
        Write-Host 'EnshroudedVRM - readable script launcher by lightnill'
        Write-Host 'Select the game and avatar. The package and supported game build will be checked before launch.'
        Add-Type -AssemblyName System.Windows.Forms
        foreach ($selection in @('Game', 'Model')) {
            if ($selection -eq 'Game' -and $Game) { continue }
            $dialog = [Windows.Forms.OpenFileDialog]::new()
            try {
                $dialog.RestoreDirectory = $true
                $dialog.CheckFileExists = $true
                $dialog.CheckPathExists = $true
                if ($selection -eq 'Game') {
                    $dialog.Title = 'Choose enshrouded.exe from Steam'
                    $dialog.Filter = 'Enshrouded (enshrouded.exe)|enshrouded.exe'
                } else {
                    $dialog.Title = 'Choose your VRM avatar'
                    $dialog.Filter = 'VRM avatars (*.vrm)|*.vrm'
                    if ($Model) { $dialog.FileName = $Model }
                    elseif (Test-Path -LiteralPath (Join-Path $PSScriptRoot 'Avatars') -PathType Container) { $dialog.InitialDirectory = Join-Path $PSScriptRoot 'Avatars' }
                }
                if ($dialog.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { Write-Host 'Cancelled. No game was started.'; exit 0 }
                if ($selection -eq 'Game') { $Game = $dialog.FileName } else { $Model = $dialog.FileName }
            } finally { $dialog.Dispose() }
        }
    }
    $plan = Get-EvrmLaunchPlan -Root $PSScriptRoot -Game $Game -Model $Model -Springs $springs
    if ($Check) {
        $result = [ordered]@{ok=$true; version='0.1.0-preview'; publicationStatus=$plan.PublicationStatus; runtimeSha256=$plan.RuntimeHash; captureFiles=$false; gameStarted=$false; launcherKind='script'}
    } else {
        if ($interactive) {
            Write-Host ('Game: ' + $plan.Game)
            Write-Host ('Avatar: ' + $plan.Model)
            $answer = Read-Host 'Checks passed. Start Enshrouded with this avatar? [y/N]'
            if ($answer -ine 'y' -and $answer -ine 'yes') { Write-Host 'Cancelled. No game was started.'; exit 0 }
        }
        $result = Invoke-EvrmLaunch $plan -Wait:$Wait
    }
    $code = 0
    if ($result.Contains('exitCode')) { $code = [int]$result['exitCode'] }
} catch {
    $result = [ordered]@{ok=$false; gameStarted=$false; error=$_.Exception.Message; launcherKind='script'}
}
if ($Report) {
    try { Write-EvrmJson $Report $result }
    catch { $result['reportWarning'] = $_.Exception.Message; if (-not $result['gameStarted']) { $code = 1 } }
}
$json = ConvertTo-Json -InputObject $result -Depth 8 -Compress
if ($code -eq 0) { [Console]::WriteLine($json) } else { [Console]::Error.WriteLine($json) }
exit $code
