# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 CineRTX contributors
#
# Audits SPDX headers, the GPL/MIT directory boundary, forbidden NVIDIA
# includes, and accidental binary blobs.  Uses only PowerShell so it runs on
# Windows and Linux CI runners without extra tooling.

[CmdletBinding()]
param(
    [string]$Root
)

$ErrorActionPreference = 'Stop'
if (-not $Root) {
    $Root = Split-Path -Parent $PSScriptRoot
}
$repo = (Resolve-Path -LiteralPath $Root).Path

$gplExpression = 'GPL-3.0-or-later WITH Plugin-Exception'
$mitExpression = 'MIT OR Apache-2.0'

$violations = [System.Collections.Generic.List[string]]::new()
$gplCount = 0
$mitCount = 0

function Get-RelativePath([string]$Path) {
    return [IO.Path]::GetRelativePath($repo, $Path).Replace('\', '/')
}

function Add-Violation([string]$Message) {
    $script:violations.Add($Message)
}

function Get-SpdxExpression([string]$Path) {
    $lines = Get-Content -LiteralPath $Path -TotalCount 6 -ErrorAction Stop
    foreach ($line in $lines) {
        if ($line -match 'SPDX-License-Identifier:\s*(.+?)\s*$') {
            return $Matches[1]
        }
    }
    return $null
}

function Test-SourceFile([string]$Path) {
    $relative = Get-RelativePath $Path
    if ((Get-Item -LiteralPath $Path).Length -eq 0) {
        # An empty translation unit is never intentional; reporting it here
        # catches the "declared in CMake but never written" failure mode.
        Add-Violation "${relative}: empty source file"
        return
    }
    $expression = Get-SpdxExpression $Path
    if (-not $expression) {
        Add-Violation "${relative}: missing SPDX-License-Identifier"
        return
    }

    $expected = if ($relative.StartsWith('include/shim/') -or $relative.StartsWith('src/shim/')) {
        $mitExpression
    } else {
        $gplExpression
    }

    if ($expression -ne $expected) {
        Add-Violation "${relative}: expected '$expected', found '$expression'"
        return
    }

    if ($expected -eq $gplExpression) {
        $script:gplCount++
        $content = Get-Content -LiteralPath $Path -Raw
        $forbiddenInclude = '(?im)^\s*#\s*include\s*[<"](?:cuda(?:\.h|_runtime\.h|_driver_api\.h)?|nvcuvid\.h|cuviddec\.h|nvEncodeAPI\.h|nvinfer(?:_runtime|_plugins)?\.h|NvInfer(?:Runtime)?\.h|nvsdk_ngx[^>"]*\.h|nvOpticalFlow[^>"]*\.h|NvFRUC\.h|NvOFFRUC\.h|npp[^>"]*\.h)[>"]'
        if ($content -match $forbiddenInclude) {
            Add-Violation "${relative}: GPL file includes an NVIDIA SDK header ($($Matches[0].Trim()))"
        }
        $forbiddenPragma = '(?im)^\s*#\s*pragma\s+comment\s*\(\s*lib\s*,\s*"[^"]*(?:nvcuda|nvcuvid|nvEncodeAPI|nvinfer|nvsdk_ngx|NvOFFRUC|npp|cudart)[^"]*"\s*\)'
        if ($content -match $forbiddenPragma) {
            Add-Violation "${relative}: GPL file carries an NVIDIA #pragma comment(lib, ...)"
        }
    } else {
        $script:mitCount++
    }
}

# Vendored open headers keep their upstream copyright and MIT notice verbatim.
# The audit accepts either an explicit SPDX MIT tag or the full MIT permission
# sentence, and refuses anything that is not MIT-compatible.
function Test-ThirdPartyFile([string]$Path) {
    $relative = Get-RelativePath $Path
    if ((Get-Item -LiteralPath $Path).Length -eq 0) {
        Add-Violation "${relative}: empty source file"
        return
    }
    $expression = Get-SpdxExpression $Path
    if ($expression) {
        if ($expression -notmatch 'MIT') {
            Add-Violation "${relative}: vendored header must be MIT, found '$expression'"
            return
        }
        $script:mitCount++
        return
    }
    $head = (Get-Content -LiteralPath $Path -TotalCount 20) -join "`n"
    if ($head -match 'Permission is hereby granted, free of charge') {
        $script:mitCount++
        return
    }
    Add-Violation "${relative}: no SPDX identifier and no MIT permission notice"
}

function Test-BuildFile([string]$Path, [string]$Expected) {
    $relative = Get-RelativePath $Path
    $expression = Get-SpdxExpression $Path
    if (-not $expression) {
        Add-Violation "${relative}: missing SPDX-License-Identifier"
        return
    }
    if ($expression -ne $Expected) {
        Add-Violation "${relative}: expected '$Expected', found '$expression'"
    } elseif ($Expected -eq $gplExpression) {
        $script:gplCount++
    } else {
        $script:mitCount++
    }
}

# ---------------------------------------------------------------------------
# Source and header files
# ---------------------------------------------------------------------------

$sourceExtensions = @('.h', '.hpp', '.c', '.cc', '.cpp', '.cxx', '.cu', '.cuh')
foreach ($directory in @('include', 'src', 'tests')) {
    $path = Join-Path $repo $directory
    if (-not (Test-Path -LiteralPath $path)) {
        Add-Violation "$directory/: directory is missing"
        continue
    }
    $files = Get-ChildItem -LiteralPath $path -Recurse -File | Where-Object {
        $sourceExtensions -contains $_.Extension.ToLowerInvariant()
    }
    foreach ($file in $files) {
        Test-SourceFile $file.FullName
    }
}

# ---------------------------------------------------------------------------
# Official Git submodules (exempt from the hard directory gate)
#
# third_party/ffmpeg and third_party/nv-codec-headers are upstream submodules,
# not vendored copies: their content is pinned by gitlink SHA and policed by its
# own project.  An uninitialised submodule leaves an empty directory, which must
# not fail the audit -- so the check is soft.  When the tree *is* present the
# MIT rule is still enforced, because a submodule swap that dragged in a
# non-MIT header would otherwise pass unnoticed.
# ---------------------------------------------------------------------------

$submoduleHeaders = @(
    @{ Path = 'third_party/nv-codec-headers/include/ffnvcodec'; Name = 'nv-codec-headers' }
)
$submoduleAudited = 0
foreach ($entry in $submoduleHeaders) {
    $path = Join-Path $repo $entry.Path
    if (-not (Test-Path -LiteralPath $path)) {
        Write-Host "  notice   : $($entry.Name) submodule not initialised; licence audit skipped"
        continue
    }
    $files = Get-ChildItem -LiteralPath $path -Recurse -File | Where-Object {
        $sourceExtensions -contains $_.Extension.ToLowerInvariant()
    }
    foreach ($file in $files) {
        Test-ThirdPartyFile $file.FullName
        $submoduleAudited++
    }
}

# ---------------------------------------------------------------------------
# Build, tooling, and CI text files
# ---------------------------------------------------------------------------

$cmakeLists = Join-Path $repo 'CMakeLists.txt'
if (Test-Path -LiteralPath $cmakeLists) {
    Test-BuildFile $cmakeLists $gplExpression
} else {
    Add-Violation 'CMakeLists.txt: missing'
}

$cmakeDir = Join-Path $repo 'cmake'
if (Test-Path -LiteralPath $cmakeDir) {
    foreach ($file in Get-ChildItem -LiteralPath $cmakeDir -File -Filter '*.cmake') {
        Test-BuildFile $file.FullName $gplExpression
    }
}

$toolsDir = Join-Path $repo 'tools'
if (Test-Path -LiteralPath $toolsDir) {
    foreach ($file in Get-ChildItem -LiteralPath $toolsDir -File -Filter '*.ps1') {
        Test-BuildFile $file.FullName $gplExpression
    }
}

$workflowDir = Join-Path $repo '.github/workflows'
if (Test-Path -LiteralPath $workflowDir) {
    foreach ($file in Get-ChildItem -LiteralPath $workflowDir -File | Where-Object {
        $_.Extension -in @('.yml', '.yaml')
    }) {
        Test-BuildFile $file.FullName $gplExpression
    }
}

# ---------------------------------------------------------------------------
# License texts
# ---------------------------------------------------------------------------

$requiredLicenses = @(
    @{ Path = 'LICENSE'; Needle = 'GNU GENERAL PUBLIC LICENSE' },
    @{ Path = 'COPYING.EXCEPTION'; Needle = 'Plugin-Exception' },
    @{ Path = 'LICENSE-MIT'; Needle = 'MIT License' },
    @{ Path = 'LICENSE-APACHE'; Needle = 'Apache License' }
)
foreach ($license in $requiredLicenses) {
    $path = Join-Path $repo $license.Path
    if (-not (Test-Path -LiteralPath $path)) {
        Add-Violation "$($license.Path): missing license text"
        continue
    }
    $content = Get-Content -LiteralPath $path -Raw
    if (-not $content.Contains($license.Needle)) {
        Add-Violation "$($license.Path): does not contain '$($license.Needle)'"
    }
    if ($license.Path -eq 'LICENSE') {
        $firstNonEmpty = (Get-Content -LiteralPath $path | Where-Object { $_.Trim().Length -gt 0 } | Select-Object -First 1).Trim()
        if ($firstNonEmpty -ne 'GNU GENERAL PUBLIC LICENSE') {
            Add-Violation "LICENSE: must be a verbatim FSF GPLv3 text starting with 'GNU GENERAL PUBLIC LICENSE' (found '$firstNonEmpty'); keep custom exceptions strictly in COPYING.EXCEPTION"
        }
    }
}

# ---------------------------------------------------------------------------
# Binary blobs in source directories
# ---------------------------------------------------------------------------

$binaryExtensions = @('.lib', '.a', '.dll', '.so', '.dylib', '.exe', '.obj', '.o', '.pdb')
$binaryCount = 0
foreach ($directory in @('include', 'src', 'tests', 'tools', 'cmake')) {
    $path = Join-Path $repo $directory
    if (-not (Test-Path -LiteralPath $path)) { continue }
    foreach ($file in Get-ChildItem -LiteralPath $path -Recurse -File) {
        if ($binaryExtensions -contains $file.Extension.ToLowerInvariant()) {
            Add-Violation "$(Get-RelativePath $file.FullName): binary blob in the source tree"
            $binaryCount++
        }
    }
}

# ---------------------------------------------------------------------------
# AI model weights and serialised inference engines (TASK-001)
#
# TensorRT .engine blobs and ONNX/RIFE weights are either proprietary or
# multi-gigabyte; they must be generated locally with --build-rife.  A committed
# blob would also make the GPL source distribution unverifiable.
# ---------------------------------------------------------------------------

$modelExtensions = @('.engine', '.plan', '.onnx', '.trt', '.safetensors', '.pth', '.pt')
$modelCount = 0
foreach ($directory in @('include', 'src', 'tests', 'tools', 'cmake', 'docs')) {
    $path = Join-Path $repo $directory
    if (-not (Test-Path -LiteralPath $path)) { continue }
    foreach ($file in Get-ChildItem -LiteralPath $path -Recurse -File -ErrorAction SilentlyContinue) {
        if ($modelExtensions -contains $file.Extension.ToLowerInvariant()) {
            Add-Violation "$(Get-RelativePath $file.FullName): AI model weights or TensorRT engine blobs MUST NOT be committed to Git. Use --build-rife to generate locally."
            $modelCount++
        }
    }
}

Write-Host 'SPDX and boundary audit'
Write-Host "  root     : $repo"
Write-Host "  licenses : GPL=$gplCount MIT=$mitCount"
Write-Host "  submodule: $submoduleAudited header(s) MIT-verified"
Write-Host "  binaries : $binaryCount"
Write-Host "  models   : $modelCount"

if ($violations.Count -gt 0) {
    Write-Host ''
    foreach ($violation in $violations) {
        Write-Host "  FAIL $violation"
    }
    Write-Host ''
    Write-Host "audit FAILED with $($violations.Count) violation(s)"
    exit 1
}

Write-Host '  result   : PASS'
exit 0
