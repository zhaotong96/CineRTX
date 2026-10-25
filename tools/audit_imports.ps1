# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Static import-table audit.  Parses PE import and delay-import directories
# directly (no Visual Studio required), or uses readelf/objdump for ELF.
# Fails if the core or the shim statically imports an NVIDIA library.

[CmdletBinding()]
param(
    [string[]]$Binary = @(),
    [string]$BuildDir,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# Support both PowerShell-native arrays (-Binary a,b) and the single
# comma-joined token produced by `pwsh -File`.
$Binary = @($Binary | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

$forbiddenPattern = '(?i)(nvcuda|nvcuvid|nvencodeapi|nvsdk_ngx|nvngx|nvidia-ngx|nvofruc|nvinfer|nvrtc|nvapi)'
$violations = [System.Collections.Generic.List[string]]::new()

function Test-PathMagic {
    param([byte[]]$Bytes, [byte[]]$Magic)
    if ($Bytes.Length -lt $Magic.Length) { return $false }
    for ($i = 0; $i -lt $Magic.Length; $i++) {
        if ($Bytes[$i] -ne $Magic[$i]) { return $false }
    }
    return $true
}

function Get-PeImports {
    param([string]$Path)

    $bytes = [IO.File]::ReadAllBytes($Path)
    if ($bytes.Length -lt 0x40) {
        throw 'file is too small to be a PE image'
    }

    $peOffset = [BitConverter]::ToInt32($bytes, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 0x18 -gt $bytes.Length) {
        throw 'invalid e_lfanew'
    }
    if ([BitConverter]::ToUInt32($bytes, $peOffset) -ne 0x00004550) {
        throw 'missing PE signature'
    }

    $coff = $peOffset + 4
    $sectionCount = [BitConverter]::ToUInt16($bytes, $coff + 2)
    $optionalSize = [BitConverter]::ToUInt16($bytes, $coff + 16)
    $optional = $coff + 20
    if ($optionalSize -lt 96 -or $optional + $optionalSize -gt $bytes.Length) {
        throw 'invalid optional header size'
    }

    $magic = [BitConverter]::ToUInt16($bytes, $optional)
    $pe32Plus = $magic -eq 0x20B
    if (-not $pe32Plus -and $magic -ne 0x10B) {
        throw 'unknown PE optional header magic'
    }

    $imageBase = if ($pe32Plus) {
        [BitConverter]::ToUInt64($bytes, $optional + 24)
    } else {
        [uint64][BitConverter]::ToUInt32($bytes, $optional + 28)
    }

    $directoryBase = $optional + $(if ($pe32Plus) { 112 } else { 96 })
    $directoryCountOffset = $optional + $(if ($pe32Plus) { 108 } else { 92 })
    $directoryCount = [BitConverter]::ToUInt32($bytes, $directoryCountOffset)

    $sections = @()
    $sectionTable = $optional + $optionalSize
    for ($i = 0; $i -lt $sectionCount; $i++) {
        $offset = $sectionTable + ($i * 40)
        if ($offset + 40 -gt $bytes.Length) { break }
        $sections += [pscustomobject]@{
            VirtualSize  = [BitConverter]::ToUInt32($bytes, $offset + 8)
            VirtualAddress = [BitConverter]::ToUInt32($bytes, $offset + 12)
            RawSize      = [BitConverter]::ToUInt32($bytes, $offset + 16)
            RawPointer   = [BitConverter]::ToUInt32($bytes, $offset + 20)
        }
    }

    function Get-DirectoryRva([int]$Index) {
        if ($Index -ge $directoryCount) { return [uint64]0 }
        $offset = $directoryBase + ($Index * 8)
        if ($offset + 4 -gt $bytes.Length) { return [uint64]0 }
        return [uint64][BitConverter]::ToUInt32($bytes, $offset)
    }

    function Convert-RvaToOffset([uint64]$Rva) {
        if ($Rva -eq 0) { return [long]-1 }
        foreach ($section in $sections) {
            $span = [Math]::Max([uint64]$section.VirtualSize, [uint64]$section.RawSize)
            if ($span -eq 0) { continue }
            $start = [uint64]$section.VirtualAddress
            if ($Rva -ge $start -and $Rva -lt ($start + $span)) {
                $delta = $Rva - $start
                if ($delta -lt [uint64]$section.RawSize) {
                    return [long]([uint64]$section.RawPointer + $delta)
                }
                return [long]-1
            }
        }
        return [long]-1
    }

    function Read-Utf8Z([long]$Offset) {
        if ($Offset -lt 0 -or $Offset -ge $bytes.Length) { return $null }
        $end = $Offset
        $limit = [Math]::Min($bytes.Length, $Offset + 1024)
        while ($end -lt $limit -and $bytes[$end] -ne 0) { $end++ }
        if ($end -eq $Offset) { return $null }
        return [Text.Encoding]::UTF8.GetString($bytes, [int]$Offset, [int]($end - $Offset))
    }

    $dlls = [System.Collections.Generic.List[string]]::new()
    $symbols = [System.Collections.Generic.List[string]]::new()
    $delayDlls = [System.Collections.Generic.List[string]]::new()
    $delaySymbols = [System.Collections.Generic.List[string]]::new()

    function Read-Thunks([uint64]$ThunkRva, [string]$DllName, $Target) {
        if ($ThunkRva -eq 0) { return }
        $thunkOffset = Convert-RvaToOffset $ThunkRva
        if ($thunkOffset -lt 0) { return }
        $width = if ($pe32Plus) { 8 } else { 4 }
        $ordinalFlag = if ($pe32Plus) {
            [uint64]::Parse('9223372036854775808')
        } else {
            [uint64]2147483648
        }

        for ($i = 0; $i -lt 65536; $i++) {
            $offset = $thunkOffset + ($i * $width)
            if ($offset + $width -gt $bytes.Length) { break }
            $value = if ($pe32Plus) {
                [BitConverter]::ToUInt64($bytes, [int]$offset)
            } else {
                [uint64][BitConverter]::ToUInt32($bytes, [int]$offset)
            }
            if ($value -eq 0) { break }

            if (($value -band $ordinalFlag) -ne 0) {
                $ordinal = $value -band [uint64]0xFFFF
                $Target.Add("$DllName!#$ordinal")
            } else {
                $nameRva = $value -band [uint64]0x7FFFFFFF
                $nameOffset = Convert-RvaToOffset $nameRva
                if ($nameOffset -ge 0) {
                    $name = Read-Utf8Z ($nameOffset + 2)
                    if ($name) { $Target.Add("$DllName!$name") }
                }
            }
        }
    }

    function Read-ImportDirectory([uint64]$Rva, [bool]$Delay) {
        if ($Rva -eq 0) { return }
        $offset = Convert-RvaToOffset $Rva
        if ($offset -lt 0) { return }
        $step = if ($Delay) { 32 } else { 20 }

        for ($i = 0; $i -lt 4096; $i++) {
            $entry = $offset + ($i * $step)
            if ($entry + $step -gt $bytes.Length) { break }

            if ($Delay) {
                $attributes = [BitConverter]::ToUInt32($bytes, [int]$entry)
                if (($attributes -band 1) -eq 0) {
                    # VA-based delay import descriptors are obsolete; skip
                    # rather than risk an incorrect subtraction.
                    continue
                }
                $nameRva = [uint64][BitConverter]::ToUInt32($bytes, [int]$entry + 4)
                $intRva = [uint64][BitConverter]::ToUInt32($bytes, [int]$entry + 16)
                $iatRva = [uint64][BitConverter]::ToUInt32($bytes, [int]$entry + 12)
            } else {
                $intRva = [uint64][BitConverter]::ToUInt32($bytes, [int]$entry)
                $timeStamp = [BitConverter]::ToUInt32($bytes, [int]$entry + 4)
                $forwarder = [BitConverter]::ToUInt32($bytes, [int]$entry + 8)
                $nameRva = [uint64][BitConverter]::ToUInt32($bytes, [int]$entry + 12)
                $iatRva = [uint64][BitConverter]::ToUInt32($bytes, [int]$entry + 16)
                if ($intRva -eq 0 -and $nameRva -eq 0 -and $iatRva -eq 0 -and
                    $timeStamp -eq 0 -and $forwarder -eq 0) {
                    break
                }
            }

            if ($nameRva -eq 0) { break }
            $nameOffset = Convert-RvaToOffset $nameRva
            if ($nameOffset -lt 0) { break }
            $name = Read-Utf8Z $nameOffset
            if (-not $name) { break }

            if ($Delay) {
                $delayDlls.Add($name)
                Read-Thunks $intRva $name $delaySymbols
            } else {
                $dlls.Add($name)
                Read-Thunks $intRva $name $symbols
            }
        }
    }

    Read-ImportDirectory (Get-DirectoryRva 1) $false
    Read-ImportDirectory (Get-DirectoryRva 13) $true

    return [pscustomobject]@{
        Format        = 'PE'
        Dependencies  = $dlls
        Symbols       = $symbols
        DelayDependencies = $delayDlls
        DelaySymbols  = $delaySymbols
    }
}

function Get-ElfImports {
    param([string]$Path)

    $dependencies = [System.Collections.Generic.List[string]]::new()
    $symbols = [System.Collections.Generic.List[string]]::new()

    $readelf = Get-Command 'readelf' -ErrorAction SilentlyContinue
    if ($readelf) {
        $dynamic = & $readelf.Source -d --wide $Path 2>$null
        foreach ($line in $dynamic) {
            if ($line -match '\(NEEDED\).*\[(.+?)\]') {
                $dependencies.Add($Matches[1])
            }
        }
        $dynamicSymbols = & $readelf.Source --dyn-syms --wide $Path 2>$null
        foreach ($line in $dynamicSymbols) {
            if ($line -match '^\s*\d+:\s+[0-9a-fA-F]+\s+\d+\s+UND\s+(\S+)') {
                $symbols.Add($Matches[1])
            }
        }
        return [pscustomobject]@{
            Format        = 'ELF'
            Dependencies  = $dependencies
            Symbols       = $symbols
            DelayDependencies = @()
            DelaySymbols  = @()
        }
    }

    $objdump = Get-Command 'objdump' -ErrorAction SilentlyContinue
    if ($objdump) {
        $output = & $objdump.Source -p $Path 2>$null
        foreach ($line in $output) {
            if ($line -match '^\s*NEEDED\s+(\S+)') {
                $dependencies.Add($Matches[1])
            }
        }
        return [pscustomobject]@{
            Format        = 'ELF'
            Dependencies  = $dependencies
            Symbols       = $symbols
            DelayDependencies = @()
            DelaySymbols  = @()
        }
    }

    throw 'neither readelf nor objdump is available for ELF inspection'
}

function Find-CandidateBinaries {
    param([string]$Directory)

    $names = @(
        'rtx-transcode.exe', 'rtx-transcode',
        'rtx-transcode-base.exe', 'rtx-transcode-base',
        'rtx_tests.exe', 'rtx_tests',
        'rtx_shim.dll', 'rtx_shim.so', 'librtx_shim.so', 'librtx_shim.dylib',
        'rtx_ngx_bridge.dll', 'rtx_ngx_bridge.so', 'librtx_ngx_bridge.so',
        'librtx_ngx_bridge.dylib'
    )
    $found = @()
    foreach ($name in $names) {
        $candidate = Join-Path $Directory $name
        if (Test-Path -LiteralPath $candidate -PathType Leaf) {
            $found += (Resolve-Path -LiteralPath $candidate).Path
        }
    }
    return $found
}

if ($Binary.Count -eq 0) {
    $searchDirectory = if ($BuildDir) {
        if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repo $BuildDir }
    } else {
        foreach ($candidate in @('build/cmake/zig-windows', 'build/cmake/windows-msvc/Release',
                                 'build/cmake/linux-release', 'build/ci', 'build')) {
            $path = Join-Path $repo $candidate
            if (Test-Path -LiteralPath $path) {
                $path
                break
            }
        }
    }

    if (-not $searchDirectory -or -not (Test-Path -LiteralPath $searchDirectory)) {
        Write-Host 'No build directory found. Pass -Binary <path> or -BuildDir <dir>.'
        exit 2
    }

    $Binary = Find-CandidateBinaries $searchDirectory
    if ($Binary.Count -eq 0) {
        Write-Host "No rtx-transcode / rtx_shim / rtx_ngx_bridge binaries under $searchDirectory"
        exit 2
    }
}

Write-Host 'Import-table audit'
$inspected = 0
foreach ($path in $Binary) {
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) {
        $violations.Add("missing binary: $path")
        continue
    }
    $resolved = (Resolve-Path -LiteralPath $path).Path
    $bytes = [IO.File]::ReadAllBytes($resolved)

    try {
        if ($bytes.Length -ge 2 -and $bytes[0] -eq 0x4D -and $bytes[1] -eq 0x5A) {
            $imports = Get-PeImports $resolved
        } elseif (Test-PathMagic $bytes ([byte[]](0x7F, 0x45, 0x4C, 0x46))) {
            $imports = Get-ElfImports $resolved
        } else {
            throw 'unrecognized executable format'
        }
    } catch {
        $violations.Add("${resolved}: $($_.Exception.Message)")
        Write-Host "  [FAIL] $resolved"
        continue
    }

    $inspected++
    $allNames = @($imports.Dependencies) + @($imports.DelayDependencies)
    $hits = @()
    foreach ($name in $allNames) {
        if ($name -match $forbiddenPattern) {
            $hits += "dependency $name"
        }
    }
    foreach ($symbol in @($imports.Symbols) + @($imports.DelaySymbols)) {
        if ($symbol -match $forbiddenPattern) {
            $hits += "symbol $symbol"
        }
    }

    $dependencyText = if ($allNames.Count -gt 0) { ($allNames -join ', ') } else { '(none)' }
    if (-not $Quiet) {
        Write-Host "  [$($imports.Format)] $resolved"
        Write-Host "         dependencies: $dependencyText"
        Write-Host "         imported symbols: $($imports.Symbols.Count + $imports.DelaySymbols.Count)"
    }

    if ($hits.Count -gt 0) {
        foreach ($hit in $hits) {
            Write-Host "  [FAIL] $hit"
            $violations.Add("$resolved imports $hit")
        }
    } elseif (-not $Quiet) {
        Write-Host '         result: no NVIDIA static imports'
    }
}

Write-Host "  inspected: $inspected binary/binaries"
if ($violations.Count -gt 0) {
    Write-Host ''
    foreach ($violation in $violations) {
        Write-Host "  FAIL $violation"
    }
    Write-Host ''
    Write-Host "audit FAILED with $($violations.Count) violation(s)"
    exit 1
}

Write-Host '  result: PASS'
exit 0
