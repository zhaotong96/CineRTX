// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
// Copyright (C) 2026 RTX Transcode Engine contributors
//
// Phase 9 (Task 9-B1) implementation: zero-dependency JSON-RPC 2.0 control
// plane.  See include/rtx/daemon_rpc.h for the contract.  The file holds the
// shared dispatcher (probe / submit / query / cancel), the stdio loop and a
// minimal HTTP/1.1 POST endpoint built directly on WinSock2/POSIX sockets.

#include "rtx/daemon_rpc.h"

#include <atomic>
#include <chrono>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <sstream>
#include <thread>

#include "rtx/hw_transcode.h"
#include "rtx/nvidia_shim.h"
#include "shim_probe.h"

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#define DAEMON_CLOSE(s) closesocket(s)
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#define DAEMON_CLOSE(s) ::close(s)
#endif

namespace rtx::daemon {
namespace {

std::string jsonString(const std::string& key, const std::string& value) {
    std::string escaped;
    escaped.reserve(value.size());
    for (char raw : value) {
        switch (raw) {
            case '"': escaped += "\\\""; break;
            case '\\': escaped += "\\\\"; break;
            case '\n': escaped += "\\n"; break;
            case '\r': escaped += "\\r"; break;
            case '\t': escaped += "\\t"; break;
            default:
                if (static_cast<unsigned char>(raw) < 0x20) {
                    char buffer[8];
                    std::snprintf(buffer, sizeof(buffer), "\\u%04x", raw);
                    escaped += buffer;
                } else {
                    escaped += raw;
                }
        }
    }
    return "\"" + key + "\": \"" + escaped + "\"";
}

std::string jsonNumber(const std::string& key, double value) {
    std::ostringstream out;
    out.precision(6);
    out << std::fixed << "\"" << key << "\": " << value;
    return out.str();
}

// Minimal field extraction for the ASCII JSON subset the daemon accepts.
std::string findStringField(const std::string& json, const std::string& key) {
    const std::string needle = "\"" + key + "\"";
    size_t position = json.find(needle);
    if (position == std::string::npos) {
        return {};
    }
    position = json.find('"', position + needle.size());
    if (position == std::string::npos) {
        return {};
    }
    ++position;
    std::string value;
    while (position < json.size() && json[position] != '"') {
        if (json[position] == '\\' && position + 1 < json.size()) {
            ++position;
            switch (json[position]) {
                case 'n': value += '\n'; break;
                case 't': value += '\t'; break;
                case 'r': value += '\r'; break;
                default: value += json[position];
            }
        } else {
            value += json[position];
        }
        ++position;
    }
    return value;
}

std::string errorEnvelope(const std::string& idText, int code, const std::string& message) {
    return "{\n  \"jsonrpc\": \"2.0\",\n  \"id\": " + (idText.empty() ? "null" : idText) +
           ",\n  \"error\": {\"code\": " + std::to_string(code) +
           ", \"message\": \"" + message + "\"}\n}\n";
}

}  // namespace

JobTable& JobTable::instance() {
    static JobTable table;
    return table;
}

std::string JobTable::submit(const std::string& input, const std::string& output) {
    std::lock_guard<std::mutex> lock(mutex_);
    const std::string job_id = "job-" + std::to_string(next_id_++);
    auto record = std::make_unique<JobRecord>();
    record->job_id = job_id;
    record->input_path = input;
    record->output_path = output;
    JobRecord* raw = record.get();
    jobs_.push_back(std::move(record));

    // Detached worker: one NvidiaTranscodeSession per job, progress mirrored
    // into the record, cancel honoured through the shared atomic.
    std::thread([raw]() {
        HwTranscodeOptions options;
        options.input_path = std::filesystem::u8path(raw->input_path);
        options.output_path = std::filesystem::u8path(raw->output_path);
        options.cancel = &raw->cancel;
        options.progress = [raw](const HwTranscodeProgress& progress) {
            if (progress.total_frames > 0) {
                raw->progress_permille =
                    static_cast<double>(progress.frames_encoded) * 1000.0 /
                    static_cast<double>(progress.total_frames);
            }
            raw->fps = progress.elapsed_ms > 0
                           ? static_cast<double>(progress.frames_encoded) * 1000.0 /
                                 progress.elapsed_ms
                           : 0.0;
            raw->frames_encoded = progress.frames_encoded;
        };
        auto result = runNvidiaRemasterTranscode(options);
        raw->finished = true;
        raw->ok = result.has_value();
        if (result.has_value()) {
            raw->progress_permille = 1000.0;
            raw->report_json = result.value().toJson();
        }
    }).detach();
    return job_id;
}

JobRecord* JobTable::find(const std::string& job_id) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& job : jobs_) {
        if (job->job_id == job_id) {
            return job.get();
        }
    }
    return nullptr;
}

std::vector<std::string> JobTable::jobIds() const {
    std::lock_guard<std::mutex> lock(mutex_);
    std::vector<std::string> ids;
    for (const auto& job : jobs_) {
        ids.push_back(job->job_id);
    }
    return ids;
}

void JobTable::reapFinished() {
    std::lock_guard<std::mutex> lock(mutex_);
    jobs_.erase(std::remove_if(jobs_.begin(), jobs_.end(),
                               [](const std::unique_ptr<JobRecord>& job) {
                                   return job->finished;
                               }),
                jobs_.end());
}

std::string dispatchRequest(const std::string& request) {
    const std::string method = findStringField(request, "method");
    std::string id_text;
    {
        const size_t id_position = request.find("\"id\"");
        if (id_position != std::string::npos) {
            const size_t colon = request.find(':', id_position);
            const size_t comma = request.find_first_of(",}\n", colon);
            if (colon != std::string::npos && comma != std::string::npos) {
                id_text = request.substr(colon + 1, comma - colon - 1);
                id_text.erase(std::remove_if(id_text.begin(), id_text.end(), ::isspace),
                              id_text.end());
            }
        }
    }
    const std::string response_id = id_text.empty() ? "null" : id_text;
    auto ok = [&response_id](const std::string& result) {
        return "{\n  \"jsonrpc\": \"2.0\",\n  \"id\": " + response_id +
               ",\n  \"result\": " + result + "\n}\n";
    };

    if (method == "rtx.probe") {
        const cli::ShimProbeResult probe = cli::probeShim();
        std::ostringstream out;
        out << "{\n  " << jsonString("probe_json", cli::formatProbeJson(probe)) << ",\n  "
            << jsonNumber("loaded", probe.loaded ? 1.0 : 0.0) << "\n}";
        return ok(out.str());
    }
    if (method == "rtx.submit_job") {
        const std::string input = findStringField(request, "input_path");
        const std::string output = findStringField(request, "output_path");
        if (input.empty() || output.empty()) {
            return errorEnvelope(response_id, -32602, "input_path and output_path are required");
        }
        JobTable::instance().reapFinished();
        const std::string job_id = JobTable::instance().submit(input, output);
        return ok("{\n  " + jsonString("job_id", job_id) + "\n}");
    }
    if (method == "rtx.query_job") {
        const std::string job_id = findStringField(request, "job_id");
        JobRecord* job = JobTable::instance().find(job_id);
        if (job == nullptr) {
            return errorEnvelope(response_id, -32001, "unknown job_id: " + job_id);
        }
        std::ostringstream out;
        out << "{\n  " << jsonString("job_id", job->job_id) << ",\n  "
            << jsonNumber("progress_permille", job->progress_permille) << ",\n  "
            << jsonNumber("fps", job->fps) << ",\n  "
            << "\"frames_encoded\": " << job->frames_encoded << ",\n  "
            << "\"finished\": " << (job->finished ? "true" : "false") << ",\n  "
            << "\"ok\": " << (job->ok ? "true" : "false") << ",\n  "
            << "\"cancelled\": " << (job->cancel.load() ? "true" : "false");
        if (!job->report_json.empty()) {
            out << ",\n  " << jsonString("report_json", job->report_json);
        }
        out << "\n}";
        return ok(out.str());
    }
    if (method == "rtx.cancel_job") {
        const std::string job_id = findStringField(request, "job_id");
        JobRecord* job = JobTable::instance().find(job_id);
        if (job == nullptr) {
            return errorEnvelope(response_id, -32001, "unknown job_id: " + job_id);
        }
        job->cancel.store(true, std::memory_order_release);
        return ok("{\n  " + jsonString("job_id", job->job_id) + ",\n  \"cancelling\": true\n}");
    }
    return errorEnvelope(response_id, -32601, "unknown method: " + method);
}

int runStdio() {
    std::string line;
    while (std::getline(std::cin, line)) {
        if (line.empty()) {
            continue;
        }
        const std::string response = dispatchRequest(line);
        std::cout << response << std::flush;
        JobTable::instance().reapFinished();
    }
    return 0;
}

int runHttp(uint16_t port) {
#ifdef _WIN32
    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::cerr << "daemon: WSAStartup failed\n";
        return 1;
    }
    struct WsaCleanup {
        ~WsaCleanup() { WSACleanup(); }
    } wsa_cleanup;
#endif
    const int socket_fd = static_cast<int>(::socket(AF_INET, SOCK_STREAM, 0));
    if (socket_fd < 0) {
        std::cerr << "daemon: cannot create the listening socket\n";
        return 1;
    }
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = htons(port);
    if (::bind(socket_fd, reinterpret_cast<sockaddr*>(&address), sizeof(address)) != 0) {
        std::cerr << "daemon: cannot bind 127.0.0.1:" << port << "\n";
        return 1;
    }
    if (::listen(socket_fd, 8) != 0) {
        std::cerr << "daemon: listen failed\n";
        return 1;
    }
    std::cout << "daemon: JSON-RPC 2.0 over HTTP on 127.0.0.1:" << port << "\n" << std::flush;
    for (;;) {
        const int connection = static_cast<int>(::accept(socket_fd, nullptr, nullptr));
        if (connection < 0) {
            continue;
        }
        // One request per connection: read headers, locate the body, dispatch.
        std::string buffer;
        char chunk[4096];
        size_t body_start = std::string::npos;
        size_t content_length = 0;
        while (body_start == std::string::npos) {
            const int received = ::recv(connection, chunk, sizeof(chunk), 0);
            if (received <= 0) {
                break;
            }
            buffer.append(chunk, static_cast<size_t>(received));
            body_start = buffer.find("\r\n\r\n");
            const size_t cl = buffer.find("Content-Length:");
            if (cl != std::string::npos) {
                content_length = std::strtoul(buffer.c_str() + cl + 15, nullptr, 10);
            }
            (void)content_length;
        }
        if (body_start == std::string::npos) {
            DAEMON_CLOSE(connection);
            continue;
        }
        const std::string body = buffer.substr(body_start + 4);
        const std::string response = dispatchRequest(body);
        std::ostringstream http;
        http << "HTTP/1.1 200 OK\r\n"
             << "Content-Type: application/json\r\n"
             << "Content-Length: " << response.size() << "\r\n"
             << "Connection: close\r\n\r\n"
             << response;
        const std::string wire = http.str();
        (void)::send(connection, wire.c_str(), static_cast<int>(wire.size()), 0);
        DAEMON_CLOSE(connection);
        JobTable::instance().reapFinished();
    }
    return 0;
}

}  // namespace rtx::daemon
