[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Install', 'Uninstall')][string]$Action,
    [string]$GameDirectory
)

$ErrorActionPreference = 'Stop'
$names = @('ReSkate.dll', 'ReSkateLauncher.exe')
$backup = Join-Path $PSScriptRoot 'backup'
$recordPath = Join-Path $backup 'installation.json'
$transaction = $null

function Get-Hash([string]$Path) {
    if (-not (Test-Path -LiteralPath $Path -PathType Leaf)) { throw "Missing file: $Path" }
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()
}

function Remove-Transaction {
    if ($transaction -and (Test-Path -LiteralPath $transaction)) {
        $resolved = [IO.Path]::GetFullPath($transaction)
        $parent = [IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\') + '\'
        if (-not $resolved.StartsWith($parent, [StringComparison]::OrdinalIgnoreCase)) {
            throw 'Refusing to clean a transaction outside the mod folder.'
        }
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

try {
    # Windows PowerShell can evaluate parameter defaults before PSScriptRoot is
    # available. Resolve the automatic location after parameter binding instead.
    if ([string]::IsNullOrWhiteSpace($GameDirectory)) {
        $candidate = [IO.DirectoryInfo]$PSScriptRoot
        while ($candidate -and -not (Test-Path -LiteralPath (Join-Path $candidate.FullName 'Skate.exe') -PathType Leaf)) {
            $candidate = $candidate.Parent
        }
        if (-not $candidate) { throw 'Import this ZIP with the ReSkate launcher first, or pass -GameDirectory.' }
        $GameDirectory = $candidate.FullName
    }
    $GameDirectory = (Resolve-Path -LiteralPath $GameDirectory).Path
    if (-not (Test-Path -LiteralPath (Join-Path $GameDirectory 'Skate.exe') -PathType Leaf)) {
        throw 'Place the mod in Skate\Mods\ReSkate_Blood, or pass -GameDirectory with your Skate folder.'
    }
    foreach ($processName in @('Skate', 'ReSkateLauncher')) {
        if (Get-Process -Name $processName -ErrorAction SilentlyContinue) {
            throw "Close $processName before running $Action."
        }
    }
    $info = Get-Content -LiteralPath (Join-Path $PSScriptRoot 'build-info.json') -Raw | ConvertFrom-Json
    $current = @{}
    foreach ($name in $names) { $current[$name] = Get-Hash (Join-Path $GameDirectory $name) }

    $record = $null
    if (Test-Path -LiteralPath $recordPath -PathType Leaf) {
        $record = Get-Content -LiteralPath $recordPath -Raw | ConvertFrom-Json
        if ($record.game_directory -ne $GameDirectory) { throw 'This backup belongs to another game folder.' }
        foreach ($name in $names) {
            if ((Get-Hash (Join-Path $backup $name)) -ne $record.original.$name) {
                throw "The backup for $name is damaged. No game files were changed."
            }
            if ($current[$name] -ne $record.installed.$name -and $current[$name] -ne $record.original.$name) {
                throw "$name has changed since installation. Restore the other runtime build first; its files will not be overwritten."
            }
        }
    } elseif (Test-Path -LiteralPath $backup) {
        throw 'The backup folder is incomplete. Preserve it and recover the original files before installing again.'
    }

    if ($Action -eq 'Install') {
        if ((Get-Hash (Join-Path $GameDirectory 'Skate.exe')) -ne $info.supported_game.sha256) {
            throw "Unsupported Skate.exe. This package requires Steam build $($info.supported_game.build_id)."
        }
        foreach ($name in $names) {
            if ((Get-Hash (Join-Path $PSScriptRoot $name)) -ne $info.binaries.$name.sha256) {
                throw "The packaged $name is missing or damaged. Extract a fresh copy of the ZIP."
            }
        }
        if ($info.PSObject.Properties['assets']) {
            foreach ($asset in $info.assets.PSObject.Properties) {
                $assetPath = [IO.Path]::GetFullPath((Join-Path $PSScriptRoot $asset.Name))
                if (-not $assetPath.StartsWith([IO.Path]::GetFullPath($PSScriptRoot).TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase) -or
                    (Get-Hash $assetPath) -ne $asset.Value) {
                    throw "The packaged blood asset $($asset.Name) is missing or damaged. Extract a fresh copy of the ZIP."
                }
            }
        }
    } elseif (-not $record) {
        throw 'There is no complete installation backup to restore.'
    }

    # Preserve the pair immediately before this operation for rollback, separately
    # from the original backup, which survives repeated installs.
    $transaction = Join-Path $PSScriptRoot ('.transaction-' + [Guid]::NewGuid().ToString('N'))
    New-Item -ItemType Directory -Path $transaction | Out-Null
    foreach ($name in $names) {
        Copy-Item -LiteralPath (Join-Path $GameDirectory $name) -Destination (Join-Path $transaction $name)
    }
    if ($Action -eq 'Install') {
        $nextRecord = [ordered]@{
            schema = 1
            game_directory = $GameDirectory
            original = if ($record) { $record.original } else { $current }
            installed = @{}
        }
        foreach ($name in $names) { $nextRecord.installed[$name] = $info.binaries.$name.sha256 }
        $nextRecord | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath (Join-Path $transaction 'installation.json') -Encoding UTF8
        if (-not $record) {
            $newBackup = Join-Path $transaction 'backup'
            New-Item -ItemType Directory -Path $newBackup | Out-Null
            foreach ($name in $names) {
                Copy-Item -LiteralPath (Join-Path $transaction $name) -Destination (Join-Path $newBackup $name)
            }
            Copy-Item -LiteralPath (Join-Path $transaction 'installation.json') -Destination (Join-Path $newBackup 'installation.json')
            Move-Item -LiteralPath $newBackup -Destination $backup
        }
    }

    $replaced = @()
    try {
        foreach ($name in $names) {
            $source = if ($Action -eq 'Install') { Join-Path $PSScriptRoot $name } else { Join-Path $backup $name }
            $target = Join-Path $GameDirectory $name
            # Mark before copying: a failed copy may have partially changed the file.
            $replaced += $name
            Copy-Item -LiteralPath $source -Destination $target -Force
            if ((Get-Hash $target) -ne (Get-Hash $source)) { throw "Verification failed for $name." }
        }
        if ($Action -eq 'Install') {
            Copy-Item -LiteralPath (Join-Path $transaction 'installation.json') -Destination $recordPath -Force
        }
    } catch {
        $failure = $_
        $rollbackErrors = @()
        foreach ($name in $replaced) {
            try {
                if ((Get-Hash (Join-Path $GameDirectory $name)) -ne $current[$name]) {
                    Copy-Item -LiteralPath (Join-Path $transaction $name) -Destination (Join-Path $GameDirectory $name) -Force
                }
            } catch { $rollbackErrors += $_.Exception.Message }
        }
        if ($rollbackErrors.Count) {
            throw "Operation failed: $failure. Rollback needs manual recovery from $transaction : $($rollbackErrors -join '; ')"
        }
        Remove-Transaction
        throw "Operation failed; previous game files were restored. $failure"
    }
    Remove-Transaction
    if ($Action -eq 'Install') {
        Write-Host 'Installed Blood. Start ReSkateLauncher.exe, press Insert, then open Skater > Blood.'
        Write-Host "Original files are saved in $backup. Run Uninstall.bat to restore them."
    } else {
        Write-Host 'Your previous ReSkate DLL and launcher are restored. You may now remove this mod folder.'
    }
    exit 0
} catch {
    Write-Host "ERROR: $($_.Exception.Message)" -ForegroundColor Red
    # A transaction is retained on an unrecoverable rollback for manual recovery.
    exit 1
}
