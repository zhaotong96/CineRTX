/* SPDX-License-Identifier: MIT OR Apache-2.0
 * Copyright (c) 2026 RTX Transcode Engine contributors
 */

#include "dyn_library.h"

#if !defined(_WIN32)

#include <dlfcn.h>

namespace rtx_shim {

std::string DynLibrary::platformName() { return "posix"; }

bool DynLibrary::openSystemLibrary(const std::string& name, std::string* error) {
    return openExplicitPath(name, error);
}

bool DynLibrary::openExplicitPath(const std::string& path, std::string* error) {
    close();
    if (path.empty()) {
        last_error_ = "empty library name";
        if (error != nullptr) {
            *error = last_error_;
        }
        return false;
    }
    dlerror();
    void* handle = dlopen(path.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (handle == nullptr) {
        const char* message = dlerror();
        last_error_ = message != nullptr ? message : "dlopen failed";
        if (error != nullptr) {
            *error = last_error_;
        }
        return false;
    }
    handle_ = handle;
    path_ = path;
    last_error_.clear();
    return true;
}

void DynLibrary::close() {
    if (handle_ != nullptr) {
        dlclose(handle_);
        handle_ = nullptr;
    }
    path_.clear();
    last_error_.clear();
}

void* DynLibrary::symbol(const char* name) const noexcept {
    if (handle_ == nullptr || name == nullptr) {
        return nullptr;
    }
    dlerror();
    void* address = dlsym(handle_, name);
    if (address == nullptr) {
        // dlsym may legitimately return null for a symbol whose value is zero;
        // call dlerror to distinguish that from a missing symbol.  Callers only
        // use non-null function pointers, so treat null as missing.
        (void)dlerror();
    }
    return address;
}

}  // namespace rtx_shim

#endif  // !_WIN32
