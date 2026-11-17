# CineRTX Phase 10 施工单（Track 10D）：Encoder 异构多文件动态任务队列与三端架构重构

- **文档编号**：`CINERTX-P10-TRACK-10D`
- **对应总纲任务**：任务 6（Encoder 模式要有任务队列，可以动态增减任务；由于每个任务的转码参数、文件个数都不一样，所以 Encoder 要重新设计） & 任务 1（GUI 全局状态提纯）
- **核心目标**：彻底废除只能存文件路径字符串、共用单一静态参数的旧 `g_batchQueue`，在 `rtx_core` 层构建支持**单任务 1..N 个文件、每任务独立 `TranscodeProfile` 参数快照、运行期实时增删改排**的工业级 `rtx::EncoderJobQueue`，并打通 Win32 GUI、WebUI 与 CLI `--session-stdio`。

---

## 一、 为什么旧版 Encoder 必须推倒重来？（现状缺陷 vs 全新架构）

| 维度 | 旧版实现 (`v0.2.0` `g_batchQueue`) | Phase 10 全新架构 (`v0.3.0` `rtx::EncoderJobQueue`) |
| :--- | :--- | :--- |
| **队列元素粒度** | `std::wstring`（仅 1 个文件路径字符串） | **`rtx::EncoderJob` 聚合体**：包含 `job_id`、**`inputs`（`1..N` 个输入文件列表）**、`output_dir`、**专属 `TranscodeProfile` 完整参数快照**、实时状态与统计报告。 |
| **参数隔离性** | **无隔离**：队列里所有文件强制使用点击“开始”瞬间界面上的同一套参数。 | **100% 任务级隔离**：Job #1 可设为 `AV1 NVENC CQ22 + 4K + 120-rife (3 个文件)`，Job #2 可设为 `HEVC CPU CRF18 + Native + 60-nvofa (5 个文件)`。 |
| **运行期动态性** | **静态锁死**：点击开始后拷贝 `queue = g_batchQueue` 并锁死参数面板，转码途中无法加减任务或改参数。 | **全动态非阻塞调度**：转码运行途中，右侧参数面板保持完全可操作！用户可随时配置新参数并点击 `[+ 加入队列]` 动态追加新任务，或删除/上移/下移/编辑任意 `Pending` 任务。 |
| **单文件 vs 多文件任务** | 1 个文件 = 1 行，100 个同参数片段会刷屏且无法成组管理。 | **单任务支持 1..N 个文件**：1 个 `EncoderJob` 既可以是单个视频（精确指定输出文件名），也可以是包含 N 个视频的批次组（自动按该任务的 `TranscodeProfile` 派生各自输出路径，共享同一任务卡片与子进度 `File k/N`）。 |

---

## 二、 核心领域模型与调度器设计 (`include/rtx/job_queue.h` & `src/core/job_queue.cpp`)

### 2.1 `include/rtx/job_queue.h` 完整 C++26 接口契约

```cpp
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

struct EncoderJob {
    uint64_t job_id = 0;                                  // Unique monotonic ID (1, 2, 3...)
    std::string name{};                                   // Auto-summary or user label
    std::vector<std::filesystem::path> inputs{};          // 1..N input media files
    std::filesystem::path explicit_output{};              // Used when inputs.size() == 1 and custom output is set
    std::filesystem::path output_directory{};             // Optional output folder override for multi-file jobs
    TranscodeProfile profile{};                           // Per-job independent SSOT parameter snapshot

    // Runtime telemetry
    JobStatus status = JobStatus::Pending;
    size_t current_file_index = 0;                        // 0 .. inputs.size() - 1
    double current_file_pct = 0.0;                        // 0.0 .. 100.0
    double total_job_pct = 0.0;                           // ((current_file_index * 100.0) + current_file_pct) / inputs.size()
    double current_fps = 0.0;
    double elapsed_ms = 0.0;
    std::string error_message{};
    std::vector<HwTranscodeReport> file_reports{};

    // Formats a concise human-readable summary for the Queue Table UI:
    // e.g. "3 files | AV1 CQ22 | 4K VSR4 | 120-rife (vsr-first)"
    [[nodiscard]] std::string formatSummary() const;
    [[nodiscard]] std::string toJson() const;
};

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

class EncoderJobQueue {
public:
    using EventCallback = std::function<void(const JobQueueEvent&)>;
    using RunnerFunc = std::function<Result<HwTranscodeReport>(const HwTranscodeOptions&)>;

    explicit EncoderJobQueue(RunnerFunc custom_runner = nullptr);
    ~EncoderJobQueue() noexcept;

    EncoderJobQueue(const EncoderJobQueue&) = delete;
    EncoderJobQueue& operator=(const EncoderJobQueue&) = delete;

    void setEventCallback(EventCallback cb);

    // --- Dynamic Queue Mutation API (Thread-Safe while worker is running!) ---

    // Enqueues a new job with its own 1..N input files and independent TranscodeProfile.
    [[nodiscard]] Result<uint64_t> enqueueJob(
        std::vector<std::filesystem::path> inputs,
        const TranscodeProfile& profile,
        std::filesystem::path output_dir = {});

    // Updates the TranscodeProfile or file list of a Pending job in-place.
    [[nodiscard]] Result<void> updatePendingJob(
        uint64_t job_id,
        const TranscodeProfile& new_profile,
        std::optional<std::vector<std::filesystem::path>> new_inputs = std::nullopt);

    // Removes a Pending/Completed/Failed/Cancelled job. (Returns error if job_id is currently Running; use skipCurrentJob instead).
    [[nodiscard]] Result<void> removeJob(uint64_t job_id);

    // Moves a Pending job up (-1) or down (+1) in execution priority.
    [[nodiscard]] Result<void> moveJob(uint64_t job_id, int delta);

    // Clears all non-running jobs (or all Pending/Completed jobs).
    void clearNonRunningJobs(bool completed_only = false);

    // Resets a Failed/Cancelled job back to Pending so the worker picks it up again.
    [[nodiscard]] Result<void> retryJob(uint64_t job_id);

    // --- Execution Control API ---

    // Starts or resumes the background worker thread draining Pending jobs.
    void startWorker();

    // Cooperatively cancels ONLY the currently running job and automatically advances to the next Pending job!
    void skipCurrentJob();

    // Cooperatively stops the currently running job and pauses the queue worker.
    void stopQueue(bool wait_for_idle = true);

    [[nodiscard]] bool isWorkerRunning() const noexcept;
    [[nodiscard]] std::vector<EncoderJob> snapshotJobs() const;
    [[nodiscard]] std::optional<EncoderJob> findJob(uint64_t job_id) const;

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
```

---

## 三、 Win32 GUI 与 WebUI Encoder 工作站重设计

### 3.1 Win32 GUI (`src/gui/rtx_gui.cpp` & `src/gui/workstation_canvas.cpp`)
1. **消除 21 个散装全局变量，收敛为单一 `WorkstationApp` 结构体**：
   - 将 `g_batchQueue`、`g_workerThread`、`g_cancelRequested` 等全部替换为 `rtx::EncoderJobQueue queue` 成员。
2. **左侧/中下部升级为多列任务队列视图（Task Queue ListView `WC_LISTVIEWW` `LVS_REPORT`）**：
   - 包含 5 列：
     - `ID`（`#1`, `#2`...）
     - `输入源 (Inputs)`（单文件显示文件名 `clip.mp4`；多文件显示 `clip1.mp4 (+3 files)`）
     - `独立编码参数 (Profile)`（如 `AV1 CQ22 | 4K VSR4 | 120-rife`）
     - `状态 (Status)`（`Pending` / `Running (2/4)` / `Done` / `Failed`）
     - `进度/速度 (Progress)`（`64.2% @ 88.5 FPS`）
3. **直观高效的交互闭环（对标 Adobe Media Encoder / HandBrake Queue）**：
   - **新建并入队（动态追加）**：
     - 用户在右侧参数面板调整好任意参数（如 `AV1 + CQ 22 + 4K + 120-rife`），通过文件选择框（支持 `OFN_ALLOWMULTISELECT` 多选 1..N 个文件）或拖拽 1..N 个文件进入窗口，点击 **`[+ 加入队列 (Add Job)]`**（`IDC_BTN_QUEUE_ADD`），立即将这组文件连同当前面板参数打包为一个独立的 `EncoderJob` 压入 `EncoderJobQueue`！
     - **即使当前队列正在后台转码，右侧参数面板也绝不锁死（Disable）**！用户可以立刻修改右侧面板参数（例如改为 `HEVC CPU + CRF 20 + 1080p`），再选另一批文件点击 **`[+ 加入队列]`**，新任务直接排入队尾等待执行！
   - **选中回显与原地修改排队任务（Inspect & Live-Edit Pending Job）**：
     - 在任务队列表格中点击任意处于 `Pending` 状态的任务行，右侧参数面板自动回显该任务的 `job.profile`；此时用户修改右侧任意下拉框或数值，自动调用 `queue.updatePendingJob(selected_job_id, updated_profile)` 实时更新该任务的专属参数！
   - **动态管理按钮组**：
     - `[+ 加入队列 (Add Job)]`（`IDC_BTN_QUEUE_ADD`）：将当前所选 1..N 个文件 + 当前面板参数新建为独立任务入队。
     - `[- 移除任务 (Remove)]`（`IDC_BTN_QUEUE_REMOVE`）：移除选中的非运行任务；若选中正在运行的任务则提示或执行跳过。
     - `[↑ 上移]` / `[↓ 下移]`（`IDC_BTN_QUEUE_UP` / `IDC_BTN_QUEUE_DOWN`）：动态调整 `Pending` 任务的执行顺序。
     - `[⏭ 跳过当前 (Skip)]` / `[⏹ 停止队列 (Stop)]`：精细控制单任务跳过与全队列启停。

### 3.2 CLI `--session-stdio` 与 WebUI (`webui/server.py` & `webui/app.js`) 同步升级
1. 在 `src/cli/main.cpp` 的 `--session-stdio` 模式中，支持流式 JSONL 队列协议：
   - 输入指令支持：
     - `{"cmd": "enqueue", "inputs": ["a.mp4", "b.mp4"], "profile": {...}}`
     - `{"cmd": "update", "jobId": 2, "profile": {...}}`
     - `{"cmd": "remove", "jobId": 2}`
     - `{"cmd": "move", "jobId": 3, "delta": -1}`
     - `{"cmd": "skip"}` / `{"cmd": "stop"}`
2. 在 `webui/server.py` 与 `webui/app.js` 中：
   - `POST /api/job` 扩展支持 `action: "enqueue" | "update" | "remove" | "move" | "skip" | "retry" | "start" | "stop"`。
   - WebUI 左侧队列面板展示每个 `EncoderJob` 的文件数徽标（如 `4 files`）、独立参数标签徽章（`AV1 CQ22` / `CPU CRF18` / `4K 120-rife`）、子文件进度与上下移/删除按钮。

---

## 四、 Track 10D 独立验证门禁（Checklist）

- [ ] **单测门禁 (`tests/test_gui_workstation.cpp`)**：
  - 新增 `test_encoder_job_queue_heterogeneous_dynamic_mutation`：
    1. 使用 Mock Runner 启动 Job #1（含 2 个输入文件，`codec="av1"`, `rate_control="cq"`, `qp=22`）；
    2. 当 Job #1 正在第 1 个文件执行途中（`status == JobStatus::Running`），并发调用 `enqueueJob` 追加 Job #2（含 3 个输入文件，`codec="hevc_cpu"`, `rate_control="crf"`, `qp=18`）和 Job #3（含 1 个输入文件，`codec="av1"`, `rate_control="qp"`, `qp=26`）；
    3. 在 Job #1 仍在运行时，调用 `moveJob(job3_id, -1)` 将 Job #3 提至 Job #2 之前，并调用 `updatePendingJob(job2_id, ...)` 将 Job #2 的 `qp` 改为 `20`；
    4. 等待队列执行完毕，断言实际执行顺序严格为 `Job #1 (2 files) -> Job #3 (1 file) -> Job #2 (3 files, qp=20)`，总共 6 次转码调用的参数与各自所属 `EncoderJob` 100% 吻合！
