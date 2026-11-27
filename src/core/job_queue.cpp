// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
#include "rtx/job_queue.h"
#include "rtx/io.h"
#include "rtx/render_graph.h"
#include "rtx/string_utils.h"

#include <algorithm>
#include <chrono>
#include <fstream>
#include <ranges>
#include <format>

namespace rtx {

bool areConcatInputsHomogeneous(std::span<const std::filesystem::path> inputs) {
    if (inputs.size() <= 1u) return true;
    std::vector<MediaInfo> probed;
    probed.reserve(inputs.size());
    for (const auto& p : inputs) {
        auto demuxer = makeFfmpegDemuxer();
        if (!demuxer) return false;
        auto info = demuxer->open(p);
        if (!info.has_value()) return false;
        probed.push_back(std::move(*info));
    }
    const MediaInfo& ref = probed.front();
    return std::ranges::all_of(std::span<const MediaInfo>(probed).subspan(1), [&ref](const MediaInfo& m) {
        return m.codec == ref.codec &&
               m.width == ref.width &&
               m.height == ref.height &&
               m.bit_depth == ref.bit_depth &&
               static_cast<int64_t>(m.frame_rate.num) * ref.frame_rate.den ==
                   static_cast<int64_t>(ref.frame_rate.num) * m.frame_rate.den &&
               m.audio_streams == ref.audio_streams;
    });
}

std::string formatEncoderJobSummary(const EncoderJob& job) {
    const auto& [id, nm, inputs, eo, od, profile, st, fi, fp, tp, fps, el, em, fr] = job;
    (void)id;(void)nm;(void)eo;(void)od;(void)st;(void)fi;(void)fp;(void)tp;(void)fps;(void)el;(void)em;(void)fr;
    return std::format("{} file(s) | {} {} {} | {} VSR{} | {}",
        inputs.size(), profile.codec, profile.rate_control, profile.qp,
        profile.resolution, profile.vsr_quality, profile.fps_mode);
}

std::string encoderJobToJson(const EncoderJob& job) {
    const auto& [job_id, name, inputs, eo, od, pr, status, fi, fp, tp, fps, el, error_message, fr] = job;
    (void)eo;(void)od;(void)pr;(void)el;(void)fr;
    return std::format(
        R"({{"jobId":{},"name":"{}","status":"{}","fileCount":{},"currentFileIndex":{},"currentFilePct":{},"totalJobPct":{},"currentFps":{},"summary":"{}","error":"{}"}})",
        job_id, rtx::str::escapeJson(name), jobStatusName(status), inputs.size(),
        fi, fp, tp, fps,
        rtx::str::escapeJson(formatEncoderJobSummary(job)), rtx::str::escapeJson(error_message));
}

EncoderJobQueue::EncoderJobQueue(RunnerFunc custom_runner)
    : runner_(custom_runner ? std::move(custom_runner) : RunnerFunc(&runNvidiaRemasterTranscode)) {}

EncoderJobQueue::~EncoderJobQueue() noexcept { stopQueue(true); }

void EncoderJobQueue::setEventCallback(EventCallback cb) {
    std::lock_guard<std::mutex> lock(mutex_);
    on_event_ = std::move(cb);
}

void EncoderJobQueue::emit(JobQueueEventKind kind, const EncoderJob& job) {
    EventCallback cb;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        cb = on_event_;
    }
    if (cb) cb(JobQueueEvent{kind, encoderJobId(job), job});
}

Result<uint64_t> EncoderJobQueue::enqueueJob(
    std::vector<std::filesystem::path> inputs,
    const TranscodeProfile& profile,
    std::filesystem::path output_dir) {
    if (inputs.empty()) {
        return err(ErrorCode::InvalidArgument, "enqueueJob requires at least one input file");
    }
    EncoderJob created{};
    uint64_t created_id = 0;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto& [jid, jname, jin, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = created;
        (void)jeo;(void)jfi;(void)jfp;(void)jtp;(void)jfps;(void)jel;(void)jem;(void)jfr;
        jid = next_job_id_++;
        created_id = jid;
        jin = std::move(inputs);
        jod = std::move(output_dir);
        jprof = profile;
        jst = JobStatus::Pending;
        jname = jin.size() == 1
            ? rtx::str::pathToUtf8(jin.front().filename())
            : std::format("{} (+{} files)",
                rtx::str::pathToUtf8(jin.front().filename()), jin.size() - 1);
        jobs_.push_back(created);
    }
    cv_.notify_all();
    emit(JobQueueEventKind::JobAdded, created);
    return created_id;
}

Result<void> EncoderJobQueue::updatePendingJob(
    uint64_t job_id,
    const TranscodeProfile& new_profile,
    std::optional<std::vector<std::filesystem::path>> new_inputs) {
    EncoderJob snapshot{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(jobs_.begin(), jobs_.end(),
            [job_id](const EncoderJob& j) { return encoderJobId(j) == job_id; });
        if (it == jobs_.end()) return err(ErrorCode::NotFound, "Job not found");
        auto& [jid, jname, jin, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = *it;
        (void)jid;(void)jname;(void)jeo;(void)jod;(void)jfi;(void)jfp;(void)jtp;(void)jfps;(void)jel;(void)jem;(void)jfr;
        if (jst != JobStatus::Pending) return err(ErrorCode::State, "Only Pending jobs can be edited");
        jprof = new_profile;
        if (new_inputs.has_value() && !new_inputs->empty()) jin = std::move(*new_inputs);
        snapshot = *it;
    }
    emit(JobQueueEventKind::JobUpdated, snapshot);
    return {};
}

Result<void> EncoderJobQueue::removeJob(uint64_t job_id) {
    EncoderJob removed{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(jobs_.begin(), jobs_.end(),
            [job_id](const EncoderJob& j) { return encoderJobId(j) == job_id; });
        if (it == jobs_.end()) return err(ErrorCode::NotFound, "Job not found");
        if (encoderJobStatus(*it) == JobStatus::Running) {
            return err(ErrorCode::State, "Cannot remove Running job; use skipCurrentJob");
        }
        removed = std::move(*it);
        jobs_.erase(it);
    }
    emit(JobQueueEventKind::JobRemoved, removed);
    return {};
}

Result<void> EncoderJobQueue::moveJob(uint64_t job_id, int delta) {
    if (delta == 0) return {};
    EncoderJob snapshot{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(jobs_.begin(), jobs_.end(),
            [job_id](const EncoderJob& j) { return encoderJobId(j) == job_id; });
        if (it == jobs_.end()) return err(ErrorCode::NotFound, "Job not found");
        if (encoderJobStatus(*it) != JobStatus::Pending) return err(ErrorCode::State, "Only Pending jobs can be reordered");

        const ptrdiff_t idx = std::distance(jobs_.begin(), it);
        const ptrdiff_t target = std::clamp<ptrdiff_t>(idx + delta, 0, static_cast<ptrdiff_t>(jobs_.size()) - 1);
        if (target != idx && encoderJobStatus(jobs_[static_cast<size_t>(target)]) == JobStatus::Pending) {
            std::swap(jobs_[static_cast<size_t>(idx)], jobs_[static_cast<size_t>(target)]);
            snapshot = jobs_[static_cast<size_t>(target)];
        } else {
            return {};
        }
    }
    emit(JobQueueEventKind::JobReordered, snapshot);
    return {};
}

void EncoderJobQueue::clearNonRunningJobs(bool completed_only) {
    std::lock_guard<std::mutex> lock(mutex_);
    std::erase_if(jobs_, [completed_only](const EncoderJob& j) {
        const JobStatus st = encoderJobStatus(j);
        if (st == JobStatus::Running) return false;
        return completed_only ? (st == JobStatus::Completed) : true;
    });
}

Result<void> EncoderJobQueue::retryJob(uint64_t job_id) {
    EncoderJob snapshot{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(jobs_.begin(), jobs_.end(),
            [job_id](const EncoderJob& j) { return encoderJobId(j) == job_id; });
        if (it == jobs_.end()) return err(ErrorCode::NotFound, "Job not found");
        auto& [jid, jname, jin, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = *it;
        (void)jid;(void)jname;(void)jin;(void)jeo;(void)jod;(void)jprof;(void)jfps;(void)jel;(void)jfr;
        if (jst != JobStatus::Failed && jst != JobStatus::Cancelled) {
            return err(ErrorCode::State, "Only Failed or Cancelled jobs can be retried");
        }
        jst = JobStatus::Pending;
        jem.clear();
        jfi = 0;
        jfp = 0.0;
        jtp = 0.0;
        snapshot = *it;
    }
    cv_.notify_all();
    emit(JobQueueEventKind::JobUpdated, snapshot);
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

void EncoderJobQueue::skipCurrentJob() { skip_current_requested_.store(true); }

void EncoderJobQueue::stopQueue(bool wait_for_idle) {
    stop_all_requested_.store(true);
    skip_current_requested_.store(true);
    cv_.notify_all();
    if (wait_for_idle && worker_thread_.joinable()) worker_thread_.join();
}

bool EncoderJobQueue::isWorkerRunning() const noexcept { return worker_active_.load(); }

std::vector<EncoderJob> EncoderJobQueue::snapshotJobs() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return {jobs_.begin(), jobs_.end()};
}

std::optional<EncoderJob> EncoderJobQueue::findJob(uint64_t job_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = std::find_if(jobs_.begin(), jobs_.end(),
        [job_id](const EncoderJob& j) { return encoderJobId(j) == job_id; });
    if (it == jobs_.end()) return std::nullopt;
    return *it;
}

void EncoderJobQueue::workerLoop() {
    for (;;) {
        uint64_t active_id = 0;
        EncoderJob job_copy{};
        {
            std::unique_lock<std::mutex> lock(mutex_);
            auto it = std::find_if(jobs_.begin(), jobs_.end(),
                [](const EncoderJob& j) { return encoderJobStatus(j) == JobStatus::Pending; });
            if (it == jobs_.end() || stop_all_requested_.load()) {
                worker_active_.store(false);
                break;
            }
            auto& [jid, jname, jin, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = *it;
            (void)jname;(void)jin;(void)jeo;(void)jod;(void)jprof;(void)jel;(void)jfr;
            jst = JobStatus::Running;
            jfi = 0;
            jfp = 0.0;
            jtp = 0.0;
            jfps = 0.0;
            jem.clear();
            active_id = jid;
            job_copy = *it;
        }
        skip_current_requested_.store(false);
        emit(JobQueueEventKind::JobStarted, job_copy);

        const auto& [c_id, c_name, c_inputs, c_exp_out, c_out_dir, c_profile,
                     c_st, c_fi, c_fp, c_tp, c_fps, c_el, c_em, c_fr] = job_copy;
        (void)c_id;(void)c_name;(void)c_st;(void)c_fi;(void)c_fp;(void)c_tp;(void)c_fps;(void)c_el;(void)c_em;(void)c_fr;

        bool job_ok = true;
        std::string err_msg;
        std::vector<HwTranscodeReport> reports;
        std::vector<std::filesystem::path> exec_inputs = c_inputs;
        std::filesystem::path concat_manifest_path;
        bool use_heterogeneous_render_graph = false;
        if (jobOutputMode(active_id) == JobOutputMode::MergeConcat && exec_inputs.size() > 1u) {
            if (areConcatInputsHomogeneous(exec_inputs)) {
                concat_manifest_path = std::filesystem::temp_directory_path() /
                    ("cinertx_concat_" + std::to_string(active_id) + ".ffconcat");
                std::ofstream mf(concat_manifest_path, std::ios::binary | std::ios::trunc);
                if (mf) {
                    mf << buildSafeConcatList(exec_inputs);
                    mf.close();
                    exec_inputs = {concat_manifest_path};
                }
            } else {
                use_heterogeneous_render_graph = true;
            }
        }
        if (use_heterogeneous_render_graph) {
            std::vector<RenderGraphSegment> v_segs;
            std::vector<AudioGraphSegment> a_segs;
            v_segs.reserve(exec_inputs.size());
            a_segs.reserve(exec_inputs.size());
            int64_t cursor_flicks = 0;
            bool probe_ok = true;
            for (size_t idx = 0; idx < exec_inputs.size(); ++idx) {
                auto demuxer = makeFfmpegDemuxer();
                auto info = demuxer ? demuxer->open(exec_inputs[idx])
                                    : Result<MediaInfo>(err(ErrorCode::State, "demuxer unavailable"));
                if (!info.has_value()) {
                    probe_ok = false;
                    err_msg = info.error().toString();
                    break;
                }
                const int64_t dur_ms = std::max<int64_t>(1, info->duration_ms);
                const int64_t dur_flicks = dur_ms * 706305600LL;
                const std::string utf8_path = rtx::str::pathToUtf8(exec_inputs[idx]);
                v_segs.emplace_back(utf8_path, 0LL, dur_flicks, cursor_flicks, cursor_flicks + dur_flicks, idx > 0, idx > 0);
                if (info->audio_streams > 0) {
                    a_segs.emplace_back(utf8_path, 0LL, dur_flicks, cursor_flicks, cursor_flicks + dur_flicks, 1.0f);
                }
                cursor_flicks += dur_flicks;
            }
            if (!probe_ok) {
                job_ok = false;
            } else {
                TranscodeProfile prof = c_profile;
                prof.input = rtx::str::pathToUtf8(exec_inputs.front());
                if (!c_exp_out.empty()) {
                    prof.output = rtx::str::pathToUtf8(c_exp_out);
                    prof.auto_output = false;
                } else if (prof.output.empty() || prof.auto_output) {
                    prof.output.clear();
                    prof.auto_output = true;
                }
                (void)validateAndNormalizeProfile(prof, false);
                HwTranscodeOptions opts = profileToHwOptions(prof);
                if (!c_out_dir.empty() && prof.auto_output) {
                    opts.output_path = c_out_dir / opts.output_path.filename();
                }
                opts.cancel = &skip_current_requested_;
                opts.split_ratio = &active_split_ratio_;
                if (isPreviewEnabled()) {
                    if (!opts.preview) {
                        opts.preview = [](const void*, std::uint32_t) {};
                    }
                } else {
                    opts.preview = nullptr;
                }
                auto res = runNvidiaRemasterRenderGraph(opts, v_segs, a_segs);
                if (!res.has_value()) {
                    job_ok = false;
                    err_msg = res.error().toString();
                } else {
                    reports.push_back(std::move(*res));
                }
            }
            exec_inputs.clear();
        }
        const size_t file_count = std::max<size_t>(1, exec_inputs.size());

        for (size_t i = 0; i < exec_inputs.size(); ++i) {
            if (stop_all_requested_.load() || skip_current_requested_.load()) {
                job_ok = false;
                err_msg = "Cancelled";
                break;
            }
            TranscodeProfile prof = c_profile;
            prof.input = rtx::str::pathToUtf8(exec_inputs[i]);
            if (exec_inputs.size() == 1 && !c_exp_out.empty()) {
                prof.output = rtx::str::pathToUtf8(c_exp_out);
                prof.auto_output = false;
            } else if (exec_inputs.size() == 1 && !prof.output.empty() && !prof.auto_output) {
            } else {
                prof.output.clear();
                prof.auto_output = true;
            }
            (void)validateAndNormalizeProfile(prof, false);
            HwTranscodeOptions opts = profileToHwOptions(prof);
            if (!c_out_dir.empty() && prof.auto_output) {
                opts.output_path = c_out_dir / opts.output_path.filename();
            }
            opts.cancel = &skip_current_requested_;
            opts.split_ratio = &active_split_ratio_;
            if (isPreviewEnabled()) {
                if (!opts.preview) {
                    opts.preview = [](const void*, std::uint32_t) {};
                }
            } else {
                opts.preview = nullptr;
            }

            auto last_prog_emit = std::chrono::steady_clock::now() - std::chrono::seconds(1);
            opts.progress = [this, active_id, i, file_count, &last_prog_emit](const HwTranscodeProgress& p) {
                const auto [frames_dec, frames_enc, total_frames, elapsed_ms] = p;
                (void)frames_dec;
                const auto now = std::chrono::steady_clock::now();
                if (std::chrono::duration<double>(now - last_prog_emit).count() < 0.10 &&
                    (total_frames == 0 || frames_enc < total_frames)) {
                    return;
                }
                last_prog_emit = now;
                const double file_pct = (total_frames > 0)
                    ? std::clamp(static_cast<double>(frames_enc) * 100.0 / static_cast<double>(total_frames), 0.0, 100.0)
                    : 0.0;
                const double total_pct = ((static_cast<double>(i) * 100.0) + file_pct) / static_cast<double>(file_count);
                const double fps = (elapsed_ms > 0.0) ? (static_cast<double>(frames_enc) * 1000.0 / elapsed_ms) : 0.0;

                EncoderJob prog_snap{};
                bool found = false;
                {
                    std::lock_guard<std::mutex> lock(mutex_);
                    auto it = std::find_if(jobs_.begin(), jobs_.end(),
                        [active_id](const EncoderJob& j) { return encoderJobId(j) == active_id; });
                    if (it != jobs_.end()) {
                        auto& [jid, jname, jin, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = *it;
                        (void)jid;(void)jname;(void)jin;(void)jeo;(void)jod;(void)jprof;(void)jst;(void)jel;(void)jem;(void)jfr;
                        jfi = i;
                        jfp = file_pct;
                        jtp = total_pct;
                        jfps = fps;
                        prog_snap = *it;
                        found = true;
                    }
                }
                if (found) emit(JobQueueEventKind::JobProgress, prog_snap);
            };

            auto res = runner_(opts);
            if (!res.has_value()) {
                job_ok = false;
                err_msg = res.error().toString();
                break;
            }
            reports.push_back(std::move(*res));

            EncoderJob prog_snap{};
            {
                std::lock_guard<std::mutex> lock(mutex_);
                auto it = std::find_if(jobs_.begin(), jobs_.end(),
                    [active_id](const EncoderJob& j) { return encoderJobId(j) == active_id; });
                if (it != jobs_.end()) {
                    auto& [jid, jname, jin, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = *it;
                    (void)jid;(void)jname;(void)jin;(void)jeo;(void)jod;(void)jprof;(void)jst;(void)jfps;(void)jel;(void)jem;(void)jfr;
                    jfi = i + 1;
                    jfp = 100.0;
                    jtp = (static_cast<double>(i + 1) * 100.0) / static_cast<double>(file_count);
                    prog_snap = *it;
                }
            }
            emit(JobQueueEventKind::JobProgress, prog_snap);
        }
        if (!concat_manifest_path.empty()) {
            std::error_code rm_ec;
            std::filesystem::remove(concat_manifest_path, rm_ec);
        }
        EncoderJob fin_snap{};
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = std::find_if(jobs_.begin(), jobs_.end(),
                [active_id](const EncoderJob& j) { return encoderJobId(j) == active_id; });
            if (it != jobs_.end()) {
                auto& [jid, jname, jin, jeo, jod, jprof, jst, jfi, jfp, jtp, jfps, jel, jem, jfr] = *it;
                (void)jid;(void)jname;(void)jin;(void)jeo;(void)jod;(void)jprof;(void)jfi;(void)jfp;(void)jfps;(void)jel;
                if (job_ok) {
                    jst = JobStatus::Completed;
                    jtp = 100.0;
                } else if (skip_current_requested_.load() || stop_all_requested_.load()) {
                    jst = JobStatus::Cancelled;
                    jem = err_msg;
                } else {
                    jst = JobStatus::Failed;
                    jem = err_msg;
                }
                jfr = std::move(reports);
                fin_snap = *it;
            }
            const bool more_pending = !stop_all_requested_.load() &&
                std::any_of(jobs_.begin(), jobs_.end(),
                    [](const EncoderJob& j) { return encoderJobStatus(j) == JobStatus::Pending; });
            if (!more_pending) worker_active_.store(false);
        }
        emit(JobQueueEventKind::JobFinished, fin_snap);
        if (!worker_active_.load()) break;
    }
}


Result<void> EncoderJobQueue::setJobOutputMode(uint64_t job_id, JobOutputMode mode) {
    EncoderJob snap{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(jobs_.begin(), jobs_.end(),
            [job_id](const EncoderJob& j) { return encoderJobId(j) == job_id; });
        if (it == jobs_.end()) return err(ErrorCode::NotFound, "job not found");
        if (encoderJobStatus(*it) != JobStatus::Pending) {
            return err(ErrorCode::State, "only Pending jobs can change output mode");
        }
        job_modes_[job_id] = mode;
        snap = *it;
    }
    emit(JobQueueEventKind::JobUpdated, snap);
    return {};
}

JobOutputMode EncoderJobQueue::jobOutputMode(uint64_t job_id) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = job_modes_.find(job_id);
    return (it != job_modes_.end()) ? it->second : JobOutputMode::BatchSeparate;
}

Result<void> EncoderJobQueue::addJobInputs(uint64_t job_id, std::vector<std::filesystem::path> extra_inputs) {
    if (extra_inputs.empty()) return {};
    EncoderJob snap{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(jobs_.begin(), jobs_.end(),
            [job_id](const EncoderJob& j) { return encoderJobId(j) == job_id; });
        if (it == jobs_.end()) return err(ErrorCode::NotFound, "job not found");
        if (encoderJobStatus(*it) != JobStatus::Pending) {
            return err(ErrorCode::State, "only Pending jobs can add inputs");
        }
        auto& inputs = std::get<2>(*it);
        for (auto& p : extra_inputs) {
            if (!p.empty()) inputs.push_back(std::move(p));
        }
        snap = *it;
    }
    emit(JobQueueEventKind::JobUpdated, snap);
    return {};
}

Result<void> EncoderJobQueue::removeJobInput(uint64_t job_id, size_t index) {
    EncoderJob snap{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(jobs_.begin(), jobs_.end(),
            [job_id](const EncoderJob& j) { return encoderJobId(j) == job_id; });
        if (it == jobs_.end()) return err(ErrorCode::NotFound, "job not found");
        if (encoderJobStatus(*it) != JobStatus::Pending) {
            return err(ErrorCode::State, "only Pending jobs can remove inputs");
        }
        auto& inputs = std::get<2>(*it);
        if (index >= inputs.size()) return err(ErrorCode::InvalidArgument, "input index out of range");
        inputs.erase(inputs.begin() + static_cast<std::ptrdiff_t>(index));
        snap = *it;
    }
    emit(JobQueueEventKind::JobUpdated, snap);
    return {};
}

Result<void> EncoderJobQueue::moveJobInput(uint64_t job_id, size_t from_index, size_t to_index) {
    EncoderJob snap{};
    {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = std::find_if(jobs_.begin(), jobs_.end(),
            [job_id](const EncoderJob& j) { return encoderJobId(j) == job_id; });
        if (it == jobs_.end()) return err(ErrorCode::NotFound, "job not found");
        if (encoderJobStatus(*it) != JobStatus::Pending) {
            return err(ErrorCode::State, "only Pending jobs can reorder inputs");
        }
        auto& inputs = std::get<2>(*it);
        if (from_index >= inputs.size() || to_index >= inputs.size()) {
            return err(ErrorCode::InvalidArgument, "input index out of range");
        }
        if (from_index != to_index) {
            auto moved = std::move(inputs[from_index]);
            inputs.erase(inputs.begin() + static_cast<std::ptrdiff_t>(from_index));
            inputs.insert(inputs.begin() + static_cast<std::ptrdiff_t>(to_index), std::move(moved));
        }
        snap = *it;
    }
    emit(JobQueueEventKind::JobUpdated, snap);
    return {};
}

}  // namespace rtx
