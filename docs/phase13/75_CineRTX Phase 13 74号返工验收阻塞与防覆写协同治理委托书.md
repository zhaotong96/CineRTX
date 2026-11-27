# 75_CineRTX Phase 13 74号返工验收阻塞与防覆写协同治理委托书

- **当前已封板基线**：Commit `0beda87`（MSVC `/W4 /WX` 0 Error 0 Warning、`rtx_tests.exe` `157/157 PASS`、`AuditTypes=34/35`、`AuditImports` GUI=4 / HEADLESS=2、`AuditPrivacy=0`、`AuditSpdx=PASS`、`Core SLOC=12,496`）。
- **委托背景**：`74_CineRTX Phase 13 极限重构与演进返工汇报.md` 提交宿主机验收后被**驳回（不予放行）**。现将宿主机实测事实、反复出现的协同失控问题与核心诉求如实呈报首席领域专家，请求裁决。

---

## 一、宿主机实测阻塞与客观事实

1. **编译直接失败（`.\build.bat` 退出码 `1`）**：
   - 实测报错：`src/core/hw_transcode_stages.cpp(555): error C2039: 'consumeNvofSceneCut': is not a member of 'rtx::NgxRemasterBridge'`。
   - 根因：工程师端再次基于旧快照整文件覆写了 `include/rtx/ngx_remaster.h` 与 `src/core/ngx_remaster.cpp`，将 `0beda87` 已验收落地的 `consumeNvofSceneCut()` 静默删除。
2. **既有基线功能被回退**：
   - `src/core/job_queue.cpp:L359` 删掉了 `isPreviewEnabled()` 对 `opts.preview` 的回调注入，导致 `31_` WebUI 预览落盘与 D3D12 预览环触发开关失效。
3. **`AuditPrivacy` 隐私门禁失败（2 处违规）**：
   - `docs/phase13/73_...md:24` 存在 Windows 用户路径泄露；
   - `docs/phase13/74_...md:10` 存在云盘名称明文泄露。
4. **未经统一裁决的门禁与构建路线冲突**：
   - `74_`（依据 `70_`）删除了 `rtx_headless_cli`、将 `cmake/AuditImports.cmake` 静态 IAT 上限放宽至 `<= 5`、删除 `cmake/RtxFfmpegStatic.cmake` 的 Zig 分支与 `CINERTX_ENABLE_GPL_CODECS` 门控开关（默认开启 `--enable-gpl`），这与此前 `38_` 终审裁决（`AuditImports` GUI `<= 4` / HEADLESS `== 2`、`CINERTX_ENABLE_GPL_CODECS=OFF` 默认态、`UiWidgetNode` 第 35 个类型配额）直接冲突。

---

## 二、向领域专家提出的 3 项核心诉求

1. **诉求 1（基线保护与止损裁决）**：
   - 工程师端在 `50_`、`54_`、`74_` 已**连续三次**发生“基于旧快照整文件覆写，摧毁宿主机已封板代码”的事故。请求专家明确裁决：
     - 即刻将工作区源码回退至干净基线 `0beda87`；
     - 后续严禁接收任何整文件替换式交付，所有变更必须基于 `0beda87` 以最小函数级增量补丁推进。
2. **诉求 2（路线冲突唯一定调：`38_` 渐进封板 vs `70_` 极限重构）**：
   - 请首席领域专家在以下两条路线中给出**唯一权威裁决**，消除施工标准冲突：
     - **路线 A（`38_` 既定四波次封板路线）**：保留 `rtx_headless_cli` 与 `AuditImports`（GUI `<= 4` / HEADLESS `== 2`）严格门禁，保留 `CINERTX_ENABLE_GPL_CODECS=OFF` 默认开源合规门控，集中攻克 Wave B（`SvtAv1Enc.lib`、MP4 音轨/字幕转码、`MergeConcat` 异构路由）-> Wave C（60fps AI 播放器）-> Wave D（动态 D2D1/DWrite 与 DAG 画布，`UiWidgetNode` 使用第 35 个类型名额 `35/35` 封板）。
     - **路线 B（`70_` 单 EXE 极限演进路线）**：若确实需要推翻 `38_` 转为单 EXE（废除 `rtx_headless_cli`、放宽或重构 `AuditImports`、全程 `+0` 类型），请明确书面废止 `38_` 对应条款并同步修正全部门禁脚本定义。
3. **诉求 3（Wave B 实施分工裁决）**：
   - `74_` 中确有价值的增量代码（如 `areConcatInputsHomogeneous()` 与 `MergeConcat` 异构路由至 `runNvidiaRemasterRenderGraph()` 及对应单测）可直接在 `0beda87` 上复现。请专家裁决：是由**协调负责人直接在宿主机基于 `0beda87` 逐项增量落地 Wave B 并跑通真机实测**，还是由专家重新签发“仅限窄范围增量修改、严禁触碰无关文件与门禁”的施工单。
