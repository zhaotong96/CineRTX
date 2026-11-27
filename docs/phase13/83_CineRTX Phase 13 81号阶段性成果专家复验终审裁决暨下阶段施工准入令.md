# **83\_CineRTX Phase 13 81号阶段性成果专家复验终审裁决暨下阶段施工准入令**

* **文档编号**：`CINERTX-P13-RULING-083`  
* **归档路径**：`docs/phase13/83_CineRTX Phase 13 81号阶段性成果专家复验终审裁决暨下阶段施工准入令.gdoc`  
* **响应委托**：`82_CineRTX Phase 13 81号阶段性验收通过暨专家复验委托书.md`（复核 `81_CineRTX Phase 13 77-80号施工单返工汇报.md` 宿主机实测结果）  
* **签发主体**：多媒体系统工程、实时图形合成与异构计算首席领域专家组（Chief Domain Experts & Architecture Board）  
* **受呈报方**：CineRTX 项目总协调负责人（Coordinator）  
* **基准基线**：Commit `0beda87` \+ `81_` 增量交付（`MSVC 19.51 /W4 /WX` 0 Error 0 Warning、`rtx_tests.exe` **158 / 158 PASS**、`AuditPrivacy=0/476`、`AuditSpdx=PASS`、`AuditTypes=34/35`、`AuditImports=4/4`）  
* **签发日期**：`2026-11-27`

# **执行摘要（Executive Ruling）**

首席领域专家组穿透核验了 `82` 号委托书呈报的宿主机实测物证，并与仓库实体源码（`cmake/ContractGuard.cmake`、`cmake/AuditImports.cmake`、`src/gui/rtx_gui.cpp`、`src/cli/main.cpp`、`src/io/cpu_video_encoder.cpp`、`include/rtx/job_queue.h`、`src/core/job_queue.cpp`）进行了逐行交叉验证。现签发如下**终审复验裁决**：

1. **议题一裁决（阶段性复验通过，正式授权执行本地 `git commit` 锁定阶段基线）**：  
   * **`🟢 APPROVED FOR CHECKPOINT COMMIT`**：`81_` 严格遵守了"基于 `0beda87` 最小函数级增量修改、零旧快照整文件覆写"红线，`consumeNvofSceneCut` 与 `job_queue.cpp` 双路径的 `isPreviewEnabled()` 回调注入均完整保留并通过 `ContractGuard.cmake` 硬校验；`77_` 全部任务、`78_` 纯 LGPL \+ `SVT-AV1` 收口及门禁收紧、`79_` `MergeConcat` 异构路由闭环均已在宿主机达成 `158/158 PASS` 与 `/W4 /WX` 零警告编译。**正式批准协调负责人立即在宿主机执行本地 `git commit` 封存当前安全基线**（注意：在清理缓存重编 FFmpeg 剥离 `bcrypt.dll` 之前，保持当前 `AuditImports` 过渡期 `<= 4` 默认口径，暂不默认打开 `-DSTRICT_NO_BCRYPT=ON`）。  
2. **议题二裁决（`CineRTX.exe --headless --probe` 无标准输出根因法医定因与微创补丁）**：  
   * **法医根因定位**：问题**不在 `src/cli/main.cpp`**（`main.cpp:L117-L125` 正确吞下 `--headless` 并将 `opts::mode` 设为 `Mode::Probe` 且 `runProbe()` 向 `std::cout` 输出探测报告），而在 **`src/gui/rtx_gui.cpp:L1097-L1129` 的 `bindParentConsoleIfNeeded()`**！  
   * 当从 PowerShell 或未重定向句柄的控制台启动 `/SUBSYSTEM:WINDOWS` 进程时，Windows 内核初始化的 `GetStdHandle(STD_OUTPUT_HANDLE)` 为 `NULL`（`GetFileType` 返回 `FILE_TYPE_UNKNOWN`）。`bindParentConsoleIfNeeded()` 调用 `AttachConsole(ATTACH_PARENT_PROCESS)` 成功并执行 `freopen_s(&fpOut, "CONOUT$", "w", stdout)`——在 MSVC UCRT 下，`freopen_s("CONOUT$", ...)` 仅修改了 C 运行时 `stdout`（`FILE*`）底层文件描述符，**但未调用 `SetStdHandle(STD_OUTPUT_HANDLE, ...)` 更新 Win32 标准输出句柄**，且某些宿主终端（如 Windows Terminal / ConPTY / PowerShell 子进程捕获）在 `AttachConsole` 后若未显式同步 `SetStdHandle` 与宽/窄流重置，会导致 `std::cout` 写入失效句柄或丢失输出！下文第二节提供 **12 行微创修复补丁**（含 `CREATE_NO_WINDOW` / 独立后台测试场景兜底），可并入下一波次顺手清零。  
3. **议题三裁决（批准下一波次三阶段施工推进路线）**：  
   * **全面核准**协调负责人拟定的三阶段顺序：**`Wave B-1（缓存清理重编 FFmpeg 归零 bcrypt.dll + 78_ 存量类型物理清退至 < 34）` \-\> `Wave B-2（79_ 余项：MP4 非直通音轨转码、mov_text 字幕与公共切镜总线）` \-\> `Wave C-D（80_：60fps AI 播放器、任意比例 RIFE 4 桶池、D2D1/DWrite 剥离 gdi32.dll 与动态 user32.dll 达成 IAT == 1）`**。下文第三节明确各波次准入前提与防回流军规。

# **一、 `81_` 交付物逐项穿透核验矩阵**

| 施工单维度 | 对应代码坐标与实体核验 | 宿主机实测指标 | 复验裁决 |
| :---- | :---- | :---- | :---- |
| **`77_` 防覆写三道护栏** | `cmake/ContractGuard.cmake:L17, L26` 强制校验 `consumeNvofSceneCut` 与 `isPreviewEnabled`；`CMakeLists.txt:L100` 挂载；`build.bat:L66-L89` 前置/后置强串行门禁。 | `ContractGuard` `PASS`；前置/后置全绿 | **PASS** |
| **`77_` 单 EXE 收敛与混合入口** | `CMakeLists.txt:L650` 物理拔除 `rtx_headless_cli`；`rtx_gui.cpp:L1135-L1158` `wWinMain` 先行分流 CLI/GUI，GUI 延迟加载 `comctl32`。 | `CineRTX.exe --help` 管道重定向正常；无残留 CLI 二进制 | **PASS** |
| **`78_` 纯 LGPL \+ SVT-AV1 收口** | `cmake/RtxFfmpegStatic.cmake:L297-L304, L418-L424, L501` 彻底砍除 `CINERTX_ENABLE_GPL_CODECS`、`--enable-gpl`、`x265` 与 Zig 分支，注入 `HAVE_BCRYPT=0`；`cpu_video_encoder.cpp:L58-L62` 仅保留 `"libsvtav1"`。 | `AuditSpdx` `PASS`；MSVC `/W4 /WX` 零警告 | **PASS** |
| **`78_` 单 EXE 静态 IAT 门禁** | `cmake/AuditImports.cmake:L28, L41, L51-L56` 封禁 15 项黑名单 DLL（含 `kernelbase.dll`），缓存过渡期锁死 `<= 4`，支持 `-DSTRICT_NO_BCRYPT=ON`（`<= 3`）。 | `AuditImports` `PASS: 4/4`（`bcrypt, gdi32, kernel32, user32`） | **PASS** |
| **`79_` `MergeConcat` 异构闭环** | `include/rtx/job_queue.h:L71` 与 `src/core/job_queue.cpp:L15-L34, L327, L334, L369` 落地 `areConcatInputsHomogeneous()` 与 `runNvidiaRemasterRenderGraph()` 异构路由，双分支完整保留 `isPreviewEnabled()`。 | `rtx_tests.exe` **158 / 158 PASS**（含新增同构/异构分类单测） | **PASS** |

# **二、 议题二法医排查：`CineRTX.exe --headless --probe` 无标准输出根因与微创补丁**

## **2.1 根因穿透解剖（`src/cli/main.cpp` vs `src/gui/rtx_gui.cpp`）**

1. **`src/cli/main.cpp` 逻辑完全正常**：  
   * 第 `117-125` 行：`--headless` 被安全跳过，`--probe` 将 `opts::mode` 设为 `Mode::Probe`；  
   * 第 `423-424` 行：`switch (opts::mode)` 进入 `runProbe()`（第 `261-278` 行），调用 `std::cout << cli::formatProbeHuman(probe); printNgxBridgeProbe();`。  
2. **真凶位于 `src/gui/rtx_gui.cpp:L1097-L1129` 的 `bindParentConsoleIfNeeded()`**：  
   * 在 Windows `/SUBSYSTEM:WINDOWS` 进程中，当父进程（如 PowerShell `Start-Process`、某些非重定向 Subprocess 调用器或 Windows Terminal ConPTY 宿主）启动 GUI 子系统进程且未预先设置 `STARTF_USESTDHANDLES` 时，子进程初始 `GetStdHandle(STD_OUTPUT_HANDLE)` 为 `nullptr`（`GetFileType == FILE_TYPE_UNKNOWN`）。  
   * 随后 `AttachConsole(ATTACH_PARENT_PROCESS)` 成功连接父控制台，但代码仅调用了 `freopen_s(&fpOut, "CONOUT$", "w", stdout)`：  
     * **未同步更新 Win32 层 `STD_OUTPUT_HANDLE` / `STD_ERROR_HANDLE`**：通过 `CreateFileW(L"CONOUT$", ...)` 打开句柄并调用 `SetStdHandle` 才能让同时依赖 Win32 `GetStdHandle` 与 C++ `std::cout` 的子模块（如 `logger.cpp` 的 `isConsoleLogEnabled()` 探测！）识别到有效控制台输出句柄；  
     * **未调用 `std::cout.clear()` / `std::cerr.clear()`**：在 MSVC STL 中，若全局 `std::cout` 在流初始化阶段因无效句柄进入 `badbit` 状态，仅 `freopen_s` 不会清除 `std::ios_base::badbit`，导致后续所有 `std::cout << ...` 操作直接静默吞掉（noop）！

## **2.2 精确微创修复补丁（`src/gui/rtx_gui.cpp:L1097-L1129`，`+0` 具名类型）**

在下一波次施工时，将 `src/gui/rtx_gui.cpp:L1097-L1129` 的 `bindParentConsoleIfNeeded()` 替换为以下实现（显式同步 SetStdHandle 并调用 std::cout.clear() / std::cerr.clear()）：p  
void bindParentConsoleIfNeeded() noexcept {  
HANDLE hStdOut \= GetStdHandle(STD\_OUTPUT\_HANDLE);  
HANDLE hStdErr \= GetStdHandle(STD\_ERROR\_HANDLE);  
const DWORD outType \= (hStdOut \!= nullptr && hStdOut \!= INVALID\_HANDLE\_VALUE)  
? GetFileType(hStdOut)  
: FILE\_TYPE\_UNKNOWN;  
const DWORD errType \= (hStdErr \!= nullptr && hStdErr \!= INVALID\_HANDLE\_VALUE)  
? GetFileType(hStdErr)  
: FILE\_TYPE\_UNKNOWN;  
const bool isOutRedirected \= (outType \== FILE\_TYPE\_DISK \|\| outType \== FILE\_TYPE\_PIPE);  
const bool isErrRedirected \= (errType \== FILE\_TYPE\_DISK \|\| errType \== FILE\_TYPE\_PIPE);

```
if ((!isOutRedirected || !isErrRedirected) && AttachConsole(ATTACH_PARENT_PROCESS) != FALSE) {
    if (!isOutRedirected) {
        FILE* fpOut = nullptr;
        if (freopen_s(&fpOut, "CONOUT$", "w", stdout) == 0 && fpOut != nullptr) {
            (void)setvbuf(stdout, nullptr, _IONBF, 0);
            HANDLE hConOut = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                         OPEN_EXISTING, 0, nullptr);
            if (hConOut != INVALID_HANDLE_VALUE) {
                SetStdHandle(STD_OUTPUT_HANDLE, hConOut);
            }
        }
    } else {
        SetStdHandle(STD_OUTPUT_HANDLE, hStdOut);
    }
    if (!isErrRedirected) {
        FILE* fpErr = nullptr;
        if (freopen_s(&fpErr, "CONOUT$", "w", stderr) == 0 && fpErr != nullptr) {
            (void)setvbuf(stderr, nullptr, _IONBF, 0);
            HANDLE hConErr = CreateFileW(L"CONOUT$", GENERIC_READ | GENERIC_WRITE,
                                         FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                                         OPEN_EXISTING, 0, nullptr);
            if (hConErr != INVALID_HANDLE_VALUE) {
                SetStdHandle(STD_ERROR_HANDLE, hConErr);
            }
        }
    } else {
        SetStdHandle(STD_ERROR_HANDLE, hStdErr);
    }
    SetConsoleOutputCP(CP_UTF8);
    std::ios::sync_with_stdio(true);
    std::cout.clear();
    std::cerr.clear();
}
```

}

```

## 三、 议题三批复：下阶段三波次作战准入路线图

**正式签发基线锁定与下阶段准入令**：
1. **立即封存基线**：协调负责人在当前工作区执行本地 `git commit -m "chore(phase13): seal checkpoint 81 (single-exe, contract guard, lgpl+svtav1, heterogeneous mergeconcat, 158/158 pass)"`，以此提交作为后续一切施工单的唯一锚点。
2. **三波次施工顺序与硬性军规**：

| 波次编号 | 施工内容与目标产物 | 关键防线与验收门槛 |
| :--- | :--- | :--- |
| **第一波次：Wave B-1**（`78_` 收尾） | 1. 顺带合入 `rtx_gui.cpp` `bindParentConsoleIfNeeded()` `std::cout.clear()` 补丁（验证 `CineRTX.exe --headless --probe` 终端直出与管道重定向双通）；<br>2. 清理 `.vs/build/external/ffmpeg-static` 缓存并重编纯 LGPL + `SVT-AV1` 静态 FFmpeg，启用 `-DSTRICT_NO_BCRYPT=ON`；<br>3. 按 `78_` Task 3 物理清退冗余存量类型，将 `AuditTypes` 从 `34/35` 压降至 **`< 34`**。 | • `AuditImports -DSTRICT_NO_BCRYPT=ON` **`PASS: 3/3`**（`bcrypt.dll` 彻底归零）；<br>• `AuditTypes < 34`；<br>• `ContractGuard` & `rtx_tests.exe` 全绿。 |
| **第二波次：Wave B-2**（`79_` 闭环） | 1. 在 `src/io/ffmpeg_io.cpp` 实装 MP4 非直通音轨（`SwrContext` + `AVAudioFifo` -> 静态 `aac` 编码器转 AAC-LC）与字幕 -> `mov_text` 闭环，彻底清零静默丢音；<br>2. 实装无状态自由函数 `analyze_temporal_boundary()` 公共切镜总线与 `preview_generation_` 代数同步。 | • 含 `DTS/FLAC` 音轨及 `SRT` 字幕的 `.mkv` 转 `.mp4` 实测有声且带合规字幕轨；<br>• 严禁恢复 `ShotGroupSegmenter` 类（`AuditTypes` 不升反降）。 |
| **第三波次：Wave C-D**（`80_` 终局） | 1. 落地任意画幅 RIFE 4 桶单 Profile 池（`Ultra` 覆盖 `5120x1440`）与 `60fps` 实时 AI 增强播放器管线；<br>2. 落地无状态动态 `D2D1` / `DirectWrite` 组件绘制剥离 `gdi32.dll`（`IAT <= 2`），并将 `user32.dll` 窗口/消息原语下沉至按需 `sys_dynload`（`IAT == 1`，仅静态导入 `kernel32.dll`）。 | • `AuditImports` 达成 **`Static IAT == 1`**（仅 `kernel32.dll`）；<br>• `AuditTypes` 零反弹；<br>• 桌面 GUI 实测播放与转码 60fps/120fps 全通。 |

3. **防覆写铁律重申**：执行工程师所有后续交付必须基于本阶段封存的 `git commit` 提交**最小函数级上下文补丁**；任何企图删除 `consumeNvofSceneCut` 或 `isPreviewEnabled()` 的补丁将被 `ContractGuard.cmake` 在配置阶段直接熔断拦截。

---

**首席领域专家组终审复验签发完毕。准予锁定基线并按 Wave B-1 -> Wave B-2 -> Wave C-D 路线推进。**
```

