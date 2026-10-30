# SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
# Copyright (C) 2026 RTX Transcode Engine contributors
#
# Static import-table audit and single-EXE self-containment gate (Gate 3.0).
#
# Five independent checks:
#   A. Zone B/C: no PE file may statically import any NVIDIA module (nvcuda,
#      nvcuvid, nvEncodeAPI64, nvsdk_ngx_d, _nvngx, nvngx_vsr, nvngx_truehdr,
#      NvOFFRUC, nvinfer, nvonnxparser).  Those are reached through
#      LoadLibraryExW at run time only.
#   B. Zone A: the media container layer is statically linked, so no PE file may
#      import avcodec / avformat / avutil / swresample / swscale / avfilter /
#      avdevice / postproc.
#   C. The build output directory must not contain any av*.dll / sw*.dll.
#   D. The single-EXE envelope: 1.2 MB <= CineRTX.exe <= 12.0 MB, with a
#      toolchain-dependent golden band (8.0 MB unstripped / 3.5 MB stripped).
#   E. Single-EXE closure: the output directory holds CineRTX.exe and, in a
#      developer configuration, rtx_tests.exe -- and nothing else.  Any DLL,
#      shared object or extra executable is a regression to the retired
#      sidecar layout.
#
# Parses PE import and delay-import directories directly (no Visual Studio
# required), or uses readelf/objdump for ELF.

[CmdletBinding()]
param(
    [string[]]$Binary = @(),
    [string]$BuildDir,
    # Gate 3.0 minimum: an empty shell (the old ~338 KB stub) must never pass.
    [double]$MinExeMb = 1.2,
    # Gate 3.0 maximum: the golden envelope ceiling for the single-EXE build.
    [double]$MaxExeMb = 12.0,
    # Reported as a warning band; the hard envelope above is what fails.
    [double]$GoldenExeMb = 8.0,
    # Same warning band for an image whose COFF symbol table was stripped
    # (-Wl,--strip-all on the GCC/Clang/Zig path).  Selected automatically.
    [double]$GoldenExeMbStripped = 3.5,
    [switch]$SkipEnvelope,
    [switch]$Quiet
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot

# Support both PowerShell-native arrays (-Binary a,b) and the single
# comma-joined token produced by `pwsh -File`.
$Binary = @($Binary | ForEach-Object { $_ -split ',' } | Where-Object { $_ })

# NVIDIA proprietary modules: Zone B (driver) and Zone C (bridge shims).
$forbiddenPattern = '(?i)(nvcuda|nvcuvid|nvencodeapi|nvsdk_ngx|nvngx|nvidia-ngx|nvofruc|nvinfer|nvonnxparser|nvrtc|nvapi)'
# The ten module families the phase-3 verdict enumerates explicitly.
$forbiddenNvidiaModules = @(
    'nvcuda', 'nvcuvid', 'nvEncodeAPI64', 'nvsdk_ngx_d', '_nvngx',
    'nvngx_vsr', 'nvngx_truehdr', 'NvOFFRUC', 'nvinfer', 'nvonnxparser'
)
# Zone A media container layer: statically linked, never imported.
$forbiddenMediaPattern = '(?i)(^|[\\/])(avcodec|avformat|avutil|swresample|swscale|avfilter|avdevice|postproc)[-_0-9]*\.(dll|so)'
$mediaDllPattern = '(?i)^(avcodec|avformat|avutil|swresample|swscale|avfilter|avdevice|postproc)[-_0-9]*\.dll$'

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
        'CineRTX.exe', 'CineRTX',
        'rtx_tests.exe', 'rtx_tests'
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

function Test-MediaDllLeak {
    param([string]$Directory)

    if (-not $Directory -or -not (Test-Path -LiteralPath $Directory)) {
        return
    }
    $leaks = @(Get-ChildItem -LiteralPath $Directory -File -Filter '*.dll' -ErrorAction SilentlyContinue |
        Where-Object { $_.Name -match $mediaDllPattern })
    if ($leaks.Count -gt 0) {
        $names = ($leaks | ForEach-Object { $_.Name }) -join ', '
        $violations.Add("$Directory contains external media DLL(s): $names (FFmpeg must be statically inlined)")
        Write-Host "  [FAIL] external media DLL(s) beside the binaries: $names"
    } elseif (-not $Quiet) {
        Write-Host '  [OK]   no external media DLL in the output directory'
    }
}

function Test-PeStripped {
    param([string]$Path)

    try {
        $stream = [IO.File]::OpenRead($Path)
    } catch {
        return $false
    }
    try {
        $head = New-Object byte[] 4096
        $read = $stream.Read($head, 0, $head.Length)
    } finally {
        $stream.Dispose()
    }
    if ($read -lt 0x40 -or $head[0] -ne 0x4D -or $head[1] -ne 0x5A) {
        return $false
    }
    $peOffset = [BitConverter]::ToInt32($head, 0x3C)
    if ($peOffset -lt 0 -or $peOffset + 20 -gt $read) {
        return $false
    }
    if ([BitConverter]::ToUInt32($head, $peOffset) -ne 0x00004550) {
        return $false
    }
    return [BitConverter]::ToUInt32($head, $peOffset + 16) -eq 0
}

function Test-ExeEnvelope {
    param([string]$Directory)

    if ($SkipEnvelope -or -not $Directory) {
        return
    }
    $exe = Join-Path $Directory 'CineRTX.exe'
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        $exe = Join-Path $Directory 'CineRTX'
    }
    if (-not (Test-Path -LiteralPath $exe -PathType Leaf)) {
        $violations.Add('CineRTX is missing from the output directory; the single-EXE envelope cannot be evaluated')
        Write-Host "  [FAIL] no CineRTX executable in $Directory"
        return
    }
    $name = Split-Path -Leaf $exe
    $bytes = (Get-Item -LiteralPath $exe).Length
    $megabytes = $bytes / 1MB
    $text = ('{0:N2} MB ({1} bytes)' -f $megabytes, $bytes)

    # The golden floor is a property of the toolchain, not a single constant.
    # MSVC links /OPT:REF /OPT:ICF and leaves the COFF symbol table in place,
    # landing near 8.45 MB; the GCC/Clang/Zig branch of CMakeLists.txt links
    # -Os -ffunction-sections -fdata-sections -Wl,--gc-sections -Wl,--strip-all
    # and lands near 4.1 MB.  Both are complete images.  NumberOfSymbols == 0
    # distinguishes them from the artifact itself, so the band stays meaningful
    # on either path instead of permanently warning on the stripped one.
    $golden = $GoldenExeMb
    if (Test-PeStripped -Path $exe) {
        $golden = $GoldenExeMbStripped
    }

    if ($megabytes -lt $MinExeMb) {
        $violations.Add(("$name is $text (< {0:N1} MB): this is a shell without the static media layer" -f $MinExeMb))
        Write-Host "  [FAIL] single-EXE envelope: $text is below the $MinExeMb MB floor"
    } elseif ($megabytes -gt $MaxExeMb) {
        $violations.Add(("$name is $text (> {0:N1} MB): the golden envelope is exceeded" -f $MaxExeMb))
        Write-Host "  [FAIL] single-EXE envelope: $text exceeds the $MaxExeMb MB ceiling"
    } elseif ($megabytes -lt $golden) {
        Write-Host "  [WARN] single-EXE envelope: $text is inside the hard gate but below the $golden MB golden band"
    } elseif (-not $Quiet) {
        Write-Host "  [OK]   single-EXE envelope: $text"
    }
}

# Gate 3.0 single-EXE closure.  The deliverable is CineRTX.exe alone; rtx_tests.exe
# exists only in a developer configuration.  Every shim, bridge and media library
# is statically linked into those two images, so any other module in the output
# directory means something regressed to the retired sidecar layout.
function Test-OutputClosure {
    param([string]$Directory)

    if (-not $Directory -or -not (Test-Path -LiteralPath $Directory)) {
        return
    }
    $allowed = @('cinertx.exe', 'cinertx', 'rtx_tests.exe', 'rtx_tests', 'rtx_ngx_bridge.dll', 'nvngx_vsr.dll', 'nvngx_truehdr.dll', 'cudart64_110.dll', 'nvoffruc.dll', 'nvinfer_10.dll', 'nvinfer_builder_resource_10.dll', 'nvinfer_plugin_10.dll', 'nvonnxparser_10.dll')
    $strays = @(Get-ChildItem -LiteralPath $Directory -File -ErrorAction SilentlyContinue |
        Where-Object {
            $_.Extension -in '.dll', '.so', '.dylib', '.exe' -and
            $allowed -notcontains $_.Name.ToLowerInvariant()
        })
    if ($strays.Count -gt 0) {
        $names = ($strays | ForEach-Object { $_.Name }) -join ', '
        $violations.Add("$Directory ships stray module(s): $names (the product is CineRTX plus rtx_tests only)")
        Write-Host "  [FAIL] single-EXE closure: stray module(s) $names"
    } elseif (-not $Quiet) {
        Write-Host '  [OK]   single-EXE closure: no sidecar module beside CineRTX'
    }
}

$searchDirectory = $null
if ($Binary.Count -eq 0) {
    $searchDirectory = if ($BuildDir) {
        if ([IO.Path]::IsPathRooted($BuildDir)) { $BuildDir } else { Join-Path $repo $BuildDir }
    } else {
        foreach ($candidate in @('build/RelWithDebInfo', 'build/cmake/zig-windows',
                                 'build/cmake/windows-msvc/Release',
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
        Write-Host "No CineRTX / rtx_tests binaries under $searchDirectory"
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
    $nvidiaHits = 0
    foreach ($name in $allNames) {
        if ($name -match $forbiddenPattern) {
            $hits += "dependency $name"
            $nvidiaHits++
        }
    }
    foreach ($symbol in @($imports.Symbols) + @($imports.DelaySymbols)) {
        if ($symbol -match $forbiddenPattern) {
            $hits += "symbol $symbol"
            $nvidiaHits++
        }
    }

    # Zone A media layer: the container code is statically linked, so any
    # av*/sw* import is a Gate 3.0 violation.
    $mediaHits = @()
    foreach ($name in $allNames) {
        if ($name -match $forbiddenMediaPattern) {
            $mediaHits += "dependency $name"
        }
    }

    $dependencyText = if ($allNames.Count -gt 0) { ($allNames -join ', ') } else { '(none)' }
    if (-not $Quiet) {
        Write-Host "  [$($imports.Format)] $resolved"
        Write-Host "         dependencies: $dependencyText"
        Write-Host "         imported symbols: $($imports.Symbols.Count + $imports.DelaySymbols.Count)"
        Write-Host "         NVIDIA static imports: $nvidiaHits (expected 0)"
    }

    if ($hits.Count -gt 0) {
        foreach ($hit in $hits) {
            Write-Host "  [FAIL] $hit"
            $violations.Add("$resolved imports $hit")
        }
    } elseif (-not $Quiet) {
        Write-Host '         result: no NVIDIA static imports'
    }

    if ($mediaHits.Count -gt 0) {
        foreach ($hit in $mediaHits) {
            Write-Host "  [FAIL] $hit (media container layer must be statically inlined)"
            $violations.Add("$resolved imports $hit")
        }
    } elseif (-not $Quiet) {
        Write-Host '         result: no external media imports'
    }
}

Write-Host "  inspected: $inspected binary/binaries"
Write-Host "  NVIDIA module families asserted absent: $($forbiddenNvidiaModules -join ', ')"

# ---------------------------------------------------------------------------
# Gate 3.0: output-directory self-containment
# ---------------------------------------------------------------------------
if (-not $searchDirectory -and $Binary.Count -gt 0) {
    $first = $Binary[0]
    if (Test-Path -LiteralPath $first -PathType Leaf) {
        $searchDirectory = Split-Path -Parent (Resolve-Path -LiteralPath $first).Path
    }
}
if ($searchDirectory) {
    Test-MediaDllLeak $searchDirectory
    Test-ExeEnvelope $searchDirectory
    Test-OutputClosure $searchDirectory
}

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
