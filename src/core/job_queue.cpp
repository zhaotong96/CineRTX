// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
#include "rtx/job_queue.h"
#include "rtx/string_utils.h"

#include <algorithm>
#include <chrono>
#include <format>
#include <sstream>

namespace rtx {

std::string EncoderJob::formatSummary() const {
    return std::format("{} file(s) | {} {} {} | {} VSR{} | {}",
        inputs.size(),
        profile.codec,
        profile.rate_control,
        profile.qp,
        profile.resolution,
        profile.vsr_quality,
        profile.fps_mode);
}
std::string EncoderJob::toJson() const {
    std::ostringstream oss;
    oss << "{"
        << "\"jobId\":" << job_id << ","
        << "\"name\":\"" << rtx::str::escapeJson(name) << "\","
        << "\"status\":\"" << jobStatusName(status) << "\","
        << "\"fileCount\":" << inputs.size() << ","
        << "\"currentFileIndex\":" << current_file_index << ","
        << "\"currentFilePct\":" << current_file_pct << ","
        << "\"totalJobPct\":" << total_job_pct << ","
        << "\"currentFps\":" << current_fps << ","
        << "\"summary\":\"" << rtx::str::escapeJson(formatSummary()) << "\","
        << "\"error\":\"" << rtx::str::escapeJson(error_message) << "\""
        << "}";
    return oss.str();
}
EncoderJobQueue::EncoderJobQueue(RunnerFunc custom_runner)
    : runner_(custom_runner ? std::move(custom_runner) : RunnerFunc(&runNvidiaRemasterTranscode)) {}

EncoderJobQueue::~EncoderJobQueue() noexcept {
    stopQueue(true);
}
void EncoderJobQueue::setEventCallback(EventCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_event_ = std::move(cb);
}
void EncoderJobQueue::emit(JobQueueEvent::Kind kind, const EncoderJob& job) {
    EventCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = on_event_;
    }
    if (cb) {
        cb(JobQueueEvent{kind, job.job_id, job});
    }
}
Result<uint64_t> EncoderJobQueue::enqueueJob(
    std::vector<std::filesystem::path> inputs,
    const TranscodeProfile& profile,
    std::filesystem::path output_dir) {
    if (inputs.empty()) {
        return err(ErrorCode::InvalidArgument, "enqueueJob requires at least one input file");
    }
    EncoderJob created;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        created.job_id = next_job_id_++;
        created.inputs = std::move(inputs);
        created.output_directory = std::move(output_dir);
        created.profile = profile;
        created.name = created.inputs.size() == 1
            ? rtx::str::pathToUtf8(created.inputs.front().filename())
            : std::format("{} (+{} files)",
                rtx::str::pathToUtf8(created.inputs.front().filename()),
                created.inputs.size() - 1);
        jobs_.push_back(created);
    }
    cv_.notify_all();
    emit(JobQueueEvent::Kind::JobAdded, created);
    return created.job_id;
}
Result<void> EncoderJobQueue::updatePendingJob(
    uint64_t job_id,
    const TranscodeProfile& new_profile,
    std::optional<std::vector<std::filesystem::path>> new_inputs) {
    EncoderJob snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(jobs_.begin(), jobs_.end(),
            [job_id](const EncoderJob& j) { return j.job_id == job_id; });
        if (it == jobs_.end()) {
            return err(ErrorCode::NotFound, "Job not found");
        }
        if (it->status != JobStatus::Pending) {
            return err(ErrorCode::State, "Only Pending jobs can be edited");
        }
        it->profile = new_profile;
        if (new_inputs.has_value() && !new_inputs->empty()) {
            it->inputs = std::move(*new_inputs);
        }
        snapshot = *it;
    }
    emit(JobQueueEvent::Kind::JobUpdated, snapshot);
    return {};
}
Result<void> EncoderJobQueue::removeJob(uint64_t job_id) {
    EncoderJob removed;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(jobs_.begin(), jobs_.end(),
            [job_id](const EncoderJob& j) { return j.job_id == job_id; });
        if (it == jobs_.end()) {
            return err(ErrorCode::NotFound, "Job not found");
        }
        if (it->status == JobStatus::Running) {
            return err(ErrorCode::State, "Cannot remove Running job; use skipCurrentJob");
        }
        removed = std::move(*it);
        jobs_.erase(it);
    }
    emit(JobQueueEvent::Kind::JobRemoved, removed);
    return {};
}
Result<void> EncoderJobQueue::moveJob(uint64_t job_id, int delta) {
    if (delta == 0) return {};
    EncoderJob snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(jobs_.begin(), jobs_.end(),
            [job_id](const EncoderJob& j) { return j.job_id == job_id; });
        if (it == jobs_.end()) return err(ErrorCode::NotFound, "Job not found");
        if (it->status != JobStatus::Pending) return err(ErrorCode::State, "Only Pending jobs can be reordered");

        const ptrdiff_t idx = std::distance(jobs_.begin(), it);
        const ptrdiff_t target = std::clamp<ptrdiff_t>(idx + delta, 0, static_cast<ptrdiff_t>(jobs_.size()) - 1);
        if (target != idx && jobs_[static_cast<size_t>(target)].status == JobStatus::Pending) {
            std::swap(jobs_[static_cast<size_t>(idx)], jobs_[static_cast<size_t>(target)]);
            snapshot = jobs_[static_cast<size_t>(target)];
        } else {
            return {};
        }
    }
    emit(JobQueueEvent::Kind::JobReordered, snapshot);
    return {};
}
void EncoderJobQueue::clearNonRunningJobs(bool completed_only) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(jobs_, [completed_only](const EncoderJob& j) {
        if (j.status == JobStatus::Running) return false;
        return completed_only ? (j.status == JobStatus::Completed) : true;
    });
}
Result<void> EncoderJobQueue::retryJob(uint64_t job_id) {
    EncoderJob snapshot;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(jobs_.begin(), jobs_.end(),
            [job_id](const EncoderJob& j) { return j.job_id == job_id; });
        if (it == jobs_.end()) return err(ErrorCode::NotFound, "Job not found");
        if (it->status != JobStatus::Failed && it->status != JobStatus::Cancelled) {
            return err(ErrorCode::State, "Only Failed or Cancelled jobs can be retried");
        }
        it->status = JobStatus::Pending;
        it->error_message.clear();
        it->current_file_index = 0;
        it->current_file_pct = 0.0;
        it->total_job_pct = 0.0;
        snapshot = *it;
    }
    cv_.notify_all();
    emit(JobQueueEvent::Kind::JobUpdated, snapshot);
    return {};
}
void EncoderJobQueue::startWorker() {
    std::lock_guard<std::mutex> lock(mutex_);
    if (worker_active_.load()) return;
    stop_all_requested_.store(false);
    skip_current_requested_.store(false);
    worker_active_.store(true);
    if (worker_thread_.joinable()) worker_thread_.join();
    worker_thread_ = std::thread([this]() { workerLoop(); });
}
void EncoderJobQueue::skipCurrentJob() {
    skip_current_requested_.store(true);
}
void EncoderJobQueue::stopQueue(bool wait_for_idle) {
    stop_all_requested_.store(true);
    skip_current_requested_.store(true);
    cv_.notify_all();
    if (wait_for_idle && worker_thread_.joinable()) {
        worker_thread_.join();
    }
}
bool EncoderJobQueue::isWorkerRunning() const noexcept {
    return worker_active_.load();
}
std::vector<EncoderJob> EncoderJobQueue::snapshotJobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {jobs_.begin(), jobs_.end()};
}
std::optional<EncoderJob> EncoderJobQueue::findJob(uint64_t job_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(jobs_.begin(), jobs_.end(),
        [job_id](const EncoderJob& j) { return j.job_id == job_id; });
    if (it == jobs_.end()) return std::nullopt;
    return *it;
}
void EncoderJobQueue::workerLoop() {
    for (;;) {
        uint64_t active_id = 0;
        EncoderJob job_copy;
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto it = std::find_if(jobs_.begin(), jobs_.end(),
                [](const EncoderJob& j) { return j.status == JobStatus::Pending; });
            if (it == jobs_.end() || stop_all_requested_.load()) {
                worker_active_.store(false);
                break;
            }
            it->status = JobStatus::Running;
            it->current_file_index = 0;
            it->current_file_pct = 0.0;
            it->total_job_pct = 0.0;
            it->current_fps = 0.0;
            it->error_message.clear();
            active_id = it->job_id;
            job_copy = *it;
        }
        skip_current_requested_.store(false);
        emit(JobQueueEvent::Kind::JobStarted, job_copy);

        bool job_ok = true;
        std::string err_msg;
        std::vector<HwTranscodeReport> reports;
        const size_t file_count = std::max<size_t>(1, job_copy.inputs.size());

        for (size_t i = 0; i < job_copy.inputs.size(); ++i) {
            if (stop_all_requested_.load() || skip_current_requested_.load()) {
                job_ok = false;
                err_msg = "Cancelled";
                break;
            }
            TranscodeProfile prof = job_copy.profile;
            prof.input = rtx::str::pathToUtf8(job_copy.inputs[i]);
            if (job_copy.inputs.size() == 1 && !job_copy.explicit_output.empty()) {
                prof.output = rtx::str::pathToUtf8(job_copy.explicit_output);
                prof.auto_output = false;
            } else if (job_copy.inputs.size() == 1 && !prof.output.empty() && !prof.auto_output) {
            } else {
                prof.output.clear();
                prof.auto_output = true;
            }
            (void)validateAndNormalizeProfile(prof, false);
            HwTranscodeOptions opts = profileToHwOptions(prof);
            if (!job_copy.output_directory.empty() && prof.auto_output) {
                opts.output_path = job_copy.output_directory / opts.output_path.filename();
            }
            opts.cancel = &skip_current_requested_;
            opts.split_ratio = &active_split_ratio_;

            auto last_prog_emit = std::chrono::steady_clock::now() - std::chrono::seconds(1);
            opts.progress = [this, active_id, i, file_count, &last_prog_emit](const HwTranscodeProgress& p) {
                const auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration<double>(now - last_prog_emit).count() < 0.10 &&
                    (p.total_frames == 0 || p.frames_encoded < p.total_frames)) {
                    return;
// Keep explicit single-file output from profile
                }
                last_prog_emit = now;
                const double file_pct = (p.total_frames > 0)
                    ? std::clamp(static_cast<double>(p.frames_encoded) * 100.0 / static_cast<double>(p.total_frames), 0.0, 100.0)
                    : 0.0;
                const double total_pct = ((static_cast<double>(i) * 100.0) + file_pct) / static_cast<double>(file_count);
                const double fps = (p.elapsed_ms > 0.0) ? (static_cast<double>(p.frames_encoded) * 1000.0 / p.elapsed_ms) : 0.0;

                EncoderJob prog_snap;
                bool found = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = std::find_if(jobs_.begin(), jobs_.end(),
                        [active_id](const EncoderJob& j) { return j.job_id == active_id; });
                    if (it != jobs_.end()) {
                        it->current_file_index = i;
                        it->current_file_pct = file_pct;
                        it->total_job_pct = total_pct;
                        it->current_fps = fps;
                        prog_snap = *it;
                        found = true;
                    }
                }
                if (found) {
                    emit(JobQueueEvent::Kind::JobProgress, prog_snap);
                }
            };

            auto res = runner_(opts);
            if (!res.has_value()) {
                job_ok = false;
                err_msg = res.error().toString();
                break;
            }
            reports.push_back(std::move(*res));

            EncoderJob prog_snap;
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = std::find_if(jobs_.begin(), jobs_.end(),
                    [active_id](const EncoderJob& j) { return j.job_id == active_id; });
                if (it != jobs_.end()) {
                    it->current_file_index = i + 1;
                    it->current_file_pct = 100.0;
                    it->total_job_pct = (static_cast<double>(i + 1) * 100.0) / static_cast<double>(file_count);
                    prog_snap = *it;
                }
            }
            emit(JobQueueEvent::Kind::JobProgress, prog_snap);
        }
        EncoderJob fin_snap;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = std::find_if(jobs_.begin(), jobs_.end(),
                [active_id](const EncoderJob& j) { return j.job_id == active_id; });
            if (it != jobs_.end()) {
                if (job_ok) {
                    it->status = JobStatus::Completed;
                    it->total_job_pct = 100.0;
                } else if (skip_current_requested_.load() || stop_all_requested_.load()) {
                    it->status = JobStatus::Cancelled;
                    it->error_message = err_msg;
                } else {
                    it->status = JobStatus::Failed;
                    it->error_message = err_msg;
                }
                it->file_reports = std::move(reports);
                fin_snap = *it;
            }
            const bool more_pending = !stop_all_requested_.load() &&
                std::any_of(jobs_.begin(), jobs_.end(),
                    [](const EncoderJob& j) { return j.status == JobStatus::Pending; });
            if (!more_pending) {
                worker_active_.store(false);
            }
        }
        emit(JobQueueEvent::Kind::JobFinished, fin_snap);
        if (!worker_active_.load()) {
            break;
        }
    }
}
} // namespace rtx
