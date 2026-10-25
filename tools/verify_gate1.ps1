# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Gate-1 re-acceptance harness.  Runs the static and functional checks from the
# rework order on a machine with an NVIDIA driver:
#
#   1. rtx_tests.exe without any environment variable override
#   2. shim probe (device, NVDEC/NVENC components, capabilities)
#   3. raw NV12 -> P010 artefact path (NVENC encodes, NVDEC decodes its output)
#   4. container transcode with the zero-copy assertion (optional, needs FFmpeg)
#
# Example:
#   pwsh tools/verify_gate1.ps1 -BuildDir build/cmake/zig-windows
#   pwsh tools/verify_gate1.ps1 -BuildDir build/cmake/zig-windows `
#        -Container sample.mp4 -Codec hevc -BitDepth 10
#
# Exit code 0 means every executed check passed.

[CmdletBinding()]
param(
    [string]$BuildDir = 'build/cmake/zig-windows',
    [string]$Container,
    [string]$RawInput,
    [int]$Width = 640,
    [int]$Height = 480,
    [int]$Frames = 8,
    [string]$Codec = 'hevc',
    [int]$BitDepth = 8,
    [switch]$SkipTests,
    [switch]$KeepArtifacts
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

if (-not [IO.Path]::IsPathRooted($BuildDir)) {
    $BuildDir = Join-Path $repo $BuildDir
}
$BuildDir = (Resolve-Path -LiteralPath $BuildDir).Path

$transcode = Join-Path $BuildDir 'rtx-transcode.exe'
if (-not (Test-Path -LiteralPath $transcode)) {
    $transcode = Join-Path $BuildDir 'rtx-transcode'
}
$tests = Join-Path $BuildDir 'rtx_tests.exe'
if (-not (Test-Path -LiteralPath $tests)) {
    $tests = Join-Path $BuildDir 'rtx_tests'
}

$results = [System.Collections.Generic.List[object]]::new()

function Add-Result([string]$Name, [bool]$Pass, [string]$Detail) {
    $script:results.Add([pscustomobject]@{ Check = $Name; Pass = $Pass; Detail = $Detail })
    $tag = if ($Pass) { 'PASS' } else { 'FAIL' }
    Write-Host ("  [{0}] {1} - {2}" -f $tag, $Name, $Detail)
}

function New-Nv12File([string]$Path, [int]$W, [int]$H, [int]$Count) {
    # Moving vertical gradient with a horizontal phase shift; chroma is neutral.
    $luma = [byte[]]::new($W * $H)
    $chroma = [byte[]]::new(($W * $H) / 2)
    for ($i = 0; $i -lt $chroma.Length; $i++) { $chroma[$i] = 128 }
    $stream = [IO.File]::Create($Path)
    try {
        for ($f = 0; $f -lt $Count; $f++) {
            for ($y = 0; $y -lt $H; $y++) {
                $base = ($y * $W)
                $row = [byte](($y + ($f * 4)) % 256)
                for ($x = 0; $x -lt $W; $x++) {
                    $luma[$base + $x] = [byte](($row + ($x -shr 3)) % 256)
                }
            }
            $stream.Write($luma, 0, $luma.Length)
            $stream.Write($chroma, 0, $chroma.Length)
        }
    } finally {
        $stream.Dispose()
    }
}

Write-Host 'Gate 1 re-acceptance'
Write-Host ("  build : {0}" -f $BuildDir)

# ---------------------------------------------------------------------------
# 1. Unit suite without environment overrides
# ---------------------------------------------------------------------------
if (-not $SkipTests) {
    if (-not (Test-Path -LiteralPath $tests)) {
        Add-Result 'unit-tests' $false "rtx_tests not found in $BuildDir"
    } else {
        $savedShim = $env:RTX_TEST_SHIM_PATH
        $savedNgx = $env:RTX_TEST_NGX_PATH
        Remove-Item Env:RTX_TEST_SHIM_PATH -ErrorAction SilentlyContinue
        Remove-Item Env:RTX_TEST_NGX_PATH -ErrorAction SilentlyContinue
        try {
            $output = & $tests 2>&1 | Out-String
            $exit = $LASTEXITCODE
        } finally {
            if ($savedShim) { $env:RTX_TEST_SHIM_PATH = $savedShim }
            if ($savedNgx) { $env:RTX_TEST_NGX_PATH = $savedNgx }
        }
        $summary = ($output -split "`n" | Where-Object { $_ -match 'test\(s\) run' } | Select-Object -Last 1)
        $passed = ($exit -eq 0) -and ($summary -match '0 failed')
        Add-Result 'unit-tests' $passed ($summary.Trim())
    }
} else {
    Write-Host '  [SKIP] unit-tests'
}

# ---------------------------------------------------------------------------
# 2. Probe
# ---------------------------------------------------------------------------
$probe = & $transcode --probe --json 2>&1 | Out-String
$probeExit = $LASTEXITCODE
$probeOk = $false
$deviceName = ''
$caps = 0
try {
    $doc = $probe | ConvertFrom-Json
    $deviceName = $doc.diagnostics.device.name
    $caps = [int]$doc.diagnostics.device.capabilities
    $probeOk = ($probeExit -eq 0) -and ($deviceName -ne '')
} catch {
    $probeOk = $false
}
$capNames = @()
if ($caps -band 1) { $capNames += 'nvdec' }
if ($caps -band 2) { $capNames += 'nvenc-hevc10' }
if ($caps -band 4) { $capNames += 'nvenc-av1' }
if ($caps -band 8) { $capNames += 'vsr' }
if ($caps -band 16) { $capNames += 'truehdr' }
Add-Result 'probe' $probeOk ("{0}; caps={1}" -f $deviceName, ($capNames -join ','))

# ---------------------------------------------------------------------------
# 3. Raw NV12 -> P010 artefact path
# ---------------------------------------------------------------------------
$work = Join-Path $BuildDir 'gate1'
if (-not (Test-Path -LiteralPath $work)) {
    New-Item -ItemType Directory -Path $work | Out-Null
}
$rawPath = $RawInput
if (-not $rawPath) {
    $rawPath = Join-Path $work 'gate1.nv12'
    Write-Host ("  generating {0}x{1} x {2} NV12 ..." -f $Width, $Height, $Frames)
    New-Nv12File $rawPath $Width $Height $Frames
}
$p010Path = Join-Path $work 'gate1.p010'
if (Test-Path -LiteralPath $p010Path) { Remove-Item -LiteralPath $p010Path -Force }

$rawArgs = @('--input-raw', $rawPath, '--width', $Width, '--height', $Height,
             '--output-raw', $p010Path, '--backend', 'nvidia',
             '--frames', $Frames, '--codec', $Codec, '--bit-depth', $BitDepth, '--json')
$rawOut = & $transcode @rawArgs 2>&1 | Out-String
$rawExit = $LASTEXITCODE
$rawOk = ($rawExit -eq 0) -and (Test-Path -LiteralPath $p010Path)
$rawDetail = "exit=$rawExit"
if ($rawOk) {
    $expected = [int64]$Width * $Height * 3 * $Frames
    $actual = (Get-Item -LiteralPath $p010Path).Length
    $rawOk = ($actual -eq $expected)
    $rawDetail = "exit=0, {0} bytes (expected {1})" -f $actual, $expected
} else {
    $rawDetail = "exit=$rawExit; " + (($rawOut -split "`n" | Select-Object -Last 3) -join ' ')
}
Add-Result 'raw-nv12-to-p010' $rawOk $rawDetail

# ---------------------------------------------------------------------------
# 4. Container transcode with the zero-copy assertion
# ---------------------------------------------------------------------------
if ($Container) {
    $outMp4 = Join-Path $work 'gate1-out.mp4'
    if (Test-Path -LiteralPath $outMp4) { Remove-Item -LiteralPath $outMp4 -Force }
    $args = @('--input', $Container, '--output', $outMp4, '--backend', 'nvidia',
              '--codec', $Codec, '--bit-depth', $BitDepth, '--json')
    $out = & $transcode @args 2>&1 | Out-String
    $exit = $LASTEXITCODE
    if ($exit -ne 0) {
        Add-Result 'container-zero-copy' $false ("exit=$exit; " + (($out -split "`n" | Select-Object -Last 3) -join ' '))
    } else {
        $report = $out | ConvertFrom-Json
        $zeroCopy = [bool]$report.zero_copy_main_loop
        $d2h = [int64]$report.decoder_device_to_host_bytes
        $h2d = [int64]$report.encoder_host_to_device_bytes
        $detail = "frames={0}/{1}, D2H={2}, H2D={3}, zero_copy={4}" -f `
            $report.frames_decoded, $report.frames_encoded, $d2h, $h2d, $zeroCopy
        Add-Result 'container-zero-copy' ($zeroCopy -and $d2h -eq 0 -and $h2d -eq 0) $detail
    }
} else {
    Write-Host '  [SKIP] container-zero-copy (pass -Container <file.mp4>)'
}

# ---------------------------------------------------------------------------
# Summary
# ---------------------------------------------------------------------------
if (-not $KeepArtifacts) {
    Get-ChildItem -LiteralPath $work -Filter 'gate1*' -ErrorAction SilentlyContinue |
        Remove-Item -Force -ErrorAction SilentlyContinue
}

$failed = @($results | Where-Object { -not $_.Pass })
Write-Host ''
Write-Host ("{0}/{1} checks passed" -f ($results.Count - $failed.Count), $results.Count)
if ($failed.Count -gt 0) {
    exit 1
}
exit 0
