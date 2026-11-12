// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 9 (Task 9-B1): zero-dependency JSON-RPC 2.0 control plane.
//
// Two transports share one dispatcher (verdict §4.5: no gRPC/Protobuf, no
// Boost, no libcurl - the ASCII JSON serializer and a 120-line socket loop
// stay under the 280 LOC red line):
//   --daemon-stdio        newline-delimited JSON-RPC on stdin/stdout
//   --daemon-http <port>  JSON-RPC 2.0 over a minimal HTTP/1.1 POST endpoint
//
// Methods: rtx.probe / rtx.submit_job / rtx.query_job / rtx.cancel_job.

#pragma once

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace rtx::daemon {

struct JobRecord {
    std::string job_id;
    std::string input_path;
    std::string output_path;
    std::atomic<bool> cancel{false};
    bool finished = false;
    bool ok = false;
    double progress_permille = 0.0;
    double fps = 0.0;
    int64_t frames_encoded = 0;
    std::string report_json;  // HwTranscodeReport::toJson() when finished
};

// One shared job table (the daemon runs jobs on detached worker threads).
class JobTable {
public:
    static JobTable& instance();

    std::string submit(const std::string& input, const std::string& output);
    JobRecord* find(const std::string& job_id);
    std::vector<std::string> jobIds() const;
    void reapFinished();

private:
    JobTable() = default;
    mutable std::mutex mutex_;
    std::vector<std::unique_ptr<JobRecord>> jobs_;
    uint64_t next_id_ = 1;
};

// Dispatches one JSON-RPC request (text) to one JSON-RPC response (text).
// Never throws; malformed requests yield a JSON-RPC error envelope.
std::string dispatchRequest(const std::string& request);

// --daemon-stdio: newline-delimited JSON-RPC loop on the standard streams.
// Returns the process exit code.
int runStdio();

// --daemon-http <port>: minimal HTTP/1.1 JSON-RPC endpoint.  Returns the
// process exit code (non-zero when the socket cannot be bound).
int runHttp(uint16_t port);

}  // namespace rtx::daemon
