// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#pragma once

namespace rtx {

// Unified control-plane application lifecycle & environment bootstrap.
// Enforces UTF-8 console/locale setup and attaches Minidump crash handlers.
void initRuntimeEnvironment() noexcept;

}  // namespace rtx
