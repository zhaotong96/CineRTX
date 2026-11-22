# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# gate_r8_desktop_check.ps1 - Gate R-8 desktop verification (Phase 11.2-R).
#
# Run INSIDE an interactive desktop session (WTS Session ID > 0) on the RTX
# host AFTER the driver has been upgraded to >= 621.24 WHQL.  Collects the
# Gate R-8 evidence into ~\test\evidence\gate_r8\.
#
# Usage:
#   powershell -ExecutionPolicy Bypass -File ~\test\gate_r8_desktop_check.ps1 ^
#       -Media "C:\path\to\sample.mkv"
#
# Manual操作项 (the script pauses and prompts between them):
#   1. H.264/HEVC/AV1 playback + transcode monitor: presented > 0, D2H == 0 B
#   2. 100x rapid viewport resize: no crash, no leak
#   3. HDR/SDR cross-monitor switch: no colour crush
#   4. Audio device hot-unplug/replug: AUDCLNT_E_DEVICE_INVALIDATED ->
#      lock-free auto-resume

param(
    [Parameter(Mandatory = $true)][string]$Media,
    # Scheduled-task mode: skip the Read-Host manual prompt; visual items are
    # logged as PENDING-CONFIRM for the coordinator to sign off on sight.
    [switch]$Auto
)

$ErrorActionPreference = "Continue"
$ev = "$env:USERPROFILE\test\evidence\gate_r8"
New-Item -ItemType Directory -Force -Path $ev | Out-Null
$log = "$ev\gate_r8_session.log"

function Log($msg) {
    $line = "{0} {1}" -f (Get-Date -Format "yyyy-MM-dd HH:mm:ss"), $msg
    $line | Tee-Object -FilePath $log -Append
}

Log "=== Gate R-8 desktop verification start ==="

# --- 0. Driver version gate (>= 621.24 WHQL); CIM-free (SSH-safe) detection ---
function Get-NvidiaDriverVersion {
    # 1) nvidia-smi (ships with the driver, no WMI needed)
    $smi = "$env:SystemRoot\System32\nvidia-smi.exe"
    if (Test-Path $smi) {
        $out = (& $smi --query-gpu=driver_version,name --format=csv,noheader 2>$null) | Select-Object -First 1
        if ($out -match "(\d+)\.(\d+)") {
            return @{ ver = "$($Matches[1]).$($Matches[2])"; name = ($out -split ",")[1].Trim() }
        }
    }
    # 2) registry: display class drivers (read access needs no elevation)
    $cls = "HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e968-e325-11ce-bfc1-08002be10318}"
    foreach ($sub in (Get-ChildItem $cls -ErrorAction SilentlyContinue)) {
        $p = Get-ItemProperty $sub.PSPath -ErrorAction SilentlyContinue
        if ($p.DriverVersion -and $p.DriverDescription -match "NVIDIA") {
            return @{ ver = $p.DriverVersion; name = $p.DriverDescription }
        }
    }
    return $null
}

$drv = Get-NvidiaDriverVersion
if ($null -eq $drv) {
    Log "FATAL: NVIDIA driver version not detectable (nvidia-smi + registry both failed)"
    exit 1
}
Log ("GPU: {0}" -f $drv.name)
Log ("DriverVersion: {0}" -f $drv.ver)
$digits = ($drv.ver -replace "\D", "")
# Keep the last 5 digits: 621.24 -> "62124" (621.xx family, 5-digit scheme).
if ($digits.Length -lt 5) {
    Log ("FATAL: cannot normalize driver version '{0}'" -f $drv.ver)
    exit 1
}
$ver5 = [int]$digits.Substring($digits.Length - 5)
if ($ver5 -lt 62124) {
    Log ("FATAL: driver {0} < 621.24 WHQL - upgrade the driver first (user-side action)" -f $drv.ver)
    exit 1
}
Log "DRIVER-GATE PASS"

# --- 1. Session gate (must be interactive, not Session 0) ---
$sessionId = (Get-Process -Id $PID).SessionId
Log ("PowerShell session id = {0}" -f $sessionId)
if ($sessionId -eq 0) {
    Log "FATAL: running in Session 0; run this script from the interactive desktop"
    exit 1
}
Log "SESSION-GATE PASS"

# --- 2. Product binary + plugin staging ---
$repoRoot = (Resolve-Path "$PSScriptRoot\..").Path
$exe = "$repoRoot\.vs\build\cmake\windows-msvc-ninja\CineRTX.exe"
if (-not (Test-Path $exe)) {
    $exe = "$env:USERPROFILE\test\CineRTX\.vs\build\cmake\windows-msvc-ninja\CineRTX.exe"
}
if (-not (Test-Path $exe)) {
    Log "FATAL: CineRTX.exe not found at $exe"
    exit 1
}
$exeDir = Split-Path $exe
foreach ($dll in @("nvngx_vsr.dll", "nvngx_truehdr.dll")) {
    if (-not (Test-Path "$exeDir\$dll")) {
        Log "FATAL: $dll missing beside CineRTX.exe"
        exit 1
    }
}
if (-not (Test-Path $Media)) {
    Log "FATAL: media file not found: $Media"
    exit 1
}
Log "PRODUCT-GATE PASS (media: $Media)"

# --- 3. Launch and sample ---
Log "Launching CineRTX.exe (quick-play = borderless fullscreen player)..."
$proc = Start-Process -FilePath $exe -ArgumentList "`"$Media`"" -PassThru
Start-Sleep -Seconds 6
if ($proc.HasExited) {
    Log ("FATAL: CineRTX.exe exited early, code {0}" -f $proc.ExitCode)
    exit 1
}
Log "LAUNCH PASS (6 s alive)"

# --- 4. Resize stress: 50 shrink/grow pairs = 100 transitions ---
Log "RESIZE-STRESS: 50 shrink/grow pairs (100 transitions) on the main window..."
$sig = @"
using System;
using System.Runtime.InteropServices;
public static class Win32Resize {
    [DllImport("user32.dll")] public static extern bool MoveWindow(IntPtr hWnd, int X, int Y, int nWidth, int nHeight, bool bRepaint);
}
"@
Add-Type -TypeDefinition $sig
$proc.Refresh()
$hwnd = $proc.MainWindowHandle
if ($hwnd -eq [IntPtr]::Zero) {
    Log "FATAL: main window handle unavailable"
    exit 1
}
for ($i = 0; $i -lt 50 -and -not $proc.HasExited; $i++) {
    [Win32Resize]::MoveWindow($hwnd, 0, 0, 1220 - ($i % 8) * 40, 860 - ($i % 6) * 30, $true) | Out-Null
    Start-Sleep -Milliseconds 100
    [Win32Resize]::MoveWindow($hwnd, 0, 0, 1220, 860, $true) | Out-Null
    Start-Sleep -Milliseconds 100
}
if ($proc.HasExited) {
    Log "FATAL: CineRTX.exe crashed during resize stress"
    exit 1
}
Log "RESIZE-STRESS PASS (100 transitions, no crash)"

Log ""
Log "=== MANUAL CHECKLIST (confirm each in the visible window) ==="
Log "[ ] H.264/HEVC/AV1 playback monitor shows a live picture (presented > 0)"
Log "[ ] Transcode monitor (if a job is running) shows the borrowed slot"
Log "[ ] No stutter/black-screen after 10 s of playback (6979343 regression check)"
Log "[ ] HDR/SDR cross-monitor switch: colours stay correct (no crush)"
Log "[ ] Audio hot-unplug/replug: playback auto-resumes without a lock-up"
Log ""
if ($Auto) {
    Log "MANUAL-CHECKLIST: PENDING-CONFIRM (scheduled run; coordinator signs off on sight)"
} else {
    $confirm = Read-Host "Type PASS after confirming the checklist above (or FAIL)"
    Log "MANUAL-CHECKLIST: $confirm"
}

# --- 5. Telemetry/log capture + shutdown ---
$runtime = "$repoRoot\.vs\build\cmake\windows-msvc-ninja"
if (-not (Test-Path $runtime)) {
    $runtime = "$env:USERPROFILE\test\CineRTX\.vs\build\cmake\windows-msvc-ninja"
}
if (Test-Path "$runtime\cinertx_runtime.log") {
    Copy-Item "$runtime\cinertx_runtime.log" "$ev\cinertx_runtime.log" -Force
    Log "runtime log captured"
}
$proc | Stop-Process -ErrorAction SilentlyContinue
Start-Sleep -Seconds 2
if (-not $proc.HasExited) {
    Log "note: process still alive after Stop-Process (investigate)"
} else {
    Log "EXIT PASS (clean shutdown)"
}
Log ("=== evidence written to {0} ===" -f $ev)
Log "=== Gate R-8 desktop verification end ==="
