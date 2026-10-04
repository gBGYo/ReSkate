[CmdletBinding()]
param([string]$Package = 'artifacts/ReSkate_HallOfMeat-0.1.0.zip')

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
$packagePath = [IO.Path]::GetFullPath((Join-Path $repo $Package))
$fixture = Join-Path $repo ('build/hom-package-test-' + [Guid]::NewGuid().ToString('N'))
$game = Join-Path $fixture 'Game folder with spaces'
$mod = Join-Path $game 'Mods/ReSkate_HallOfMeat'
$utf8 = New-Object Text.UTF8Encoding($false)
Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression

function Check([bool]$Condition, [string]$Message) {
    if (-not $Condition) { throw $Message }
}
function Hash([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Put([string]$Path, [string]$Text) { [IO.File]::WriteAllText($Path, $Text, $utf8) }
function Run([string]$Action, [bool]$ShouldSucceed, [switch]$DefaultPath) {
    if ($DefaultPath) {
        $result = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $mod 'Manage.ps1') -Action $Action 2>&1
    } else {
        $result = & powershell.exe -NoProfile -ExecutionPolicy Bypass -File (Join-Path $mod 'Manage.ps1') -Action $Action -GameDirectory $game 2>&1
    }
    $code = $LASTEXITCODE
    Check (($code -eq 0) -eq $ShouldSucceed) "Unexpected result ($code) for $Action : $result"
}
function Original-Pair {
    foreach ($name in $names) { Check ((Hash (Join-Path $game $name)) -eq $original[$name]) "Original $name changed." }
}
function Installed-Pair {
    foreach ($name in $names) { Check ((Hash (Join-Path $game $name)) -eq $payload[$name]) "Installed $name mismatch." }
}

New-Item -ItemType Directory -Path $game -Force | Out-Null
try {
    [IO.Compression.ZipFile]::ExtractToDirectory($packagePath, $mod)
    $info = Get-Content -LiteralPath (Join-Path $mod 'build-info.json') -Raw | ConvertFrom-Json
    $names = @('ReSkate.dll', 'ReSkateLauncher.exe')
    foreach ($name in $names) {
        Check ((Hash (Join-Path $mod $name)) -eq $info.binaries.$name.sha256) "ZIP binary hash mismatch: $name"
    }
    $manifest = Get-Content -LiteralPath (Join-Path $mod 'manifest.json') -Raw | ConvertFrom-Json
    Check ($manifest.name -eq 'ReSkate_HallOfMeat') 'Wrong mod name.'
    $levels = Get-Content -LiteralPath (Join-Path $mod 'reskate-levels.json') -Raw | ConvertFrom-Json
    Check ($levels.schema -eq 1 -and $levels.levels.Count -eq 0) 'Wrong launcher content marker.'
    $zip = [IO.Compression.ZipFile]::OpenRead($packagePath)
    try {
        Check ($null -ne $zip.GetEntry('manifest.json')) 'Manifest must be at ZIP root.'
        Check ($null -ne $zip.GetEntry('icon.png')) 'Missing mod icon.'
        $expected = @('build-info.json', 'CHANGELOG.md', 'icon.png', 'Install.bat', 'INSTALL.txt',
            'Manage.ps1', 'manifest.json', 'README.md', 'reskate-levels.json', 'ReSkate.dll',
            'ReSkateLauncher.exe', 'Uninstall.bat')
        Check ($zip.Entries.Count -eq $expected.Count) 'Unexpected files in the runtime-only package.'
        foreach ($entry in $zip.Entries) {
            Check ($entry.FullName -cin $expected) "Unexpected ZIP entry: $($entry.FullName)"
        }
    } finally { $zip.Dispose() }
    Write-Host 'PASS: trainer-style archive contents and binary hashes; no bundled source or licenses.'

    # This fixture owns dummy binaries, never the user's running game/launcher.
    # Isolate process names only in its extracted script so a live session cannot
    # mask another validation failure or block the copy/rollback tests.
    $managerPath = Join-Path $mod 'Manage.ps1'
    $manager = Get-Content -LiteralPath $managerPath -Raw
    $processCheck = 'Get-Process -Name $processName -ErrorAction SilentlyContinue'
    Check ($manager.Contains($processCheck)) 'The fixture process-check substitution needs updating.'
    Put $managerPath ($manager.Replace($processCheck,
        "Get-Process -Name ('HOMFixture_' + " + '$processName) -ErrorAction SilentlyContinue'))

    # Only the extracted fixture metadata uses dummy file identities; the shipping
    # package keeps the real supported Skate.exe hash and its original binaries.
    Put (Join-Path $game 'Skate.exe') 'fixture game'
    $info.supported_game.sha256 = Hash (Join-Path $game 'Skate.exe')
    $original = @{}; $payload = @{}
    foreach ($name in $names) {
        Put (Join-Path $game $name) "original $name"
        Put (Join-Path $mod $name) "Hall of Meat $name"
        $original[$name] = Hash (Join-Path $game $name)
        $payload[$name] = Hash (Join-Path $mod $name)
        $info.binaries.$name.sha256 = $payload[$name]
    }
    $info | ConvertTo-Json -Depth 6 | Set-Content -LiteralPath (Join-Path $mod 'build-info.json') -Encoding UTF8
    Run 'Uninstall' $false
    Original-Pair
    Put (Join-Path $game 'Skate.exe') 'wrong game'
    Run 'Install' $false
    Original-Pair
    Put (Join-Path $game 'Skate.exe') 'fixture game'
    Put (Join-Path $mod 'ReSkate.dll') 'damaged payload'
    Run 'Install' $false
    Original-Pair
    Put (Join-Path $mod 'ReSkate.dll') 'Hall of Meat ReSkate.dll'
    Write-Host 'PASS: no-backup removal, unsupported game and damaged payload rejected without changes.'

    # Deny writes to the second target while allowing hashing, to force rollback
    # after the first binary has been installed.
    $lock = [IO.File]::Open((Join-Path $game 'ReSkateLauncher.exe'), [IO.FileMode]::Open,
        [IO.FileAccess]::Read, [IO.FileShare]::Read)
    try { Run 'Install' $false; Original-Pair } finally { $lock.Dispose() }
    Write-Host 'PASS: failed second-file copy rolls the first file back.'
    Run 'Install' $true
    Installed-Pair
    foreach ($name in $names) { Check ((Hash (Join-Path $mod "backup/$name")) -eq $original[$name]) "Backup mismatch: $name" }
    Run 'Install' $true
    Installed-Pair
    foreach ($name in $names) { Check ((Hash (Join-Path $mod "backup/$name")) -eq $original[$name]) "Reinstall changed backup: $name" }
    Write-Host 'PASS: install and reinstall retain the original backup pair.'

    Put (Join-Path $game 'ReSkate.dll') 'another runtime mod'
    Run 'Uninstall' $false
    Check (([IO.File]::ReadAllText((Join-Path $game 'ReSkate.dll'))) -eq 'another runtime mod') 'Uninstaller overwrote another mod.'
    Put (Join-Path $game 'ReSkate.dll') 'Hall of Meat ReSkate.dll'
    Put (Join-Path $mod 'backup/ReSkate.dll') 'damaged backup'
    Run 'Uninstall' $false
    Installed-Pair
    Put (Join-Path $mod 'backup/ReSkate.dll') 'original ReSkate.dll'
    Write-Host 'PASS: changed runtime and corrupted backup are protected.'
    Run 'Uninstall' $true
    Original-Pair
    Run 'Install' $true
    Installed-Pair
    Run 'Uninstall' $true
    Original-Pair
    Write-Host 'PASS: uninstall restores both files; reinstall after uninstall works.'

    Run 'Install' $true -DefaultPath
    Installed-Pair
    Run 'Uninstall' $true -DefaultPath
    Original-Pair
    Write-Host 'PASS: Windows PowerShell resolves the game folder when -GameDirectory is omitted.'

    $previousNoPause = $env:RESKATE_HOM_NO_PAUSE
    $env:RESKATE_HOM_NO_PAUSE = '1'
    try {
        foreach ($action in @('Install', 'Uninstall')) {
            $batch = Join-Path $mod "$action.bat"
            $batchOutput = & $env:ComSpec /d /c ('"' + $batch + '"') 2>&1
            Check ($LASTEXITCODE -eq 0) "Batch $action failed: $batchOutput"
            if ($action -eq 'Install') { Installed-Pair } else { Original-Pair }
        }
    } finally { $env:RESKATE_HOM_NO_PAUSE = $previousNoPause }
    Write-Host 'PASS: Install.bat and Uninstall.bat work with spaces in the path.'
} finally {
    $resolved = [IO.Path]::GetFullPath($fixture)
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repo 'build')).TrimEnd('\') + '\'
    if (-not $resolved.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to clean a fixture outside the workspace build folder.'
    }
    Remove-Item -LiteralPath $resolved -Recurse -Force
}
