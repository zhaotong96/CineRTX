// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors

#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <string>
#include <vector>

#include "test_framework.h"

namespace {

void printUsage(const char* executable) {
    std::cout << "usage: " << executable << " [--list] [--filter TEXT]\n";
}

}  // namespace

int main(int argc, char** argv) {
    bool list_only = false;
    std::string filter;
    for (int i = 1; i < argc; ++i) {
        const std::string argument = argv[i];
        if (argument == "--list") {
            list_only = true;
        } else if (argument == "--filter" && i + 1 < argc) {
            filter = argv[++i];
        } else if (argument.rfind("--filter=", 0) == 0) {
            filter = argument.substr(std::strlen("--filter="));
        } else if (argument == "--help" || argument == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "unknown argument: " << argument << "\n";
            printUsage(argv[0]);
            return 2;
        }
    }

    auto& cases = rtx::test::registry();
    std::stable_sort(cases.begin(), cases.end(),
                     [](const rtx::test::TestCase& a, const rtx::test::TestCase& b) {
                         return a.name < b.name;
                     });

    if (list_only) {
        for (const auto& test : cases) {
            std::cout << test.name << "\n";
        }
        return 0;
    }

    size_t run = 0;
    size_t failed = 0;
    const auto start = std::chrono::steady_clock::now();
    for (const auto& test : cases) {
        if (!filter.empty() && test.name.find(filter) == std::string::npos) {
            continue;
        }
        ++run;
        rtx::test::Context context;
        std::cout << "[ RUN      ] " << test.name << std::endl;
        try {
            test.function(context);
        } catch (const std::exception& error) {
            context.failures.push_back(std::string("uncaught std::exception: ") + error.what());
        } catch (...) {
            context.failures.push_back("uncaught unknown exception");
        }
        if (context.failures.empty()) {
            std::cout << "[       OK ] " << test.name << std::endl;
        } else {
            ++failed;
            std::cout << "[  FAILED  ] " << test.name << std::endl;
            for (const auto& failure : context.failures) {
                std::cout << "             " << failure << "\n";
            }
        }
    }
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    std::cout << "[==========] " << run << " test(s) run, " << failed << " failed ("
              << elapsed << " ms)\n";
    if (run == 0) {
        std::cout << "[  FAILED  ] no tests matched";
        if (!filter.empty()) {
            std::cout << " filter '" << filter << "'";
        }
        std::cout << "\n";
        return 1;
    }
    return failed == 0 ? 0 : 1;
}
