// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 9 (Task 9-B1, gate 9.4): JSON-RPC 2.0 daemon contract tests.  The
// dispatcher is exercised in-process (rtx.probe / rtx.submit_job /
// rtx.query_job / rtx.cancel_job round trips plus malformed-request
// defence); the HTTP transport loop is exercised on the acceptance host.

#include "test_framework.h"

#include <string>

#include "rtx/daemon_rpc.h"

using namespace rtx::daemon;

namespace {

bool contains(const std::string& haystack, const std::string& needle) {
    return haystack.find(needle) != std::string::npos;
}

}  // namespace

RTX_TEST(daemon_rpc_probe_round_trip) {
    const std::string response = dispatchRequest(
        R"({"jsonrpc": "2.0", "id": 1, "method": "rtx.probe", "params": {}})");
    RTX_CHECK(contains(response, "\"jsonrpc\": \"2.0\""));
    RTX_CHECK(contains(response, "\"id\": 1"));
    RTX_CHECK(contains(response, "\"result\""));
    RTX_CHECK(contains(response, "probe_json"));
}

RTX_TEST(daemon_rpc_submit_query_cancel_lifecycle) {
    // submit_job without a real GPU still registers the job and returns an id;
    // the worker fails fast when the shim cannot load, which the query path
    // reports as finished/ok=false - the lifecycle (submit -> query ->
    // cancel) must be complete and defensive either way.
    const std::string submit = dispatchRequest(
        R"({"jsonrpc": "2.0", "id": 2, "method": "rtx.submit_job",)"
        R"("params": {"input_path": "in.mp4", "output_path": "out.mp4"}})");
    RTX_CHECK(contains(submit, "\"job_id\""));
    const size_t key = submit.find("job-");
    RTX_REQUIRE(key != std::string::npos);
    std::string job_id;
    for (size_t i = key; i < submit.size() && submit[i] != '"'; ++i) {
        job_id += submit[i];
    }
    RTX_CHECK(!job_id.empty());

    const std::string query = dispatchRequest(
        "{\"jsonrpc\": \"2.0\", \"id\": 3, \"method\": \"rtx.query_job\","
        "\"params\": {\"job_id\": \"" + job_id + "\"}}");
    RTX_CHECK(contains(query, "\"job_id\": \"" + job_id + "\""));
    RTX_CHECK(contains(query, "\"progress_permille\""));
    RTX_CHECK(contains(query, "\"finished\""));

    const std::string cancel = dispatchRequest(
        "{\"jsonrpc\": \"2.0\", \"id\": 4, \"method\": \"rtx.cancel_job\","
        "\"params\": {\"job_id\": \"" + job_id + "\"}}");
    RTX_CHECK(contains(cancel, "\"cancelling\": true"));

    // Unknown job ids produce a JSON-RPC error envelope, not a crash.
    const std::string unknown = dispatchRequest(
        R"({"jsonrpc": "2.0", "id": 5, "method": "rtx.query_job",)"
        R"("params": {"job_id": "does-not-exist"}})");
    RTX_CHECK(contains(unknown, "\"error\""));
    RTX_CHECK(contains(unknown, "-32001"));
}

RTX_TEST(daemon_rpc_malformed_request_defence) {
    // Unknown method.
    const std::string unknown_method = dispatchRequest(
        R"({"jsonrpc": "2.0", "id": 9, "method": "rtx.does_not_exist"})");
    RTX_CHECK(contains(unknown_method, "-32601"));
    // Missing fields / garbage JSON must produce an error envelope, never an
    // exception.
    const std::string garbage = dispatchRequest("this is not json at all");
    RTX_CHECK(contains(garbage, "\"error\""));
    const std::string missing_params = dispatchRequest(
        R"({"jsonrpc": "2.0", "id": 10, "method": "rtx.submit_job"})");
    RTX_CHECK(contains(missing_params, "-32602"));
    // submit_job with empty paths is rejected with the parameter error.
    const std::string empty_paths = dispatchRequest(
        R"({"jsonrpc": "2.0", "id": 11, "method": "rtx.submit_job",)"
        R"("params": {"input_path": "", "output_path": ""}})");
    RTX_CHECK(contains(empty_paths, "-32602"));
}
