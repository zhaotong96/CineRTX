// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#if defined(_MSC_VER)

#include <corecrt.h>
#include <process.h>
#include <cstdlib>
#include <ctime>
#include <io.h>
#include <direct.h>
#include <sys/stat.h>
#include <cstring>
#include <cwchar>

#define BIND_RAW(name, ptr) extern "C" void* __imp_##name = (void*)(ptr);

// C CRT memory & io
BIND_RAW(_beginthreadex, &_beginthreadex)
BIND_RAW(_aligned_free, &_aligned_free)
BIND_RAW(_aligned_malloc, &_aligned_malloc)
BIND_RAW(_aligned_realloc, &_aligned_realloc)
BIND_RAW(_dup, &_dup)
BIND_RAW(dup, &_dup)
BIND_RAW(_fdopen, &_fdopen)
BIND_RAW(fdopen, &_fdopen)
BIND_RAW(_fstat64, &_fstat64)
BIND_RAW(_gmtime64, &_gmtime64)
BIND_RAW(_localtime64, &_localtime64)
BIND_RAW(_mkdir, &_mkdir)
BIND_RAW(_mktime64, &_mktime64)
BIND_RAW(_stat64, &_stat64)
BIND_RAW(_tempnam, &_tempnam)
BIND_RAW(tempnam, &_tempnam)
BIND_RAW(_wmkdir, &_wmkdir)
BIND_RAW(_wstat64, &_wstat64)
BIND_RAW(bsearch, &bsearch)
BIND_RAW(clock, &clock)
BIND_RAW(strspn, &strspn)
BIND_RAW(wcscat, &wcscat)
BIND_RAW(wcscpy, &wcscpy)
BIND_RAW(close, &_close)
BIND_RAW(isatty, &_isatty)
BIND_RAW(read, &_read)
BIND_RAW(setmode, &_setmode)
BIND_RAW(write, &_write)

// Math functions with disambiguated C function pointers
extern "C" {
    float __cdecl atan2f(float, float);
    float __cdecl atanf(float);
    double __cdecl cbrt(double);
    float __cdecl cbrtf(float);
    double __cdecl copysign(double, double);
    float __cdecl cosf(float);
    double __cdecl exp2(double);
    float __cdecl exp2f(float);
    float __cdecl expf(float);
    double __cdecl hypot(double, double);
    double __cdecl ldexp(double, int);
    long long __cdecl llrint(double);
    long long __cdecl llrintf(float);
    float __cdecl log2f(float);
    float __cdecl logf(float);
    long __cdecl lrint(double);
    long __cdecl lrintf(float);
    float __cdecl powf(float, float);
    double __cdecl rint(double);
    double __cdecl scalbn(double, int);
    float __cdecl sinf(float);
    float __cdecl sqrtf(float);
    float __cdecl tanf(float);
    double __cdecl trunc(double);

    int __cdecl __stdio_common_vsscanf(
        unsigned __int64 options,
        char const* input,
        size_t length,
        char const* format,
        _locale_t locale,
        va_list arglist);
}

BIND_RAW(atan2f, &atan2f)
BIND_RAW(atanf, &atanf)
BIND_RAW(cbrt, &cbrt)
BIND_RAW(cbrtf, &cbrtf)
BIND_RAW(copysign, &copysign)
BIND_RAW(cosf, &cosf)
BIND_RAW(exp2, &exp2)
BIND_RAW(exp2f, &exp2f)
BIND_RAW(expf, &expf)
BIND_RAW(hypot, &hypot)
BIND_RAW(ldexp, &ldexp)
BIND_RAW(llrint, &llrint)
BIND_RAW(llrintf, &llrintf)
BIND_RAW(log2f, &log2f)
BIND_RAW(logf, &logf)
BIND_RAW(lrint, &lrint)
BIND_RAW(lrintf, &lrintf)
BIND_RAW(powf, &powf)
BIND_RAW(rint, &rint)
BIND_RAW(scalbn, &scalbn)
BIND_RAW(sinf, &sinf)
BIND_RAW(sqrtf, &sqrtf)
BIND_RAW(tanf, &tanf)
BIND_RAW(trunc, &trunc)
BIND_RAW(__stdio_common_vsscanf, &__stdio_common_vsscanf)

#endif
