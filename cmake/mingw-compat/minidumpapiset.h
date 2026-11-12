// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Minimal <minidumpapiset.h> compatibility shim for the Zig/mingw-w64 Windows
// target.  The mingw-w64 headers bundled with Zig do not ship the Windows SDK
// Minidump API declarations, so cmake/toolchain-zig.cmake appends this
// directory to CMAKE_CXX_STANDARD_INCLUDE_DIRECTORIES.  MSVC builds never see
// it: the Visual Studio environment resolves <minidumpapiset.h> from the real
// Windows SDK long before the standard-include fallback applies.

#ifndef RTX_MINGW_COMPAT_MINIDUMPAPISET_H
#define RTX_MINGW_COMPAT_MINIDUMPAPISET_H

#include <windows.h>

#ifndef _MINIDUMPAPISET_H_  // avoid double definition if the SDK ever appears
#define _MINIDUMPAPISET_H_

#ifdef __cplusplus
extern "C" {
#endif

// MINIDUMP_TYPE flags (WinDbg-compatible subset; runtime_env.cpp uses
// MiniDumpNormal only, the rest keeps the enum faithful to the SDK).
typedef enum _MINIDUMP_TYPE {
    MiniDumpNormal = 0x00000000,
    MiniDumpWithDataSegs = 0x00000001,
    MiniDumpWithFullMemory = 0x00000002,
    MiniDumpWithHandleData = 0x00000004,
    MiniDumpFilterMemory = 0x00000008,
    MiniDumpScanMemory = 0x00000010,
    MiniDumpWithUnloadedModules = 0x00000020,
    MiniDumpWithIndirectlyReferencedMemory = 0x00000040,
    MiniDumpFilterModulePaths = 0x00000080,
    MiniDumpWithProcessThreadData = 0x00000100,
    MiniDumpWithPrivateReadWriteMemory = 0x00000200,
    MiniDumpWithoutOptionalData = 0x00000400,
    MiniDumpWithFullMemoryInfo = 0x00000800,
    MiniDumpWithThreadInfo = 0x00001000,
    MiniDumpWithCodeSegs = 0x00002000,
    MiniDumpWithoutAuxiliaryState = 0x00004000,
    MiniDumpWithFullAuxiliaryState = 0x00008000,
    MiniDumpWithPrivateWriteWatchMemory = 0x00010000,
    MiniDumpIgnoreInaccessibleMemory = 0x00020000,
    MiniDumpWithValidDataRegions = 0x00040000,
    MiniDumpWithAvxState = 0x00100000,
    MiniDumpWithAuxiliaryModuleState = 0x00200000,
    MiniDumpWithProcessData = 0x00400000
} MINIDUMP_TYPE;

typedef struct _MINIDUMP_EXCEPTION_INFORMATION {
    DWORD ThreadId;
    PEXCEPTION_POINTERS ExceptionPointers;
    BOOL ClientPointers;
} MINIDUMP_EXCEPTION_INFORMATION, *PMINIDUMP_EXCEPTION_INFORMATION;

typedef struct _MINIDUMP_USER_STREAM {
    ULONG32 Type;
    ULONG BufferSize;
    PVOID Buffer;
} MINIDUMP_USER_STREAM, *PMINIDUMP_USER_STREAM;

typedef struct _MINIDUMP_USER_STREAM_INFORMATION {
    ULONG UserStreamCount;
    PMINIDUMP_USER_STREAM UserStreamArray;
} MINIDUMP_USER_STREAM_INFORMATION, *PMINIDUMP_USER_STREAM_INFORMATION;

typedef struct _MINIDUMP_CALLBACK_OUTPUT {
    union {
        ULONG ModuleWriteFlags;
        ULONG ThreadWriteFlags;
        ULONG MemoryWriteFlags;
        struct {
            ULONG64 DataSize;
            ULONG64 VirtualAddress;
        } Memory;
        BOOL CheckCancel;
    };
    HANDLE Handle;
    struct {
        ULONG64 PageFaultVirtualAddress;
    } GetVmQueryResult;
} MINIDUMP_CALLBACK_OUTPUT, *PMINIDUMP_CALLBACK_OUTPUT;

typedef struct _MINIDUMP_CALLBACK_INPUT {
    ULONG ProcessId;
    HANDLE ProcessHandle;
    ULONG CallbackType;
    union {
        MINIDUMP_CALLBACK_OUTPUT Status;
    };
} MINIDUMP_CALLBACK_INPUT, *PMINIDUMP_CALLBACK_INPUT;

typedef BOOL(WINAPI* MINIDUMP_CALLBACK_ROUTINE)(
    PVOID CallbackParam, PMINIDUMP_CALLBACK_INPUT CallbackInput,
    PMINIDUMP_CALLBACK_OUTPUT CallbackOutput);

typedef struct _MINIDUMP_CALLBACK_INFORMATION {
    MINIDUMP_CALLBACK_ROUTINE CallbackRoutine;
    PVOID CallbackParam;
} MINIDUMP_CALLBACK_INFORMATION, *PMINIDUMP_CALLBACK_INFORMATION;

#ifdef __cplusplus
}  // extern "C"
#endif

#endif  // _MINIDUMPAPISET_H_

#endif  // RTX_MINGW_COMPAT_MINIDUMPAPISET_H
