[CmdletBinding()]
param(
    [ValidatePattern('^[0-9]+\.[0-9]+\.[0-9]+$')][string]$Version = '0.1.0',
    [string]$BuildDirectory = 'build/vs2022-x64',
    [string]$OutputDirectory = 'artifacts',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $repo
$build = [IO.Path]::GetFullPath((Join-Path $repo $BuildDirectory))
$output = [IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
$templates = Join-Path $repo 'packaging/HallOfMeat'
$tests = @('dingosdk_slam_tests', 'dingosdk_slam_visuals_tests', 'dingosdk_slam_replay_tests', 'dingosdk_slam_settings_tests',
    'dingosdk_slam_bail_tests', 'dingosdk_slam_retry_tests')
if (-not $SkipBuild) {
    & cmake --preset vs2022-x64 -B $build "-DDINGOSDK_VERSION=$Version-hall-of-meat" `
        '-DDINGOSDK_BUILD_SLAM_TESTS=ON' '-DDINGOSDK_LAUNCHER_AUTO_UPDATE=OFF' '-DDINGOSDK_BACKTRACE_URL:STRING='
    if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed.' }
    & cmake --build $build --config Release --target dingosdk_runtime @tests --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Hall of Meat build failed.' }
}
$cache = Get-Content -LiteralPath (Join-Path $build 'CMakeCache.txt') -Raw
foreach ($required in @('DINGOSDK_LAUNCHER_AUTO_UPDATE:BOOL=OFF', 'DINGOSDK_BACKTRACE_URL:STRING=',
    "DINGOSDK_VERSION:STRING=$Version-hall-of-meat", 'DINGOSDK_BUILD_SLAM_TESTS:BOOL=ON')) {
    if ($cache -notmatch ('(?m)^' + [regex]::Escape($required) + '\r?$')) {
        throw "The package requires $required. Run without -SkipBuild to configure it."
    }
}
& ctest --test-dir $build -C Release -R '^slam_' --output-on-failure --no-tests=error
if ($LASTEXITCODE -ne 0) { throw 'Slam regression tests failed.' }

Add-Type -AssemblyName System.IO.Compression.FileSystem
Add-Type -AssemblyName System.IO.Compression
Add-Type -AssemblyName System.Drawing
New-Item -ItemType Directory -Path $output -Force | Out-Null
$packageName = "ReSkate_HallOfMeat-$Version"
$stage = Join-Path $output ($packageName + '-' + [Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $stage | Out-Null
try {
    foreach ($file in Get-ChildItem -LiteralPath $templates -File) {
        Copy-Item -LiteralPath $file.FullName -Destination $stage
    }
    $manifest = Get-Content -LiteralPath (Join-Path $stage 'manifest.json') -Raw | ConvertFrom-Json
    $manifest.version_number = $Version
    $utf8 = New-Object Text.UTF8Encoding($false)
    [IO.File]::WriteAllText((Join-Path $stage 'manifest.json'), ($manifest | ConvertTo-Json -Depth 5), $utf8)
    foreach ($doc in @('README.md', 'INSTALL.txt', 'CHANGELOG.md')) {
        $path = Join-Path $stage $doc
        [IO.File]::WriteAllText($path, (Get-Content -LiteralPath $path -Raw -Encoding UTF8).Replace('0.1.0', $Version), $utf8)
    }
    $binaries = [ordered]@{}
    foreach ($name in @('ReSkate.dll', 'ReSkateLauncher.exe')) {
        $path = Join-Path $build "Release/$name"
        Copy-Item -LiteralPath $path -Destination $stage
        $binaries[$name] = [ordered]@{
            sha256 = (Get-FileHash -LiteralPath $path -Algorithm SHA256).Hash.ToLowerInvariant()
            bytes = (Get-Item -LiteralPath $path).Length
        }
    }
    # A procedural vector-style badge; no copied game artwork or trainer branding.
    $bitmap = New-Object Drawing.Bitmap(256, 256)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    $pen = New-Object Drawing.Pen([Drawing.Color]::Ivory, 10)
    $brush = New-Object Drawing.SolidBrush([Drawing.Color]::Ivory)
    $font = New-Object Drawing.Font('Arial', 32, [Drawing.FontStyle]::Bold)
    try {
        $graphics.SmoothingMode = [Drawing.Drawing2D.SmoothingMode]::AntiAlias
        $graphics.Clear([Drawing.Color]::FromArgb(28, 30, 33))
        $graphics.FillRectangle([Drawing.Brushes]::Firebrick, 0, 198, 256, 58)
        $graphics.DrawEllipse($pen, 98, 24, 60, 54)
        $graphics.DrawLine($pen, 128, 78, 128, 132)
        $graphics.DrawLine($pen, 83, 96, 173, 96)
        $graphics.DrawLine($pen, 128, 132, 92, 181)
        $graphics.DrawLine($pen, 128, 132, 164, 181)
        $graphics.DrawLine($pen, 83, 96, 68, 136)
        $graphics.DrawLine($pen, 173, 96, 188, 136)
        $graphics.DrawString('HOM', $font, $brush, 66, 202)
        $bitmap.Save((Join-Path $stage 'icon.png'), [Drawing.Imaging.ImageFormat]::Png)
    } finally { $font.Dispose(); $brush.Dispose(); $pen.Dispose(); $graphics.Dispose(); $bitmap.Dispose() }

    $revision = 'unknown'
    if (Test-Path -LiteralPath (Join-Path $repo '.git')) {
        $revision = & git rev-parse HEAD
        if ($LASTEXITCODE -ne 0) { throw 'git rev-parse failed.' }
    }
    $identity = Get-Content -LiteralPath (Join-Path $repo 'Engine/Game/Build/supported_build.h') -Raw
    $gameHash = [regex]::Match($identity, 'game_sha256\s*=\s*"([a-f0-9]{64})"').Groups[1].Value
    $gameBuild = [regex]::Match($identity, 'steam_build_id\s*=\s*"([0-9]+)"').Groups[1].Value
    if (-not $gameHash -or -not $gameBuild) { throw 'Could not read the supported game identity.' }
    $info = [ordered]@{
        schema = 1; name = 'ReSkate_HallOfMeat'; version = $Version
        source_revision = $revision
        supported_game = @{ build_id = $gameBuild; sha256 = $gameHash }
        build = @{ configuration = 'Release'; architecture = 'x64'; binary_updates = $false; crash_uploads = $false }
        binaries = $binaries
    }
    [IO.File]::WriteAllText((Join-Path $stage 'build-info.json'), ($info | ConvertTo-Json -Depth 6), $utf8)
    $zipPath = Join-Path $output "$packageName.zip"
    $pendingZip = Join-Path $output ($packageName + '-' + [Guid]::NewGuid().ToString('N') + '.zip')
    $archive = [IO.Compression.ZipFile]::Open($pendingZip, [IO.Compression.ZipArchiveMode]::Create)
    try {
        foreach ($file in Get-ChildItem -LiteralPath $stage -Recurse -File) {
            $relative = $file.FullName.Substring($stage.Length + 1).Replace('\', '/')
            [IO.Compression.ZipFileExtensions]::CreateEntryFromFile($archive, $file.FullName, $relative,
                [IO.Compression.CompressionLevel]::Optimal) | Out-Null
        }
    } finally { $archive.Dispose() }
    Move-Item -LiteralPath $pendingZip -Destination $zipPath -Force
    $hash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
    [IO.File]::WriteAllText(($zipPath + '.sha256'), "$hash  $packageName.zip`n", $utf8)
    Write-Host "Package: $zipPath"
    Write-Host "SHA-256: $hash"
} finally {
    $resolvedStage = [IO.Path]::GetFullPath($stage)
    if (-not $resolvedStage.StartsWith($output.TrimEnd('\') + '\', [StringComparison]::OrdinalIgnoreCase)) {
        throw 'Refusing to clean staging outside the output directory.'
    }
    Remove-Item -LiteralPath $resolvedStage -Recurse -Force
}
