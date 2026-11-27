// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <filesystem>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <thread>
#include <tuple>
#include <unordered_map>
#include <vector>

#include "rtx/hw_transcode.h"
#include "rtx/param_schema.h"
#include "rtx/status.h"

namespace rtx {

enum class JobOutputMode : uint8_t {
    BatchSeparate = 0,  // Independent .mp4 per input
    MergeConcat   = 1,  // Seamless single-.mp4 concat/merge
};

enum class JobStatus : uint8_t {
    Pending   = 0,  // Queued; profile and file list are freely editable
    Running   = 1,  // Actively transcoding on worker thread
    Completed = 2,  // All input files in this job finished with RTX_OK
    Failed    = 3,  // Stopped on error (error_message populated; retryable)
    Cancelled = 4,  // Cancelled or skipped by user
};

[[nodiscard]] constexpr std::string_view jobStatusName(JobStatus s) noexcept {
    switch (s) {
        case JobStatus::Pending:   return "pending";
        case JobStatus::Running:   return "running";
        case JobStatus::Completed: return "completed";
        case JobStatus::Failed:    return "failed";
        case JobStatus::Cancelled: return "cancelled";
    }
    return "unknown";
}
// Phase 12 (Wave 12.2-D): standard tuple alias replaces EncoderJob wrapper.
// Layout: (job_id, name, inputs, explicit_output, output_directory, profile,
//          status, current_file_index, current_file_pct, total_job_pct,
//          current_fps, elapsed_ms, error_message, file_reports).
using EncoderJob = std::tuple<
    uint64_t, std::string, std::vector<std::filesystem::path>,
    std::filesystem::path, std::filesystem::path, TranscodeProfile,
    JobStatus, size_t, double, double, double, double,
    std::string, std::vector<HwTranscodeReport>>;

[[nodiscard]] inline uint64_t encoderJobId(const EncoderJob& j) noexcept {
    const auto& [id, nm, in, eo, od, pr, st, fi, fp, tp, fps, el, em, fr] = j;
    (void)nm;(void)in;(void)eo;(void)od;(void)pr;(void)st;(void)fi;(void)fp;(void)tp;(void)fps;(void)el;(void)em;(void)fr;
    return id;
}
[[nodiscard]] inline JobStatus encoderJobStatus(const EncoderJob& j) noexcept {
    const auto& [id, nm, in, eo, od, pr, st, fi, fp, tp, fps, el, em, fr] = j;
    (void)id;(void)nm;(void)in;(void)eo;(void)od;(void)pr;(void)fi;(void)fp;(void)tp;(void)fps;(void)el;(void)em;(void)fr;
    return st;
}
[[nodiscard]] std::string formatEncoderJobSummary(const EncoderJob& job);
[[nodiscard]] std::string encoderJobToJson(const EncoderJob& job);
[[nodiscard]] bool areConcatInputsHomogeneous(std::span<const std::filesystem::path> inputs);

enum class JobQueueEventKind : uint8_t {
    JobAdded,
    JobUpdated,
    JobRemoved,
    JobReordered,
    JobStarted,
    JobProgress,
    JobFinished,
    QueueIdle,
};

// Phase 12 (Wave 12.2-C): standard tuple alias replaces passive JobQueueEvent wrapper.
using JobQueueEvent = std::tuple<JobQueueEventKind, uint64_t, EncoderJob>;

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调多资源异步生命周期的宿主状态机：std::unique_ptr/std::jthread/std::tuple 无法表达其声明序即析构逆序、线程亲和与受控降级路径的交叉不变量。
// Invariant: 成员声明序即依赖序（先声明者后析构）；状态迁移仅在持锁线程或属主线程发生。
class EncoderJobQueue {
public:
    using EventCallback = std::function<void(const JobQueueEvent&)>;
    using RunnerFunc = std::function<Result<HwTranscodeReport>(const HwTranscodeOptions&)>;

    explicit EncoderJobQueue(RunnerFunc custom_runner = nullptr);
    ~EncoderJobQueue() noexcept;

    EncoderJobQueue(const EncoderJobQueue&) = delete;
    EncoderJobQueue& operator=(const EncoderJobQueue&) = delete;

    void setEventCallback(EventCallback cb);

    [[nodiscard]] Result<uint64_t> enqueueJob(
        std::vector<std::filesystem::path> inputs,
        const TranscodeProfile& profile,
        std::filesystem::path output_dir = {});

    [[nodiscard]] Result<void> updatePendingJob(
        uint64_t job_id,
        const TranscodeProfile& new_profile,
        std::optional<std::vector<std::filesystem::path>> new_inputs = std::nullopt);

    [[nodiscard]] Result<void> removeJob(uint64_t job_id);
    [[nodiscard]] Result<void> moveJob(uint64_t job_id, int delta);
    void clearNonRunningJobs(bool completed_only = false);
    [[nodiscard]] Result<void> retryJob(uint64_t job_id);

    // Phase 13 (Wave 3-B): Thread-safe Pending-only input mutation & output mode (0 new named types)
    [[nodiscard]] Result<void> setJobOutputMode(uint64_t job_id, JobOutputMode mode);
    [[nodiscard]] JobOutputMode jobOutputMode(uint64_t job_id) const;
    [[nodiscard]] Result<void> addJobInputs(uint64_t job_id, std::vector<std::filesystem::path> extra_inputs);
    [[nodiscard]] Result<void> removeJobInput(uint64_t job_id, size_t index);
    [[nodiscard]] Result<void> moveJobInput(uint64_t job_id, size_t from_index, size_t to_index);

    // Phase 13 (Wave 4-B): Transcode preview compute gate (bypass preview readback when disabled)
    void setPreviewEnabled(bool enabled) noexcept { preview_enabled_.store(enabled, std::memory_order_relaxed); }
    [[nodiscard]] bool isPreviewEnabled() const noexcept { return preview_enabled_.load(std::memory_order_relaxed); }

    void startWorker();
    void skipCurrentJob();
    void stopQueue(bool wait_for_idle = true);

    void start() { startWorker(); }
    void cancelCurrent() { skipCurrentJob(); }
    void clearCompleted() { clearNonRunningJobs(true); }

    [[nodiscard]] bool isWorkerRunning() const noexcept;
    [[nodiscard]] std::vector<EncoderJob> snapshotJobs() const;
    [[nodiscard]] std::vector<EncoderJob> snapshot() const { return snapshotJobs(); }
    [[nodiscard]] std::optional<EncoderJob> findJob(uint64_t job_id) const;

    [[nodiscard]] Result<uint64_t> enqueue(const TranscodeProfile& profile) {
        std::vector<std::filesystem::path> inputs;
        if (!profile.input.empty()) {
            inputs.push_back(std::filesystem::path(profile.input));
        }
        std::filesystem::path out_dir;
        if (!profile.output.empty()) {
            out_dir = std::filesystem::path(profile.output).parent_path();
        }
        return enqueueJob(std::move(inputs), profile, std::move(out_dir));
    }
private:
    void workerLoop();
    void emit(JobQueueEventKind kind, const EncoderJob& job);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<EncoderJob> jobs_;
    std::unordered_map<uint64_t, JobOutputMode> job_modes_;
    std::atomic<bool> preview_enabled_{true};
    uint64_t next_job_id_ = 1;

    std::atomic<bool> worker_active_{false};
    std::atomic<bool> stop_all_requested_{false};
    std::atomic<bool> skip_current_requested_{false};
    std::atomic<float> active_split_ratio_{0.5f};
    std::thread worker_thread_;

    RunnerFunc runner_;
    EventCallback on_event_;
};

} // namespace rtx
