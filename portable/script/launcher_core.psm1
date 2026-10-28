# Copyright (c) 2026 lightnill. MIT; see the package LICENSE.
# Windows PowerShell 5.1. No downloads, policy changes or global installation.
Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Read-only filesystem calls preserve Unicode paths and resolve junctions before
# checking package boundaries. Add-Type uses Windows' installed .NET tooling.
if (-not ('EvrmScript.Paths' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.IO;
using System.Runtime.InteropServices;
using System.Text;
using Microsoft.Win32.SafeHandles;
namespace EvrmScript {
    public static class Paths {
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        private static extern SafeFileHandle CreateFileW(string path, uint access,
            uint share, IntPtr security, uint creation, uint flags, IntPtr template);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        private static extern uint GetFinalPathNameByHandleW(SafeFileHandle file,
            StringBuilder buffer, uint size, uint flags);
        [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
        private static extern uint GetShortPathNameW(string path, StringBuilder buffer, uint size);
        [StructLayout(LayoutKind.Sequential)]
        private struct FileInformation {
            public uint Attributes;
            public System.Runtime.InteropServices.ComTypes.FILETIME CreationTime;
            public System.Runtime.InteropServices.ComTypes.FILETIME LastAccessTime;
            public System.Runtime.InteropServices.ComTypes.FILETIME LastWriteTime;
            public uint VolumeSerialNumber, FileSizeHigh, FileSizeLow, NumberOfLinks, FileIndexHigh, FileIndexLow;
        }
        [DllImport("kernel32.dll", SetLastError=true)]
        private static extern bool GetFileInformationByHandle(SafeFileHandle file, out FileInformation information);
        public static bool IsSingleLink(string path) {
            using (SafeFileHandle file = CreateFileW(path, 0x80, 7, IntPtr.Zero, 3, 0x00200000, IntPtr.Zero)) {
                FileInformation information;
                if (file.IsInvalid || !GetFileInformationByHandle(file, out information))
                    throw new IOException("A diagnostic file's link count could not be checked.");
                return information.NumberOfLinks == 1;
            }
        }
        public static string Resolve(string path) {
            using (SafeFileHandle file = CreateFileW(path, 0x80, 7, IntPtr.Zero, 3, 0x02000000, IntPtr.Zero)) {
                if (file.IsInvalid) throw new IOException("A selected file or folder is missing or unreadable.", new Win32Exception(Marshal.GetLastWin32Error()));
                uint size = GetFinalPathNameByHandleW(file, null, 0, 0);
                if (size == 0 || size >= 32768) throw new IOException("A selected path could not be resolved.");
                StringBuilder text = new StringBuilder((int)size);
                uint count = GetFinalPathNameByHandleW(file, text, size, 0);
                if (count == 0 || count >= size) throw new IOException("A selected path changed while resolving it.");
                string result = text.ToString();
                if (result.StartsWith(@"\\?\UNC\", StringComparison.OrdinalIgnoreCase)) return @"\\" + result.Substring(8);
                if (result.StartsWith(@"\\?\", StringComparison.Ordinal)) return result.Substring(4);
                return result;
            }
        }
        public static string ShortName(string path) {
            uint size = GetShortPathNameW(path, null, 0);
            if (size == 0 || size >= 32768) throw new IOException("Windows has no usable short name for this folder. Extract the mod to a writable ASCII-only path.");
            StringBuilder text = new StringBuilder((int)size);
            uint count = GetShortPathNameW(path, text, size);
            if (count == 0 || count >= size) throw new IOException("The portable folder's short name could not be read.");
            return text.ToString();
        }
    }
}
'@
}

function Assert-Evrm([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}

function Read-EvrmText([string]$Path, [int]$Limit) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try {
        Assert-Evrm ($stream.Length -gt 0 -and $stream.Length -le $Limit) 'A package metadata file has an invalid size.'
        $bytes = New-Object byte[] ([int]$stream.Length)
        $offset = 0
        while ($offset -lt $bytes.Length) {
            $count = $stream.Read($bytes, $offset, $bytes.Length - $offset)
            Assert-Evrm ($count -gt 0) 'A package metadata file is truncated.'
            $offset += $count
        }
        Assert-Evrm ($stream.ReadByte() -eq -1) 'A package metadata file changed while reading it.'
        return ([Text.UTF8Encoding]::new($false, $true)).GetString($bytes).TrimStart([char]0xfeff)
    } finally { $stream.Dispose() }
}

function Get-EvrmProperty($Object, [string]$Name) {
    $property = $Object.PSObject.Properties[$Name]
    Assert-Evrm ($null -ne $property) 'The package metadata is incomplete.'
    return $property.Value
}

function Get-EvrmSha256([string]$Path) {
    $stream = [IO.File]::Open($Path, [IO.FileMode]::Open, [IO.FileAccess]::Read, [IO.FileShare]::Read)
    $hash = [Security.Cryptography.SHA256]::Create()
    try { return [BitConverter]::ToString($hash.ComputeHash($stream)).Replace('-', '').ToLowerInvariant() }
    finally { $hash.Dispose(); $stream.Dispose() }
}

function Test-EvrmMember([string]$Name) {
    if ([string]::IsNullOrEmpty($Name) -or $Name.Length -gt 240 -or $Name.StartsWith('/') -or $Name.IndexOfAny([char[]]@('\', ':', [char]0)) -ge 0) { return $false }
    foreach ($part in $Name.Split('/')) {
        if (-not $part -or $part -eq '.' -or $part -eq '..' -or $part.EndsWith('.') -or $part.EndsWith(' ')) { return $false }
        if ($part -match '[\x00-\x1f<>"|?*]') { return $false }
        if ($part.Split('.')[0] -match '^(con|prn|aux|nul|com[1-9]|lpt[1-9])$') { return $false }
    }
    return $true
}

function Get-EvrmLocalInputPath([string]$Path) {
    Assert-Evrm (-not [string]::IsNullOrWhiteSpace($Path)) 'Choose the game and a local VRM file.'
    Assert-Evrm (-not $Path.StartsWith('\\') -and -not $Path.StartsWith('//')) 'Use local folders for the mod, game and avatar.'
    $full = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($Path)
    Assert-Evrm (-not $full.StartsWith('\\') -and -not $full.StartsWith('//')) 'Use local folders for the mod, game and avatar.'
    $drive = [IO.DriveInfo]::new([IO.Path]::GetPathRoot($full))
    Assert-Evrm ($drive.DriveType -ne [IO.DriveType]::Network) 'Use local folders for the mod, game and avatar.'
    return $full
}

function Resolve-EvrmLocalPath([string]$Path) {
    $resolved = [EvrmScript.Paths]::Resolve((Get-EvrmLocalInputPath $Path))
    Assert-Evrm (-not $resolved.StartsWith('\\')) 'Use local folders for the mod, game and avatar.'
    return $resolved
}

function Get-EvrmLoaderDirectory([string]$Folder) {
    if ($Folder -notmatch '[^\x00-\x7f]') { return $Folder }
    $alias = [EvrmScript.Paths]::ShortName($Folder)
    Assert-Evrm ($alias -notmatch '[^\x00-\x7f]' -and -not $alias.Contains(';')) 'Vulkan needs an ASCII-only mod-folder path on this system.'
    Assert-Evrm ([string]::Equals([EvrmScript.Paths]::Resolve($alias), [EvrmScript.Paths]::Resolve($Folder), [StringComparison]::OrdinalIgnoreCase)) 'The Vulkan loader alias does not refer to this package.'
    return $alias
}

# Steam's libraryfolders.vdf supports quoted strings, escaped slash/quote pairs,
# comments and legacy numeric keys. Do not execute it as PowerShell code.
function Skip-EvrmVdfSpace($State) {
    while ($State.At -lt $State.Text.Length) {
        if ($State.Text[$State.At] -eq '/' -and $State.At + 1 -lt $State.Text.Length -and $State.Text[$State.At + 1] -eq '/') {
            while ($State.At -lt $State.Text.Length -and $State.Text[$State.At] -ne "`n") { $State.At++ }
        } elseif ([char]::IsWhiteSpace($State.Text[$State.At])) { $State.At++ }
        else { break }
    }
}

function Read-EvrmVdfToken($State) {
    Skip-EvrmVdfSpace $State
    Assert-Evrm ($State.At -lt $State.Text.Length) 'Steam library metadata is incomplete.'
    $character = $State.Text[$State.At]; $State.At++
    if ($character -eq '{' -or $character -eq '}') { return [string]$character }
    $text = [Text.StringBuilder]::new()
    if ($character -ne '"') {
        [void]$text.Append($character)
        while ($State.At -lt $State.Text.Length -and -not [char]::IsWhiteSpace($State.Text[$State.At]) -and $State.Text[$State.At] -ne '{' -and $State.Text[$State.At] -ne '}') {
            [void]$text.Append($State.Text[$State.At]); $State.At++
        }
        return $text.ToString()
    }
    while ($State.At -lt $State.Text.Length) {
        $character = $State.Text[$State.At]; $State.At++
        if ($character -eq '"') { return $text.ToString() }
        if ($character -eq '\' -and $State.At -lt $State.Text.Length -and ($State.Text[$State.At] -eq '\' -or $State.Text[$State.At] -eq '"')) {
            $character = $State.Text[$State.At]; $State.At++
        }
        [void]$text.Append($character)
    }
    throw 'Steam library metadata has an unfinished string.'
}

function Read-EvrmVdfObject($State, [int]$Depth, [bool]$Closed) {
    Assert-Evrm ($Depth -le 16) 'Steam library metadata is too deeply nested.'
    while ($true) {
        Skip-EvrmVdfSpace $State
        if ($State.At -eq $State.Text.Length) {
            Assert-Evrm (-not $Closed) 'Steam library metadata has an unfinished section.'
            return
        }
        $key = Read-EvrmVdfToken $State
        if ($key -eq '}') { Assert-Evrm $Closed 'Steam library metadata has an unexpected section.'; return }
        Assert-Evrm ($key -ne '{') 'Steam library metadata has an unexpected section.'
        $value = Read-EvrmVdfToken $State
        if ($value -eq '{') { Read-EvrmVdfObject $State ($Depth + 1) $true; continue }
        Assert-Evrm ($value -ne '}') 'Steam library metadata lacks a value.'
        if (($key -ceq 'path' -or $key -match '^\d+$') -and [IO.Path]::IsPathRooted($value) -and -not $value.StartsWith('\\')) {
            Assert-Evrm ($State.Found.Count -lt 128) 'Steam library list is too large.'
            if (-not $State.Found.Contains($value)) { $State.Found.Add($value) }
        }
    }
}

function Get-EvrmSteamLibraries([string]$Text) {
    Assert-Evrm ($Text.Length -le 2MB) 'Steam library metadata is too large.'
    $state = [pscustomobject]@{Text=$Text; At=0; Found=[Collections.Generic.List[string]]::new()}
    Read-EvrmVdfObject $state 0 $false
    return $state.Found.ToArray()
}

function Find-EvrmGame {
    $roots = [Collections.Generic.List[string]]::new()
    $key = [Microsoft.Win32.Registry]::CurrentUser.OpenSubKey('Software\Valve\Steam')
    if ($null -ne $key) {
        try { $value = $key.GetValue('SteamPath'); if ($value -is [string] -and $value) { $roots.Add($value) } }
        finally { $key.Dispose() }
    }
    foreach ($name in @('ProgramFiles(x86)', 'ProgramFiles')) {
        $value = [Environment]::GetEnvironmentVariable($name)
        if ($value) { $roots.Add([IO.Path]::Combine($value, 'Steam')) }
    }
    foreach ($root in $roots.ToArray()) {
        try {
            $root = Get-EvrmLocalInputPath $root
            $file = [IO.Path]::Combine($root, 'steamapps\libraryfolders.vdf')
            if ([IO.File]::Exists($file)) {
                foreach ($library in @(Get-EvrmSteamLibraries (Read-EvrmText $file 2MB))) { $roots.Add($library) }
            }
        } catch { # A file picker remains available for malformed local Steam metadata.
        }
    }
    foreach ($root in $roots) {
        try {
            $root = Get-EvrmLocalInputPath $root
            $game = [IO.Path]::Combine($root, 'steamapps\common\Enshrouded\enshrouded.exe')
            if ([IO.File]::Exists($game)) { return $game }
        } catch { # Ignore remote or unusable discovery roots; browsing remains available.
        }
    }
    return ''
}

function Get-EvrmDefaults([string]$Root) {
    $result = [ordered]@{Game=''; Model=''; Springs=$true}
    try {
        $settings = [IO.Path]::Combine($Root, 'State\settings.json')
        if ([IO.File]::Exists($settings)) {
            $saved = (Read-EvrmText $settings 32768) | ConvertFrom-Json
            foreach ($name in @('game', 'model')) {
                $property = $saved.PSObject.Properties[$name]
                if ($null -ne $property -and $property.Value -is [string]) { $result[$name] = $property.Value }
            }
            $property = $saved.PSObject.Properties['springs']
            if ($null -ne $property -and $property.Value -is [bool]) { $result.Springs = $property.Value }
        }
    } catch { # Invalid saved settings do not prevent selecting files again.
    }
    if (-not $result.Game) { $result.Game = Find-EvrmGame }
    if (-not $result.Model) {
        $folder = [IO.Path]::Combine($Root, 'Avatars')
        if ([IO.Directory]::Exists($folder)) {
            $models = @([IO.Directory]::EnumerateFiles($folder, '*.vrm') | Select-Object -First 2)
            if ($models.Count -eq 1) { $result.Model = $models[0] }
        }
    }
    return [pscustomobject]$result
}

function Get-EvrmSetupSelections([string]$Game, [string]$Root='') {
    $found = $false
    if ($Game) {
        try {
            $path = Resolve-EvrmLocalPath $Game
            $found = [IO.Path]::GetFileName($path) -ieq 'enshrouded.exe' -and [IO.File]::Exists($path)
            if ($found -and $Root) {
                $package = Resolve-EvrmLocalPath $Root
                $manifest = (Read-EvrmText ([IO.Path]::Combine($package, 'release-manifest.json')) 2MB) | ConvertFrom-Json
                $expected = Get-EvrmProperty $manifest 'game_sha256'
                Assert-Evrm ($expected -is [string] -and $expected -cmatch '^[0-9a-f]{64}$') 'The package has an invalid game fingerprint.'
                $found = (Get-EvrmSha256 $path) -ceq $expected
            }
        } catch { # Missing, unsupported or unreadable paths need a fresh selection.
            $found = $false
        }
    }
    if (-not $found) { 'Game' }
    'Model'
}

function Get-EvrmSavedSettings([string]$Root) {
    $settings = [IO.Path]::Combine($Root, 'State\settings.json')
    Assert-Evrm ([IO.File]::Exists($settings)) 'No saved setup was found.'
    try { $saved = (Read-EvrmText $settings 32768) | ConvertFrom-Json }
    catch { throw 'The saved setup could not be read.' }
    Assert-Evrm ($saved -is [pscustomobject]) 'The saved setup is invalid.'
    $game = Get-EvrmProperty $saved 'game'
    $model = Get-EvrmProperty $saved 'model'
    $springs = Get-EvrmProperty $saved 'springs'
    # IsPathRooted also accepts C:relative and \root-relative paths on .NET
    # Framework. Saved launches must not depend on the caller's directory/drive.
    Assert-Evrm ($game -is [string] -and $game -match '^[A-Za-z]:[\\/]') 'The saved game path is invalid.'
    Assert-Evrm ($model -is [string] -and $model -match '^[A-Za-z]:[\\/]') 'The saved avatar path is invalid.'
    Assert-Evrm ($springs -is [bool]) 'The saved spring setting is invalid.'
    return [pscustomobject]@{Game=$game; Model=$model; Springs=$springs}
}

function Get-EvrmLaunchPlan([string]$Root, [string]$Game, [string]$Model, [bool]$Springs=$true) {
    $rootPath = Resolve-EvrmLocalPath $Root
    $gamePath = Resolve-EvrmLocalPath $Game
    $modelPath = Resolve-EvrmLocalPath $Model
    Assert-Evrm ([IO.Directory]::Exists($rootPath)) 'The package folder is not a directory.'
    Assert-Evrm (-not $rootPath.Contains(';')) 'Move the portable mod into a folder without a semicolon (;). Vulkan treats it as a folder-list separator.'
    Assert-Evrm (-not $rootPath.Contains('%')) 'Move the portable mod into a folder without percent (%) signs. Windows can expand them before a CMD launcher starts.'
    $manifest = (Read-EvrmText ([IO.Path]::Combine($rootPath, 'release-manifest.json')) 2MB) | ConvertFrom-Json
    Assert-Evrm ((Get-EvrmProperty $manifest 'schema') -ceq 'enshrouded-vrm-portable-v1') 'This package has an unsupported manifest.'
    $status = Get-EvrmProperty $manifest 'publication_status'
    Assert-Evrm ($status -ceq 'preview' -or $status -ceq 'local-test-only') 'This package has an unsupported publication status.'
    $gameHash = Get-EvrmProperty $manifest 'game_sha256'
    Assert-Evrm ($gameHash -is [string] -and $gameHash -cmatch '^[0-9a-f]{64}$') 'The package has an invalid game fingerprint.'
    $files = Get-EvrmProperty $manifest 'files'
    Assert-Evrm ($files -is [Array] -and $files.Count -gt 0 -and $files.Count -le 128) 'The package file list is invalid.'
    $names = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    $runtimeHash = ''
    $prefix = $rootPath.TrimEnd('\') + '\'
    foreach ($entry in $files) {
        $name = Get-EvrmProperty $entry 'path'; $hash = Get-EvrmProperty $entry 'sha256'
        Assert-Evrm ($name -is [string] -and (Test-EvrmMember $name) -and $hash -is [string] -and $hash -cmatch '^[0-9a-f]{64}$') 'The package contains an unsafe manifest entry.'
        Assert-Evrm ($names.Add($name)) 'The package manifest lists a file twice.'
        $file = Resolve-EvrmLocalPath ([IO.Path]::Combine($rootPath, $name.Replace('/', '\')))
        Assert-Evrm ($file.StartsWith($prefix, [StringComparison]::OrdinalIgnoreCase) -and [IO.File]::Exists($file)) 'A required package file is missing or linked outside the package.'
        Assert-Evrm ((Get-EvrmSha256 $file) -ceq $hash) 'A package file differs from its release hash. Extract a fresh copy.'
        if ($name -ieq 'Runtime/enshrouded_vrm_runtime.dll') { $runtimeHash = $hash }
    }
    foreach ($name in @('Runtime/enshrouded_vrm_runtime.dll', 'Runtime/enshrouded_vrm_layer.json', 'Compatibility/profile.json', 'Compatibility/catalog.json')) {
        Assert-Evrm ($names.Contains($name)) 'The package manifest is missing a required component.'
    }
    Assert-Evrm ([IO.Path]::GetFileName($gamePath) -ieq 'enshrouded.exe' -and [IO.File]::Exists($gamePath)) 'Choose enshrouded.exe from your Steam installation.'
    Assert-Evrm ((Get-EvrmSha256 $gamePath) -ceq $gameHash) 'This game version is not supported by this preview. No modded game was started.'
    Assert-Evrm ([IO.Path]::GetExtension($modelPath) -ieq '.vrm' -and [IO.File]::Exists($modelPath)) 'Choose a local .vrm avatar file.'
    $stream = [IO.File]::OpenRead($modelPath)
    try {
        $header = New-Object byte[] 12
        $count = $stream.Read($header, 0, 12)
        Assert-Evrm ($count -eq 12 -and $stream.Length -ge 12 -and $stream.Length -le 512MB -and [BitConverter]::ToUInt32($header, 0) -eq 0x46546c67 -and [BitConverter]::ToUInt32($header, 4) -eq 2 -and [BitConverter]::ToUInt32($header, 8) -eq $stream.Length) 'This file is not a complete supported VRM/GLB container.'
    } finally { $stream.Dispose() }

    $environment = [Collections.Generic.Dictionary[string,string]]::new([StringComparer]::OrdinalIgnoreCase)
    foreach ($entry in [Environment]::GetEnvironmentVariables('Process').GetEnumerator()) {
        if (-not ([string]$entry.Key).StartsWith('ENSHROUDED_VRM_', [StringComparison]::OrdinalIgnoreCase)) { $environment[[string]$entry.Key] = [string]$entry.Value }
    }
    $runtime = [IO.Path]::Combine($rootPath, 'Runtime')
    $layerPath = Get-EvrmLoaderDirectory $runtime
    if ($environment.ContainsKey('VK_LAYER_PATH') -and $environment['VK_LAYER_PATH']) { $layerPath += ';' + $environment['VK_LAYER_PATH'] }
    $environment['VK_LAYER_PATH'] = $layerPath
    $layers = [Collections.Generic.List[string]]::new()
    $layers.Add('VK_LAYER_LOCAL_enshrouded_vrm')
    if ($environment.ContainsKey('VK_INSTANCE_LAYERS')) {
        foreach ($layer in $environment['VK_INSTANCE_LAYERS'].Split(';')) {
            if ($layer -and $layer -ine 'VK_LAYER_LOCAL_enshrouded_vrm') { $layers.Add($layer) }
        }
    }
    $environment['VK_INSTANCE_LAYERS'] = $layers -join ';'
    $session = [DateTime]::Now.ToString('yyyyMMdd-HHmmss') + '-' + $PID + '-' + [Guid]::NewGuid().ToString('N').Substring(0, 8)
    $log = [IO.Path]::Combine($rootPath, 'State\Logs\' + $session + '.log')
    $cache = [IO.Path]::Combine($rootPath, 'State\Cache')
    $values = @{
        ENSHROUDED_VRM_MODEL=$modelPath; ENSHROUDED_VRM_PROFILE=[IO.Path]::Combine($rootPath, 'Compatibility\profile.json')
        ENSHROUDED_VRM_CATALOG=[IO.Path]::Combine($rootPath, 'Compatibility\catalog.json'); ENSHROUDED_VRM_VERIFIED_BUILD=$gameHash
        ENSHROUDED_VRM_SHADERS=[IO.Path]::Combine($runtime, 'shaders'); ENSHROUDED_VRM_LOG=$log
        ENSHROUDED_VRM_CAPTURE_DIR=$cache; ENSHROUDED_VRM_CAPTURE_FRAMES='none'; ENSHROUDED_VRM_NATIVE_REPLACEMENT='1'
        ENSHROUDED_VRM_DEFER_RENDERING_PROOF='1'; ENSHROUDED_VRM_GPU_SKINNING='1'; ENSHROUDED_VRM_TOOL_SLOTS='1'
        ENSHROUDED_VRM_SPRING_BONES=$(if ($Springs) { '1' } else { '0' }); ENSHROUDED_VRM_DEBUG_OFFSET='0'
        SteamAppId='1203620'; SteamGameId='1203620'
    }
    foreach ($entry in $values.GetEnumerator()) { $environment[$entry.Key] = [string]$entry.Value }
    $size = 1
    foreach ($entry in $environment.GetEnumerator()) {
        Assert-Evrm ($entry.Key.Length -gt 0 -and $entry.Key.IndexOf([char]0) -lt 0 -and $entry.Value.IndexOf([char]0) -lt 0) 'Invalid launch environment.'
        $size += $entry.Key.Length + $entry.Value.Length + 2
    }
    Assert-Evrm ($size -lt 32760) 'The launch environment is too large.'
    return [pscustomobject]@{Root=$rootPath; Game=$gamePath; Model=$modelPath; Springs=$Springs; Environment=$environment; Log=$log; Cache=$cache; RuntimeHash=$runtimeHash; PublicationStatus=$status}
}

function Write-EvrmJson([string]$Path, $Value) {
    $Path = Get-EvrmLocalInputPath $Path
    $json = ConvertTo-Json -InputObject $Value -Depth 8 -Compress
    [IO.File]::WriteAllText($Path, $json + "`n", [Text.UTF8Encoding]::new($false))
}

function Write-EvrmPlayScript([string]$Root) {
    $rootPath = Resolve-EvrmLocalPath $Root
    $path = [IO.Path]::Combine($rootPath, 'Play-EnshroudedVRM.cmd')
    # No game/avatar paths are embedded. Invoke PowerShell directly so CALL's
    # second expansion cannot reinterpret percent signs in a moved package path.
    $text = @'
@echo off
setlocal DisableDelayedExpansion
rem Copyright (c) 2026 lightnill. MIT; see LICENSE.
rem Generated by EnshroudedVRM. Run Launch-EnshroudedVRM.cmd to change settings.
set "EVRM_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if exist "%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe" set "EVRM_POWERSHELL=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%EVRM_POWERSHELL%" -NoLogo -NoProfile -STA -File "%~dp0Launch-EnshroudedVRM.ps1" -Play %*
set "EVRM_EXIT=%ERRORLEVEL%"
if not "%EVRM_EXIT%"=="0" if "%~1"=="" pause
exit /b %EVRM_EXIT%
'@
    $text = ($text -replace "\r?\n", "`r`n") + "`r`n"
    if ([IO.File]::Exists($path) -or [IO.Directory]::Exists($path)) {
        $attributes = [IO.File]::GetAttributes($path)
        Assert-Evrm (($attributes -band ([IO.FileAttributes]::ReparsePoint -bor [IO.FileAttributes]::Directory)) -eq 0) 'Play-EnshroudedVRM.cmd already exists as a directory or link; it was left unchanged.'
        Assert-Evrm ([string]::Equals((Read-EvrmText $path 4096), $text, [StringComparison]::Ordinal)) 'Play-EnshroudedVRM.cmd already has different contents; it was left unchanged. Rename it and run setup again.'
        return $path
    }
    # Publish a complete new file without replacing an existing target, including
    # one created between the existence check and the move.
    $temporary = [IO.Path]::Combine($rootPath, '.evrm-play-' + [Guid]::NewGuid().ToString('N') + '.tmp')
    try {
        $bytes = [Text.Encoding]::ASCII.GetBytes($text)
        $stream = [IO.File]::Open($temporary, [IO.FileMode]::CreateNew, [IO.FileAccess]::Write, [IO.FileShare]::None)
        try { $stream.Write($bytes, 0, $bytes.Length); $stream.Flush($true) }
        finally { $stream.Dispose() }
        [IO.File]::Move($temporary, $path)
    } finally {
        if ([IO.File]::Exists($temporary)) { [IO.File]::Delete($temporary) }
    }
    return $path
}

function Get-EvrmLogFolder([string]$Root) {
    $folder = Resolve-EvrmLocalPath $Root
    foreach ($part in @('State', 'Logs')) {
        $folder = [IO.Path]::Combine($folder, $part)
        if ([IO.File]::Exists($folder) -or [IO.Directory]::Exists($folder)) {
            $attributes = [IO.File]::GetAttributes($folder)
            Assert-Evrm (($attributes -band [IO.FileAttributes]::Directory) -ne 0 -and ($attributes -band [IO.FileAttributes]::ReparsePoint) -eq 0) 'The diagnostic folder must be a real local directory, not a link.'
        } else { [void][IO.Directory]::CreateDirectory($folder) }
        Assert-Evrm ([string]::Equals((Resolve-EvrmLocalPath $folder), $folder, [StringComparison]::OrdinalIgnoreCase)) 'The diagnostic folder resolves outside its expected location.'
    }
    return $folder
}

function Get-EvrmDiagnosticFile($Plan, [string]$Suffix) {
    $session = [IO.Path]::GetFileNameWithoutExtension($Plan.Log)
    Assert-Evrm ($session -cmatch '^\d{8}-\d{6}-\d{1,10}-[0-9a-f]{8}$') 'Invalid diagnostic session name.'
    Assert-Evrm ($Suffix -cin @('.log', '.game.log', '.attempt.json')) 'Invalid diagnostic file type.'
    $folder = Get-EvrmLogFolder $Plan.Root
    $path = [IO.Path]::Combine($folder, $session + $Suffix)
    if ([IO.File]::Exists($path) -or [IO.Directory]::Exists($path)) {
        Assert-Evrm (([IO.File]::GetAttributes($path) -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -eq 0) 'A diagnostic file is a directory or link and was left unchanged.'
        Assert-Evrm ([EvrmScript.Paths]::IsSingleLink($path)) 'A diagnostic file has multiple hard links and was left unchanged.'
    }
    return $path
}

function Write-EvrmDiagnosticBytes([string]$Path, [byte[]]$Bytes) {
    if ([IO.File]::Exists($Path) -or [IO.Directory]::Exists($Path)) {
        Assert-Evrm (([IO.File]::GetAttributes($Path) -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -eq 0) 'A diagnostic file is a directory or link and was left unchanged.'
        Assert-Evrm ([EvrmScript.Paths]::IsSingleLink($Path)) 'A diagnostic file has multiple hard links and was left unchanged.'
        $mode = [IO.FileMode]::Open
    } else { $mode = [IO.FileMode]::CreateNew }
    $stream = [IO.File]::Open($Path, $mode, [IO.FileAccess]::Write, [IO.FileShare]::Read)
    try { $stream.SetLength(0); $stream.Write($Bytes, 0, $Bytes.Length) }
    finally { $stream.Dispose() }
}

function Write-EvrmLaunchAttempt($Plan, $Attempt) {
    $path = Get-EvrmDiagnosticFile $Plan '.attempt.json'
    $bytes = [Text.UTF8Encoding]::new($false).GetBytes((ConvertTo-Json -InputObject $Attempt -Depth 4 -Compress) + [Environment]::NewLine)
    Assert-Evrm ($bytes.Length -le 16384) 'The launch-attempt record is too large.'
    Write-EvrmDiagnosticBytes $path $bytes
}

function New-EvrmLaunchAttempt($Plan) {
    $record = [ordered]@{
        session=[IO.Path]::GetFileNameWithoutExtension($Plan.Log); startedUtc=[DateTime]::UtcNow.ToString('o')
        status='launching'; gameStarted=$false; processId=$null; earlyExit=$null; exitCode=$null
        gameLogCaptured=$false; gameLogTruncated=$false; startupWindowSeconds=30
    }
    try { Write-EvrmLaunchAttempt $Plan $record }
    catch { $record['diagnosticWarning']=$_.Exception.Message.Substring(0,[Math]::Min(512,$_.Exception.Message.Length)) }
    return $record
}

function Copy-EvrmLogTail([string]$Source, [string]$Destination, [int]$Limit) {
    Assert-Evrm (([IO.File]::GetAttributes($Source) -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -eq 0) 'The source log is not a regular file.'
    Assert-Evrm ([EvrmScript.Paths]::IsSingleLink($Source)) 'The source log has multiple hard links and was left unchanged.'
    $stream = [IO.File]::Open($Source, [IO.FileMode]::Open, [IO.FileAccess]::Read, ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    try {
        $length = $stream.Length
        $count = [int][Math]::Min($length, $Limit)
        $bytes = New-Object byte[] $count
        [void]$stream.Seek([Math]::Max(0, $length - $count), [IO.SeekOrigin]::Begin)
        $offset = 0
        while ($offset -lt $count) {
            $read = $stream.Read($bytes, $offset, $count - $offset)
            Assert-Evrm ($read -gt 0) 'The game log changed while reading it.'
            $offset += $read
        }
    } finally { $stream.Dispose() }
    Write-EvrmDiagnosticBytes $Destination $bytes
    return [pscustomobject]@{sourceBytes=$length; keptBytes=$count; truncated=($length -gt $count)}
}

function Get-EvrmGameLogStamp($Plan) {
    $path = [IO.Path]::Combine([IO.Path]::GetDirectoryName($Plan.Game), 'enshrouded.log')
    if (-not [IO.File]::Exists($path)) { return $null }
    Assert-Evrm (([IO.File]::GetAttributes($path) -band ([IO.FileAttributes]::Directory -bor [IO.FileAttributes]::ReparsePoint)) -eq 0) 'The game log is a directory or link.'
    Assert-Evrm ([EvrmScript.Paths]::IsSingleLink($path)) 'The game log has multiple hard links and was left unchanged.'
    $stream = [IO.File]::Open($path, [IO.FileMode]::Open, [IO.FileAccess]::Read, ([IO.FileShare]::ReadWrite -bor [IO.FileShare]::Delete))
    $hash = [Security.Cryptography.SHA256]::Create()
    try {
        $head = New-Object byte[] ([int][Math]::Min(256, $stream.Length))
        $read = $stream.Read($head, 0, $head.Length)
        $digest = [BitConverter]::ToString($hash.ComputeHash($head, 0, $read))
        return [pscustomobject]@{path=$path; length=$stream.Length; writeTicks=[IO.File]::GetLastWriteTimeUtc($path).Ticks; head=$digest}
    } finally { $hash.Dispose(); $stream.Dispose() }
}

function Save-EvrmStartupLog($Plan, $Attempt, $Baseline) {
    if ($null -ne $Baseline -and $null -ne $Baseline.PSObject.Properties['unavailable'] -and $Baseline.unavailable) {
        $warning='Game-log snapshot skipped because the previous log could not be checked: '+$Baseline.reason
        $Attempt['diagnosticWarning']=$warning.Substring(0,[Math]::Min(512,$warning.Length))
        return
    }
    $stamp = Get-EvrmGameLogStamp $Plan
    if ($null -eq $stamp -or $stamp.length -eq 0) { return }
    if ($null -ne $Baseline -and $stamp.length -eq $Baseline.length -and $stamp.writeTicks -eq $Baseline.writeTicks -and $stamp.head -ceq $Baseline.head) { return }
    $copy = Copy-EvrmLogTail $stamp.path (Get-EvrmDiagnosticFile $Plan '.game.log') 262144
    $Attempt['gameLogCaptured'] = $true
    $Attempt['gameLogTruncated'] = $copy.truncated
    $Attempt['gameLogSourceBytes'] = $copy.sourceBytes
    $Attempt['gameLogSavedBytes'] = $copy.keptBytes
}

function Remove-EvrmOldLogs($Plan) {
    $folder = Get-EvrmLogFolder $Plan.Root
    $owned = @(
        foreach ($path in [IO.Directory]::EnumerateFiles($folder)) {
            $name = [IO.Path]::GetFileName($path)
            if ($name -cnotmatch '^(?<session>\d{8}-\d{6}-\d{1,10}-[0-9a-f]{8})(?<suffix>\.attempt\.json|\.game\.log|\.log)$') { continue }
            $session=$Matches.session; $suffix=$Matches.suffix
            if (([IO.File]::GetAttributes($path) -band [IO.FileAttributes]::ReparsePoint) -ne 0) { continue }
            if (-not [EvrmScript.Paths]::IsSingleLink($path)) { continue }
            [pscustomobject]@{session=$session; suffix=$suffix; path=$path; created=[IO.File]::GetCreationTimeUtc($path)}
        }
    )
    $groups = @($owned | Group-Object session | ForEach-Object {
        [pscustomobject]@{name=$_.Name; files=@($_.Group); created=($_.Group | Sort-Object created | Select-Object -First 1).created}
    } | Sort-Object @{Expression='created'; Descending=$true}, @{Expression='name'; Descending=$true})
    $current = [IO.Path]::GetFileNameWithoutExtension($Plan.Log)
    $keep = [Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
    [void]$keep.Add($current)
    foreach ($group in $groups) {
        if ($keep.Count -ge 5) { break }
        [void]$keep.Add($group.name)
    }
    foreach ($group in $groups) {
        foreach ($file in $group.files) {
            # Validate each absolute target again immediately before mutation.
            $memberPlan = [pscustomobject]@{Root=$Plan.Root; Log=$file.path}
            # GetFileNameWithoutExtension needs the native .log form for a group.
            $memberPlan.Log = [IO.Path]::Combine($folder, $group.name + '.log')
            $target = Get-EvrmDiagnosticFile $memberPlan $file.suffix
            Assert-Evrm ([string]::Equals([IO.Path]::GetDirectoryName((Resolve-EvrmLocalPath $target)), $folder, [StringComparison]::OrdinalIgnoreCase)) 'A diagnostic cleanup target resolves outside State/Logs.'
            if (-not $keep.Contains($group.name)) { [IO.File]::Delete($target) }
            elseif ($group.name -cne $current -and $file.suffix -ceq '.log' -and ([IO.FileInfo]::new($target)).Length -gt 1048576) {
                [void](Copy-EvrmLogTail $target $target 1048576)
            }
        }
    }
}

function Watch-EvrmStartup($Plan, $Process, $Attempt, $Baseline, [double]$Seconds=30) {
    Assert-Evrm ($Seconds -gt 0 -and $Seconds -le 30) 'Invalid startup observation window.'
    $timer = [Diagnostics.Stopwatch]::StartNew()
    $exited = $false
    do {
        try { Save-EvrmStartupLog $Plan $Attempt $Baseline }
        catch { $Attempt['diagnosticWarning'] = $_.Exception.Message.Substring(0, [Math]::Min(512, $_.Exception.Message.Length)) }
        $remaining = [int][Math]::Ceiling(($Seconds - $timer.Elapsed.TotalSeconds) * 1000)
        if ($remaining -le 0) { break }
        if ($Process.WaitForExit([Math]::Min(1000, $remaining))) { $exited=$true; break }
    } while ($timer.Elapsed.TotalSeconds -lt $Seconds)
    if ($Process.HasExited) { $exited=$true }
    try { Save-EvrmStartupLog $Plan $Attempt $Baseline }
    catch { $Attempt['diagnosticWarning'] = $_.Exception.Message.Substring(0, [Math]::Min(512, $_.Exception.Message.Length)) }
    $Attempt['gameStarted']=$true
    $Attempt['status']=$(if ($exited) { 'exited-during-startup' } else { 'running-after-startup-check' })
    $Attempt['earlyExit']=$exited
    if ($exited) { $Attempt['exitCode']=$Process.ExitCode }
    $Attempt['observedSeconds']=[Math]::Round($timer.Elapsed.TotalSeconds, 3)
    $Attempt['finishedUtc']=[DateTime]::UtcNow.ToString('o')
    try { Write-EvrmLaunchAttempt $Plan $Attempt }
    catch { $Attempt['diagnosticWarning'] = $_.Exception.Message.Substring(0, [Math]::Min(512, $_.Exception.Message.Length)) }
    return $Attempt
}

function Invoke-EvrmLaunch($Plan, [switch]$Wait) {
    $running = @([Diagnostics.Process]::GetProcessesByName('enshrouded'))
    $alreadyRunning = $running.Count -gt 0
    foreach ($process in $running) { $process.Dispose() }
    Assert-Evrm (-not $alreadyRunning) 'Close Enshrouded before launching it through this mod.'
    [void][IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($Plan.Log))
    [void][IO.Directory]::CreateDirectory($Plan.Cache)
    Write-EvrmJson ([IO.Path]::Combine($Plan.Root, 'State\settings.json')) ([ordered]@{game=$Plan.Game; model=$Plan.Model; springs=$Plan.Springs})
    $attempt=$null; $baseline=$null; $diagnosticWarning=''
    try { $baseline=Get-EvrmGameLogStamp $Plan }
    catch {
        $diagnosticWarning=$_.Exception.Message.Substring(0,[Math]::Min(512,$_.Exception.Message.Length))
        $baseline=[pscustomobject]@{unavailable=$true; reason=$diagnosticWarning}
    }
    try { $attempt=New-EvrmLaunchAttempt $Plan; Remove-EvrmOldLogs $Plan }
    catch { $diagnosticWarning=$_.Exception.Message }
    $start = [Diagnostics.ProcessStartInfo]::new()
    $start.FileName = $Plan.Game
    $start.WorkingDirectory = [IO.Path]::GetDirectoryName($Plan.Game)
    $start.UseShellExecute = $false
    $start.EnvironmentVariables.Clear()
    foreach ($entry in $Plan.Environment.GetEnumerator()) { $start.EnvironmentVariables[$entry.Key] = $entry.Value }
    $child = [Diagnostics.Process]::new()
    $child.StartInfo = $start
    try {
        Assert-Evrm ($child.Start()) 'Windows could not start Enshrouded.'
    } catch {
        if ($null -ne $attempt) {
            $attempt['status']='launch-failed'; $attempt['finishedUtc']=[DateTime]::UtcNow.ToString('o')
            $attempt['error']=$_.Exception.Message.Substring(0,[Math]::Min(512,$_.Exception.Message.Length))
            try { Write-EvrmLaunchAttempt $Plan $attempt } catch {}
        }
        $child.Dispose(); throw
    }
    # From this point the child is owned by the user. Reporting failures are
    # warnings; never retry the launch or terminate a successfully started game.
    $result = [ordered]@{gameStarted=$true; processId=$child.Id; executable=$Plan.Game; model=$Plan.Model; logPath=$Plan.Log; runtimeSha256=$Plan.RuntimeHash; captureFiles=$false; gpuSkinning=$true; publicationStatus=$Plan.PublicationStatus; launcherKind='script'}
    try {
        if ($diagnosticWarning) { $result['diagnosticWarning']=$diagnosticWarning }
        if ($null -ne $attempt) {
            $attempt['gameStarted']=$true; $attempt['processId']=$child.Id; $attempt['status']='checking-startup'
            if ($diagnosticWarning) { $attempt['diagnosticWarning']=$diagnosticWarning.Substring(0,[Math]::Min(512,$diagnosticWarning.Length)) }
            try {
                Write-EvrmLaunchAttempt $Plan $attempt
                $result['attemptReportPath']=Get-EvrmDiagnosticFile $Plan '.attempt.json'
            } catch { $result['diagnosticWarning']='Game started; its launch-attempt record could not be saved: '+$_.Exception.Message }
        }
        try { $result['playScriptPath'] = Write-EvrmPlayScript $Plan.Root }
        catch { $result['playScriptWarning'] = 'Game started; Play-EnshroudedVRM.cmd could not be created: ' + $_.Exception.Message }
        try { $result['startFileTimeUtc'] = $child.StartTime.ToFileTimeUtc() }
        catch { $result['identityWarning'] = 'Game started; its creation time could not be collected.' }
        try { Write-EvrmJson ([IO.Path]::Combine($Plan.Root, 'State\last-session.json')) $result }
        catch { $result['settingsWarning'] = 'Game started; its local session report could not be saved.' }
        if ($null -ne $attempt) {
            try {
                $attempt=Watch-EvrmStartup $Plan $child $attempt $baseline
                $result['startupStatus']=$attempt['status']; $result['earlyExit']=$attempt['earlyExit']
                if ($null -ne $attempt['exitCode']) { $result['exitCode']=$attempt['exitCode'] }
                if ($attempt.Contains('diagnosticWarning')) { $result['diagnosticWarning']=$attempt['diagnosticWarning'] }
            } catch { $result['diagnosticWarning']='Game started; startup diagnostics could not be completed: '+$_.Exception.Message }
        }
        if ($Wait) {
            try { $child.WaitForExit(); $result['exitCode'] = $child.ExitCode }
            catch { $result['waitWarning'] = 'Game started; its exit status could not be collected.' }
        }
        try { Write-EvrmJson ([IO.Path]::Combine($Plan.Root, 'State\last-session.json')) $result }
        catch { $result['settingsWarning'] = 'Game started; its local session report could not be saved.' }
    } finally { $child.Dispose() }
    return $result
}

Export-ModuleMember -Function Get-EvrmDefaults, Get-EvrmSetupSelections, Get-EvrmSavedSettings, Get-EvrmSteamLibraries, Get-EvrmLaunchPlan, Invoke-EvrmLaunch, Write-EvrmJson, Write-EvrmPlayScript
