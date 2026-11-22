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
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "rtx/hw_transcode.h"
#include "rtx/param_schema.h"
#include "rtx/status.h"

namespace rtx {

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
// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 多字段领域模型：具名字段承载序列化/校验/反射契约，压进 std::tuple 会丢失全部字段语义与默认值契约。
// Invariant: 字段集合与默认值即对外契约（schema/测试逐字段锚定）；只增不删。
struct EncoderJob {
    uint64_t job_id = 0;
    std::string name{};
    std::vector<std::filesystem::path> inputs{};
    std::filesystem::path explicit_output{};
    std::filesystem::path output_directory{};
    TranscodeProfile profile{};

    JobStatus status = JobStatus::Pending;
    size_t current_file_index = 0;
    double current_file_pct = 0.0;
    double total_job_pct = 0.0;
    double current_fps = 0.0;
    double elapsed_ms = 0.0;
    std::string error_message{};
    std::vector<HwTranscodeReport> file_reports{};

    [[nodiscard]] std::string formatSummary() const;
    [[nodiscard]] std::string toJson() const;
};

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 多字段领域模型：具名字段承载序列化/校验/反射契约，压进 std::tuple 会丢失全部字段语义与默认值契约。
// Invariant: 字段集合与默认值即对外契约（schema/测试逐字段锚定）；只增不删。
struct JobQueueEvent {
    enum class Kind : uint8_t {
        JobAdded,
        JobUpdated,
        JobRemoved,
        JobReordered,
        JobStarted,
        JobProgress,
        JobFinished,
        QueueIdle,
    };
    Kind kind = Kind::JobUpdated;
    uint64_t job_id = 0;
    EncoderJob snapshot{};
};

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
    void emit(JobQueueEvent::Kind kind, const EncoderJob& job);

    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::deque<EncoderJob> jobs_;
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
