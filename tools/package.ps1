# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Builds a release archive, verifies the compliance gates, and writes SHA-256
# manifests. File ordering and hashes are stable; the archive timestamp and
# manifest generation time are intentionally recorded, not normalized.

[CmdletBinding()]
param(
    [string]$BuildDir,
    [string]$OutputDir,
    [string]$Version,
    [switch]$SkipAudit,
    [switch]$KeepStaging
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

if (-not $OutputDir) {
    $OutputDir = Join-Path $repo 'out'
}

if (-not $Version) {
    $cmakeText = Get-Content -LiteralPath (Join-Path $repo 'CMakeLists.txt') -Raw
    if ($cmakeText -match 'project\s*\(\s*CineRTX\s+VERSION\s+([0-9]+\.[0-9]+\.[0-9]+)') {
        $Version = $Matches[1]
    } else {
        throw 'cannot determine the project version from CMakeLists.txt'
    }
}

$osName = if ($env:OS -eq 'Windows_NT') {
    'windows'
} elseif ($PSVersionTable.PSVersion.Major -ge 6 -and $IsMacOS) {
    'macos'
} else {
    'linux'
}
$arch = [Runtime.InteropServices.RuntimeInformation]::OSArchitecture.ToString().ToLowerInvariant()
$platform = "$osName-$arch"
$packageName = "CineRTX-$Version-$platform"

if (-not $BuildDir) {
    $candidates = @(
        "build/RelWithDebInfo",
        "build/cmake/zig-windows",
        "build/cmake/windows-msvc/Release",
        "build/cmake/linux-release",
        "build/ci",
        "build"
    )
    foreach ($candidate in $candidates) {
        $path = Join-Path $repo $candidate
        if (Test-Path -LiteralPath $path) {
            $BuildDir = $path
            break
        }
    }
}
if (-not $BuildDir -or -not (Test-Path -LiteralPath $BuildDir)) {
    throw 'no build directory found; pass -BuildDir'
}
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

function Find-Artifact {
    param([string]$Directory, [string[]]$Names)
    foreach ($name in $Names) {
        $candidate = Join-Path $Directory $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $null
}

# The deliverable is one self-contained executable.  The shim, the NGX bridge,
# the FRUC bridge and the media container layer are static archives linked into
# it, so there is nothing else to collect -- and shipping a sidecar DLL would be
# a Gate 3.0 closure failure, not a packaging convenience.
$exe = Find-Artifact $BuildDir @('CineRTX.exe', 'CineRTX')

$requiredItems = @(
    @{ Name = 'CineRTX'; Path = $exe }
)

foreach ($item in $requiredItems) {
    if (-not $item.Path) {
        throw "missing artifact: $($item.Name) under $BuildDir"
    }
}

if (-not $SkipAudit) {
    Write-Host '==> compliance audit'
    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'check_spdx.ps1')
    if ($LASTEXITCODE -ne 0) {
        throw 'SPDX audit failed; refusing to package'
    }

    & pwsh -NoProfile -File (Join-Path $PSScriptRoot 'audit_imports.ps1') -Binary $exe
    if ($LASTEXITCODE -ne 0) {
        throw 'import-table audit failed; refusing to package'
    }
}

$stage = Join-Path $OutputDir $packageName
if (Test-Path -LiteralPath $stage) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}
New-Item -ItemType Directory -Path $stage -Force | Out-Null

Copy-Item -LiteralPath $exe -Destination $stage

foreach ($name in @('README.md', 'LICENSE', 'LICENSE-MIT', 'LICENSE-APACHE', 'COPYING.EXCEPTION')) {
    $source = Join-Path $repo $name
    if (-not (Test-Path -LiteralPath $source)) {
        throw "missing release file: $name"
    }
    Copy-Item -LiteralPath $source -Destination $stage
}

$docs = Join-Path $repo 'docs'
if (Test-Path -LiteralPath $docs) {
    Copy-Item -LiteralPath $docs -Destination $stage -Recurse
}

$headers = Join-Path $repo 'include'
if (Test-Path -LiteralPath $headers) {
    Copy-Item -LiteralPath $headers -Destination $stage -Recurse
}

$files = Get-ChildItem -LiteralPath $stage -Recurse -File | Sort-Object FullName
$entries = foreach ($file in $files) {
    [pscustomobject]@{
        path = [IO.Path]::GetRelativePath($stage, $file.FullName).Replace('\', '/')
        size = $file.Length
        sha256 = (Get-FileHash -LiteralPath $file.FullName -Algorithm SHA256).Hash.ToLowerInvariant()
    }
}

$manifest = [ordered]@{
    name = $packageName
    version = $Version
    platform = $platform
    generated_utc = [DateTime]::UtcNow.ToString('yyyy-MM-ddTHH:mm:ssZ')
    files = @($entries)
}
$manifestPath = Join-Path $stage 'manifest.json'
$manifest | ConvertTo-Json -Depth 5 | Set-Content -LiteralPath $manifestPath -Encoding utf8

$sums = foreach ($entry in $entries) {
    "$($entry.sha256)  $($entry.path)"
}
$sumsPath = Join-Path $stage 'SHA256SUMS.txt'
$sums | Set-Content -LiteralPath $sumsPath -Encoding utf8

New-Item -ItemType Directory -Path $OutputDir -Force | Out-Null
$zipPath = Join-Path $OutputDir "$packageName.zip"
if (Test-Path -LiteralPath $zipPath) {
    Remove-Item -LiteralPath $zipPath -Force
}
Compress-Archive -Path (Join-Path $stage '*') -DestinationPath $zipPath -CompressionLevel Optimal

$zipHash = (Get-FileHash -LiteralPath $zipPath -Algorithm SHA256).Hash.ToLowerInvariant()
"$zipHash  $(Split-Path -Leaf $zipPath)" | Set-Content -LiteralPath "$zipPath.sha256" -Encoding utf8
Copy-Item -LiteralPath $manifestPath -Destination (Join-Path $OutputDir "$packageName.manifest.json") -Force

if (-not $KeepStaging) {
    Remove-Item -LiteralPath $stage -Recurse -Force
}

Write-Host ''
Write-Host "package : $zipPath"
Write-Host "sha256  : $zipHash"
Write-Host "files   : $($entries.Count)"
