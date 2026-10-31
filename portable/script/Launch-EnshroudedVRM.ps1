# Copyright (c) 2026 lightnill. MIT; see LICENSE.
# Run directly, or use Launch-EnshroudedVRM.cmd. Normal execution policy applies.
[CmdletBinding()]
param(
    [string]$Game,
    [string]$Model,
    [switch]$Check,
    [switch]$Launch,
    [switch]$Play,
    [switch]$Wait,
    [switch]$NoSprings,
    [switch]$EnableSprings,
    [switch]$Version,
    [string]$Report
)
$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
[Console]::OutputEncoding = [Text.UTF8Encoding]::new($false)
$interactive = -not ($Check -or $Launch -or $Play -or $Version)
$result = $null
$code = 1
try {
    if ($Version) { [Console]::WriteLine('EnshroudedVRM 0.2.0 script launcher by lightnill'); exit 0 }
    if ($Play -and ($Check -or $Launch -or $PSBoundParameters.ContainsKey('Game') -or $PSBoundParameters.ContainsKey('Model') -or $NoSprings -or $EnableSprings)) {
        throw '-Play uses saved settings; choose it without -Check, -Launch, game/avatar paths or spring overrides.'
    }
    if ($Check -and $Launch) { throw 'Choose either -Check or -Launch.' }
    if ($NoSprings -and $EnableSprings) { throw 'Choose either -NoSprings or -EnableSprings.' }
    if ($Wait -and -not ($Launch -or $Play)) { throw '-Wait requires -Launch or -Play.' }
    Import-Module -Name (Join-Path $PSScriptRoot 'launcher_core.psm1') -Force -DisableNameChecking
    if ($Play) {
        $defaults = Get-EvrmSavedSettings $PSScriptRoot
        $Game = $defaults.Game
        $Model = $defaults.Model
        $springs = $defaults.Springs
    } else {
        $defaults = Get-EvrmDefaults $PSScriptRoot
        if (-not $Game) { $Game = $defaults.Game }
        if (-not $Model) { $Model = $defaults.Model }
        $springs = $defaults.Springs -and -not $NoSprings
        if ($EnableSprings) { $springs = $true }
    }
    if ($interactive) {
        Write-Host 'EnshroudedVRM by lightnill - press F8 in game to choose your avatar, lip sync and appearance.'
        $selections = @(Get-EvrmSetupSelections $Game $PSScriptRoot)
        if ($selections.Count) { Add-Type -AssemblyName System.Windows.Forms }
        foreach ($selection in $selections) {
            $dialog = [Windows.Forms.OpenFileDialog]::new()
            try {
                $dialog.RestoreDirectory = $true
                $dialog.CheckFileExists = $true
                $dialog.CheckPathExists = $true
                if ($selection -eq 'Game') {
                    $dialog.Title = 'Choose enshrouded.exe from Steam'
                    $dialog.Filter = 'Enshrouded (enshrouded.exe)|enshrouded.exe'
                    if ($Game) { $dialog.FileName = $Game }
                }
                if ($dialog.ShowDialog() -ne [Windows.Forms.DialogResult]::OK) { Write-Host 'Cancelled. No game was started.'; exit 0 }
                $Game = $dialog.FileName
            } finally { $dialog.Dispose() }
        }
    }
    $plan = Get-EvrmLaunchPlan -Root $PSScriptRoot -Game $Game -Model $Model -Springs $springs
    if ($Check) {
        $result = [ordered]@{ok=$true; version='0.2.0'; publicationStatus=$plan.PublicationStatus; runtimeSha256=$plan.RuntimeHash; captureFiles=$false; gameStarted=$false; launcherKind='script'}
    } else {
        Write-Host 'Press F8 in game for mod settings: avatar, voice to viseme and appearance presets.'
        if (-not $Wait) { Write-Host 'Starting Enshrouded. This window checks startup for up to 30 seconds, then closes automatically.' }
        $result = Invoke-EvrmLaunch $plan -Wait:$Wait
        if ($interactive) {
            if ($result.Contains('playScriptPath')) { Write-Host 'Next time, open Play-EnshroudedVRM.cmd. Change mod settings with F8 in game.' }
            if ($result.Contains('playScriptWarning')) { Write-Warning $result['playScriptWarning'] }
        }
    }
    $code = 0
    if ($result.Contains('exitCode')) { $code = [int]$result['exitCode'] }
} catch {
    $message = $_.Exception.Message
    if ($null -ne $result -and $result.Contains('gameStarted') -and $result['gameStarted']) {
        # UI/output preferences can turn a post-start notice into an exception.
        # Keep the successful launch result: the game is already user-owned.
        $result['launcherWarning'] = 'Game started; a launcher message could not be displayed: ' + $message
        $code = 0
        if ($result.Contains('exitCode')) { $code = [int]$result['exitCode'] }
    } else {
        if ($Play) { $message += ' Run Launch-EnshroudedVRM.cmd to review or repair your setup.' }
        $result = [ordered]@{ok=$false; gameStarted=$false; error=$message; launcherKind='script'}
    }
}
if ($Report) {
    try { Write-EvrmJson $Report $result }
    catch { $result['reportWarning'] = $_.Exception.Message; if (-not $result['gameStarted']) { $code = 1 } }
}
$json = ConvertTo-Json -InputObject $result -Depth 8 -Compress
if ($code -eq 0) { [Console]::WriteLine($json) } else { [Console]::Error.WriteLine($json) }
exit $code
