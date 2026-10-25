# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Configure, build, and test the engine with one of the repository presets.

[CmdletBinding()]
param(
    [ValidateSet('zig-windows', 'zig-linux', 'linux-release', 'windows-msvc')]
    [string]$Preset = 'zig-windows',

    [string]$BuildDir,
    [string]$CMake,
    [string]$Ninja,
    [string]$Zig,

    [switch]$Clean,
    [switch]$SkipTests,
    [switch]$WarningsAsErrors
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

function Find-Tool {
    param(
        [string]$Explicit,
        [string]$Command,
        [string]$EnvironmentName
    )

    if ($Explicit) {
        if (-not (Test-Path -LiteralPath $Explicit)) {
            throw "tool not found: $Explicit"
        }
        return (Resolve-Path -LiteralPath $Explicit).Path
    }

    if ($EnvironmentName) {
        $candidate = [Environment]::GetEnvironmentVariable($EnvironmentName)
        if ($candidate -and (Test-Path -LiteralPath $candidate)) {
            return (Resolve-Path -LiteralPath $candidate).Path
        }
    }

    $found = Get-Command $Command -ErrorAction SilentlyContinue
    if ($found) {
        if ($found.Source) { return $found.Source }
        if ($found.Path) { return $found.Path }
    }
    return $null
}

$cmakeExe = Find-Tool -Explicit $CMake -Command 'cmake' -EnvironmentName 'RTX_CMAKE'
if (-not $cmakeExe) {
    throw 'CMake was not found. Pass -CMake <path> or set RTX_CMAKE.'
}

$ninjaExe = Find-Tool -Explicit $Ninja -Command 'ninja' -EnvironmentName 'RTX_NINJA'
if ($ninjaExe) {
    $env:PATH = (Split-Path -Parent $ninjaExe) + [IO.Path]::PathSeparator + $env:PATH
} elseif ($Preset -ne 'windows-msvc') {
    throw 'Ninja was not found. Pass -Ninja <path> or set RTX_NINJA.'
}

$zigExe = Find-Tool -Explicit $Zig -Command 'zig' -EnvironmentName 'RTX_ZIG'
if ($Preset -like 'zig-*') {
    if (-not $zigExe) {
        throw 'Zig was not found. Pass -Zig <path> or set RTX_ZIG.'
    }
    $env:RTX_ZIG = $zigExe
}

$binaryDir = if ($BuildDir) {
    if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repo $BuildDir }
} else {
    Join-Path $repo "build/cmake/$Preset"
}

if ($Clean) {
    $buildRoot = [IO.Path]::GetFullPath((Join-Path $repo 'build'))
    $resolved = [IO.Path]::GetFullPath($binaryDir)
    if (-not $resolved.StartsWith($buildRoot, [StringComparison]::OrdinalIgnoreCase)) {
        throw "refusing to clean a directory outside build/: $resolved"
    }
    if (Test-Path -LiteralPath $resolved) {
        Write-Host "==> removing $resolved"
        Remove-Item -LiteralPath $resolved -Recurse -Force
    }
}

Write-Host "==> toolchain"
Write-Host "    cmake : $cmakeExe"
if ($ninjaExe) { Write-Host "    ninja : $ninjaExe" }
if ($zigExe) { Write-Host "    zig   : $zigExe" }
Write-Host "    build : $binaryDir"

$configure = @('--preset', $Preset, '-B', $binaryDir)
if ($WarningsAsErrors) {
    $configure += '-DRTX_WARNINGS_AS_ERRORS=ON'
}

Write-Host "==> cmake $($configure -join ' ')"
& $cmakeExe @configure
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

$build = @('--build', $binaryDir)
if ($Preset -eq 'windows-msvc') {
    $build += @('--config', 'Release')
}

Write-Host "==> cmake $($build -join ' ')"
& $cmakeExe @build
if ($LASTEXITCODE -ne 0) {
    exit $LASTEXITCODE
}

if (-not $SkipTests) {
    $ctestExe = Join-Path (Split-Path -Parent $cmakeExe) 'ctest.exe'
    if (-not (Test-Path -LiteralPath $ctestExe)) {
        $ctestExe = Join-Path (Split-Path -Parent $cmakeExe) 'ctest'
    }
    if (-not (Test-Path -LiteralPath $ctestExe)) {
        $ctestExe = Find-Tool -Explicit $null -Command 'ctest'
    }
    if (-not $ctestExe) {
        throw 'ctest was not found next to cmake or on PATH.'
    }

    $test = @('--test-dir', $binaryDir, '--output-on-failure')
    if ($Preset -eq 'windows-msvc') {
        $test += @('-C', 'Release')
    }

    Write-Host "==> ctest $($test -join ' ')"
    & $ctestExe @test
    if ($LASTEXITCODE -ne 0) {
        exit $LASTEXITCODE
    }
}

Write-Host "==> done"
