/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 *
 * MSVC CRT compatibility layer for linking NVIDIA's nvsdk_ngx_s.lib with the
 * MinGW/Zig toolchain.  The stub is compiled with /MT + /GS and only needs the
 * static-local guard helpers, the stack-cookie helpers, and its operator new /
 * delete, so a handful of shims is sufficient.  Nothing here replaces the
 * vendor binary; it is only the CRT surface the COFF objects reference.
 */

#if !defined(_MSC_VER)

#include <windows.h>

#include <cstdint>
#include <cstdlib>
#include <new>
#include <stdexcept>

extern "C" int _fltused = 1;

extern "C" __thread int _Init_thread_epoch = 0;

extern "C" void _Init_thread_header(int* guard) {
    for (;;) {
        const LONG state = InterlockedCompareExchange(
            reinterpret_cast<volatile LONG*>(guard), -1, 0);
        if (state == 0 || state != -1) {
            return;
        }
        Sleep(0);
    }
}

extern "C" void _Init_thread_footer(int* guard) {
    InterlockedExchange(reinterpret_cast<volatile LONG*>(guard), 1);
}

extern "C" void _Init_thread_abort(int* guard) {
    InterlockedExchange(reinterpret_cast<volatile LONG*>(guard), 0);
}

extern "C" uintptr_t __security_cookie = 0x2B992DDFA232ULL;

extern "C" void __security_check_cookie(uintptr_t value) {
    if (value != __security_cookie) {
        abort();
    }
}

extern "C" int __GSHandlerCheck(void*, void*, void*, void*) { return 1; }

extern "C" int __GSHandlerCheck_EH(void*, void*, void*, void*) { return 1; }

extern "C" void* ngx_msvc_operator_new(unsigned long long size)
    __asm__("??2@YAPEAX_K@Z");
extern "C" void* ngx_msvc_operator_new(unsigned long long size) {
    void* memory = std::malloc(size == 0 ? 1 : static_cast<size_t>(size));
    if (memory == nullptr) {
        throw std::bad_alloc();
    }
    return memory;
}

extern "C" void ngx_msvc_operator_delete(void* memory) __asm__("??3@YAXPEAX@Z");
extern "C" void ngx_msvc_operator_delete(void* memory) { std::free(memory); }

extern "C" void ngx_msvc_bad_alloc() __asm__("?_Xbad_alloc@std@@YAXXZ");
extern "C" void ngx_msvc_bad_alloc() { throw std::bad_alloc(); }

extern "C" void ngx_msvc_length_error(const char* message)
    __asm__("?_Xlength_error@std@@YAXPEBD@Z");
extern "C" void ngx_msvc_length_error(const char* message) {
    throw std::length_error(message);
}

#endif  // !defined(_MSC_VER)
