// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Strongly-typed localisation contract (W7-3).

#include "test_framework.h"

#include <chrono>
#include <string>

#include "rtx/i18n.h"
#include "rtx/status.h"

using namespace rtx;

RTX_TEST(i18n_table_bounds) {
    RTX_CHECK_EQ(static_cast<size_t>(StrId::Count), static_cast<size_t>(64));
    RTX_CHECK_EQ(static_cast<size_t>(Language::Count), static_cast<size_t>(2));

    const Language saved = currentLanguage();
    for (Language language : {Language::En, Language::Zh}) {
        setLanguage(language);
        RTX_CHECK(currentLanguage() == language);
        for (size_t index = 0; index < static_cast<size_t>(StrId::Count); ++index) {
            const StrId id = static_cast<StrId>(index);
            const wchar_t* wide = trW(id);
            RTX_REQUIRE(wide != nullptr);
            // Every entry must be non-empty in both languages: a missing
            // translation is a build error, but this catches a stray L"".
            RTX_CHECK(wide[0] != L'\0');
            RTX_CHECK(!tr(id).empty());
            RTX_CHECK(strIdName(id) != std::string("Unknown"));
        }
    }

    // Out-of-range ids return an empty string rather than reading past the
    // table.  This is the only defensive branch on the paint path.
    RTX_CHECK(trW(static_cast<StrId>(9999))[0] == L'\0');
    RTX_CHECK_EQ(std::string(strIdName(static_cast<StrId>(9999))), std::string("Unknown"));

    // The two languages must actually differ: a copy-paste table would pass
    // every other check in this test.
    setLanguage(Language::En);
    const std::string english = tr(StrId::AppTitle);
    setLanguage(Language::Zh);
    const std::string chinese = tr(StrId::AppTitle);
    RTX_CHECK(english != chinese);
    RTX_CHECK(!english.empty());
    RTX_CHECK(!chinese.empty());
    setLanguage(saved);
}

RTX_TEST(i18n_switch_latency) {
    // A language flip is one relaxed atomic store plus an InvalidateRect; the
    // lookup itself is a single indexed load.  The budget is 1.0 us averaged
    // over a full table sweep, which is what the paint path actually does.
    const Language saved = currentLanguage();
    constexpr int kIterations = 2000;
    volatile size_t sink = 0;

    const auto start = std::chrono::steady_clock::now();
    for (int iteration = 0; iteration < kIterations; ++iteration) {
        const Language language = (iteration & 1) == 0 ? Language::En : Language::Zh;
        setLanguage(language);
        for (size_t index = 0; index < static_cast<size_t>(StrId::Count); ++index) {
            const wchar_t* text = trW(static_cast<StrId>(index));
            sink = sink + (text != nullptr ? 1u : 0u);
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::nanoseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();

    const double per_lookup_ns =
        static_cast<double>(elapsed) / (static_cast<double>(kIterations) *
                                        static_cast<double>(StrId::Count));
    RTX_CHECK(per_lookup_ns < 1000.0);  // < 1.0 us per entry
    RTX_CHECK(sink > 0);

    // Language parsing is the CLI boundary and must reject nonsense.
    RTX_CHECK(parseLanguage("zh-CN").ok());
    RTX_CHECK(parseLanguage("EN").ok());
    RTX_CHECK(parseLanguage("").ok());
    RTX_CHECK_EQ(parseLanguage("klingon").code(), ErrorCode::InvalidArgument);
    RTX_CHECK(parseLanguage("zh").value() == Language::Zh);
    RTX_CHECK(parseLanguage("en").value() == Language::En);

    setLanguage(saved);
}
