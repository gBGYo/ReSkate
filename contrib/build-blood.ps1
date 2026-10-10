[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)][string]$GameDirectory,
    [Parameter(Mandatory = $true)][string]$StudioCli,
    [string]$BuildDirectory = 'build/vs2022-x64',
    [string]$OutputDirectory = 'artifacts/Blood',
    [switch]$SkipBuild
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path $PSScriptRoot -Parent
Set-Location -LiteralPath $repo
$game = (Resolve-Path -LiteralPath $GameDirectory).Path
$compiler = (Resolve-Path -LiteralPath $StudioCli).Path
$build = [IO.Path]::GetFullPath((Join-Path $repo $BuildDirectory))
$output = [IO.Path]::GetFullPath((Join-Path $repo $OutputDirectory))
$identity = Get-Content -LiteralPath (Join-Path $repo 'Engine/Game/Build/supported_build.h') -Raw
$expected = [regex]::Match($identity, 'game_sha256\s*=\s*"([a-f0-9]{64})"').Groups[1].Value
if (-not $expected -or (Get-FileHash -LiteralPath (Join-Path $game 'Skate.exe') -Algorithm SHA256).Hash -ne $expected) {
    throw 'Blood assets require the supported Skate build specified in supported_build.h.'
}
if (-not $SkipBuild) {
    & cmake --build $build --config Release --target dingosdk_blood_asset_author --parallel 4
    if ($LASTEXITCODE -ne 0) { throw 'Blood asset author build failed. Configure with DINGOSDK_BUILD_BLOOD_TESTS=ON first.' }
}
$authored = Join-Path $output 'authored'
$fbmod = Join-Path $output 'blood.fbmod'
$staging = Join-Path $output 'compiled'
$patch = Join-Path $staging 'Patch'
& (Join-Path $build 'Release/dingosdk_blood_asset_author.exe') $game $authored
if ($LASTEXITCODE -ne 0) { throw 'Blood asset author failed.' }
& python (Join-Path $repo 'Extension/Blood/Tools/package_blood.py') $authored $fbmod
if ($LASTEXITCODE -ne 0) { throw 'Blood FBMOD packaging failed.' }
& $compiler compile-mod $game $staging $fbmod
if ($LASTEXITCODE -ne 0) { throw 'ReSkate Studio blood compilation failed.' }
& python (Join-Path $repo 'Extension/Blood/Tools/package_blood.py') $authored $fbmod --mod-root $patch
if ($LASTEXITCODE -ne 0) { throw 'Blood shared-bundle declaration failed.' }
& (Join-Path $build 'Release/dingosdk_blood_asset_author.exe') $game $patch --verify-merge
if ($LASTEXITCODE -ne 0) { throw 'Blood assets did not merge with the target game and its installed mods.' }
$published = Join-Path $output 'Patch'
New-Item -ItemType Directory -Path $published -Force | Out-Null
foreach ($file in Get-ChildItem -LiteralPath $patch -Force) {
    Copy-Item -LiteralPath $file.FullName -Destination $published -Recurse -Force
}
Write-Host "Blood mod: $published"
Write-Host 'Copy the Patch folder to Mods/ReSkate_Blood beside the game executable and enable Blood in Skater > Blood.'
