# CineRTX Phase 11.3-R SuperPower 施工单：极限现代 C++20/23/26 重构与 NLE 全栈合龙（专家直通版）

- **文档编号**：`CINERTX-P11-WORKORDER-039`（全面替代并废止 `CINERTX-P11-WORKORDER-036`）
- **归档路径**：[`docs/phase11/39_CineRTX Phase 11.3-R SuperPower 施工单_极限现代C++重构与NLE全栈合龙.md`](39_CineRTX%20Phase%2011.3-R%20SuperPower%20施工单_极限现代C++重构与NLE全栈合龙.md)
- **承接角色**：Round 3 高级执行工程师（高级 C++ 系统架构与算法工程师，无头离线环境）
- **发单主体**：CineRTX 项目总协调负责人（Coordinator）
- **前置基线**：
  1. [`38 号专家终审裁决与极限重构方案`](38_CineRTX%20现代%20C%2B%2B%20极限重构审计.md)（已 100% 完成仓内相对化脱敏并封板）；
  2. 工作区已彻底物理归零回 Commit `749fe0d`（`AuditTypes.cmake` 150 具名类型 0 违规，`AuditSloc.cmake` 锁定入场纯净基准 31,064 Pure SLOC，工作树 100% clean）；
  3. 接口正交分工已确立：第 1 项（声学静音）与第 3 项（CMake 构建清洗）由负责人在本地合龙收尾，高级工程师专注于纯 C++20/23/26 语言与算法层重构。
- **依据母本**：[`38_CineRTX 现代 C++ 极限重构审计.md`](38_CineRTX%20现代%20C%2B%2B%20极限重构审计.md) · [`37_CineRTX Phase 11.3 施工验收与极限现代C++重构专家咨询委托书.md`](37_CineRTX%20Phase%2011.3%20施工验收与极限现代C++重构专家咨询委托书.md) · [`35_CineRTX Phase 11.2-R 终审方案.md`](35_CineRTX%20Phase%2011.2-R%20终审方案.md)
- **建议工期**：2–3 个有效工作日
- **核心目标**：贯彻“能用 std 绝不建类”的最高代码洁癖，以 6 支 `std::variant` 代数和、Deducing this、单遍扫描线算法、`_mm256_fmadd_ps`、`std::mdspan` 及 `constexpr` 内生描述符表重写 NLE 与音频总线，**刚性兑现纯净代码行 $\le 30,664$ Pure SLOC（净减 -400 至 -600 行）平账预算**。

---

## 〇、 外部高级工程师前置纪律与红线契约

为杜绝重演此前两轮外包中发生的“注释欺诈”、“破坏 RFC 8259 尾逗号”、“全局重载 operator new 劫持”等低级恶性错误，Round 3 高级执行工程师必须严格遵守以下四项刚性红线：

1. **版本库权限与文件边界隔离**：
   高级工程师处于无头离线环境，**严禁且无法访问 Git 版本库**。高级工程师仅允许在指定的 7 处核心源文件与 2 处测试文件中实施纯 C++ 代码重构，**严禁触碰 `cmake/`、`docs/`、根 `CMakeLists.txt` 或执行任何 git 检出/分支指令**。
2. **“能用 std 绝不建类”与白名单存续律**：
   全仓内部逻辑层严禁任何伪面向对象样板包装类（Wrapper Slop）。除硬件对齐布局（如 DXBC cbuffer、CUDA 内存对齐）、跨模块纯 C ABI、多态边界及核心领域模型外，**所有自建类只要能用标准库设施（`std::variant`、`std::tuple`、`std::span`、`std::optional`、`std::expected`）表达的一律不准存在**。存续类型必须在声明上方 8 行内附带合规 `@rtx-type-justification`。
3. **单测运行库绝对隔离律（严禁全局重载 `operator new`）**：
   在编写 [`tests/test_audio_mixer_avx2.cpp`](../../tests/test_audio_mixer_avx2.cpp) 时，**严禁在全局作用域替换 `operator new/delete`**。热循环零堆分配验证必须通过局部作用域隔离机制、栈定容数组断言或静态类型契约达成，绝不可劫持链接进 `rtx_tests.exe` 的其余 128 项单测。
4. **代码注释零删减与 RFC 8259 JSON 纯洁性**：
   严禁删减任何业务注释、SPDX 许可证头或压缩空行凑数；`toJson()` 工程序列化**严禁在数组末尾生成非法逗号（尾逗号）**，`fromJson()` 严禁使用 `throw/catch` 做局部控制流跳转。

---

## 一、 核心重构任务与实施规范

### 任务 1：根域 `schema/` 体系拔除与 C++23 内生描述符表落地
**修改/删除文件**：彻底物理移除 `scripts/` 与 `schema/` 目录；修改 [`include/rtx/param_schema.h`](../../include/rtx/param_schema.h)、[`src/core/param_schema.cpp`](../../src/core/param_schema.cpp)、[`tests/test_param_schema_reflection.cpp`](../../tests/test_param_schema_reflection.cpp)

1. **物理清空死滞目录**：
   在交付包中物理移除 `scripts/`（含 `gate_r8_desktop_check.ps1` 与 `reimplant_phase112r_comments.py`）及 `schema/`（含 `reflection_codegen.cpp`、`schema_model.hpp`、`verify_ssot.py` 与 `schema/generated/` 下的 3 份派生头文件）。
2. **落地 C++23 内生编译期参数描述符表**：
   在 `include/rtx/param_schema.h` 中彻底替代旧体外生成器，以纯原生 C++23 表达 20 项参数元数据与成员指针绑定，并以 `consteval` 静态断言在编译期拦截键名重复与范围越界：
   ```cpp
   // include/rtx/param_schema.h
   using ProfileMemberPtr = std::variant<std::string TranscodeProfile::*, int TranscodeProfile::*, bool TranscodeProfile::*>;

   inline constexpr std::array<std::pair<ParamMeta, ProfileMemberPtr>, 20> kParamDescriptors{{
       {{.json_key = "input",  .cli_flag = "--input",  .kind = ParamKind::String, .label_id = StrId::ParamInput},  &TranscodeProfile::input},
       {{.json_key = "output", .cli_flag = "--output", .kind = ParamKind::String, .label_id = StrId::ParamOutput}, &TranscodeProfile::output},
       // ... 其余 18 项参数严格对齐原 20 字段定义 ...
   }};

   consteval bool validateParamDescriptors() {
       for (std::size_t i = 0; i < kParamDescriptors.size(); ++i) {
           const auto& a = kParamDescriptors[i].first;
           if (a.json_key.empty()) return false;
           if (a.kind == ParamKind::Int && (a.default_int < a.min_val || a.default_int > a.max_val)) return false;
           for (std::size_t j = i + 1; j < kParamDescriptors.size(); ++j) {
               const auto& b = kParamDescriptors[j].first;
               if (a.json_key == b.json_key) return false;
               if (!a.cli_flag.empty() && a.cli_flag == b.cli_flag) return false;
           }
       }
       return true;
   }
   static_assert(validateParamDescriptors(), "Duplicate or out-of-range SSOT parameter descriptor");

   template <typename Profile, typename Visitor>
   constexpr void forEachProfileField(Profile& profile, Visitor&& visitor) {
       for (const auto& [meta, ptr] : kParamDescriptors) {
           std::visit([&](auto member) { visitor(meta, profile.*member); }, ptr);
       }
   }
   ```
3. **收敛下游实现与单测包含**：
   在 `src/core/param_schema.cpp` 中直接消费 `kParamDescriptors` 实现 `exportSchemaJson()` 与 CLI 绑定；在 `tests/test_param_schema_reflection.cpp` 中彻底移除对 `schema/generated/...` 的依赖，保持全部 20 项参数 JSON 往返测试 100% 绿灯。

---

### 任务 2：通用基础设施删除器合并与时钟元组压缩
**修改文件**：[`include/rtx/raii_owners.h`](../../include/rtx/raii_owners.h)、[`src/core/playback_session_audio.cpp`](../../src/core/playback_session_audio.cpp)、[`src/core/playback_session.h`](../../src/core/playback_session.h)

1. **统一 `FnDeleter` 模板（消除 `IndirectDeleter`）**：
   在 `include/rtx/raii_owners.h` 中升级唯一的 `FnDeleter` 模板，利用 C++20 `requires` 表达式在编译期自动分派一级指针与二级指针释放函数，并以 C++23 `static operator()` 消除空状态传递 `this` 指针的寄存器开销：
   ```cpp
   template <auto Fn>
   struct FnDeleter {
       static void operator()(auto* p) noexcept {
           if (!p) return;
           if constexpr (requires { Fn(p); }) {
               Fn(p);
           } else {
               Fn(&p); // 统一接管 avcodec_free_context / swr_free / av_frame_free / av_packet_free 等二级指针
           }
       }
   };
   ```
   随后在 `src/core/playback_session_audio.cpp` 中物理删除自定义的 `IndirectDeleter`，全部切换为 `FnDeleter`。
2. **压缩 `PlaybackClockState` 为 4 元组**：
   在 `src/core/playback_session.h` 中，将原易误用 `std::get<3/5>` 幻数的 6 元组压缩为 4 元组，以 `std::optional<steady_clock::time_point>` 表达暂停状态，在解包点全面改用结构化绑定：
   ```cpp
   using PlaybackClockState = std::tuple<IAudioMasterClock*, std::optional<std::chrono::steady_clock::time_point>, Flicks, double>;
   ```

---

### 任务 3：NLE 核心模型代数化重构（零命令类与 Deducing this）
**新建/修改文件**：[`include/rtx/nle_project.h`](../../include/rtx/nle_project.h)、[`src/core/nle_project.cpp`](../../src/core/nle_project.cpp)、[`tests/test_nle_vertical_slice.cpp`](../../tests/test_nle_vertical_slice.cpp)

1. **纯代数和类型 `NleEditDelta` 替代 7 个命令类**：
   物理删除 `AddClipCommand`、`TrimClipCommand`、`SplitClipCommand`、`MoveClipCommand`、`DeleteClipCommand`、`ReorderTracksCommand`、`RelinkAssetCommand`、`NleEditCommand` 与 `EditHistory` 包装类。将可逆编辑增量定义为纯标准库变体元组：
   ```cpp
   // include/rtx/nle_project.h
   using NleEditDelta = std::variant<
       std::pair<bool /*is_delete*/, TimelineClip>,                                                   // Add (false) / Delete (true)
       std::tuple<uint64_t /*clip_id*/, std::pair<int64_t, int64_t>, std::pair<int64_t, int64_t>>,   // Trim (old_in_out, new_in_out)
       std::tuple<uint64_t /*kept_id*/, uint64_t /*created_id*/, int64_t /*at*/, int64_t /*old_out*/>, // Split
       std::tuple<uint64_t /*clip_id*/, std::pair<int, int64_t>, std::pair<int, int64_t>>,           // Move (old_lane_start, new_lane_start)
       std::pair<std::array<int8_t, kNleTrackCount>, std::array<int8_t, kNleTrackCount>>,            // ReorderTracks (old, new)
       std::tuple<uint64_t /*asset_id*/, NleMediaAsset /*prev*/, NleMediaAsset /*verified*/>         // RelinkAsset
   >;
   using EditStacks = std::pair<std::deque<NleEditDelta>, std::deque<NleEditDelta>>; // undo_stack, redo_stack
   ```
2. **C++23 Deducing this 提纯 `NleProject`**：
   在 `NleProject` 中使用 Deducing this 单模板消除 const 与非 const 重载，彻底清除 `const_cast`：
   ```cpp
   struct NleProject {
       std::vector<NleMediaAsset> assets;
       std::vector<TimelineClip> clips;
       std::array<int8_t, kNleTrackCount> lane_stack;

       template <typename Self>
       [[nodiscard]] auto* clip(this Self& self, uint64_t id) noexcept {
           auto it = std::ranges::find(self.clips, id, &TimelineClip::id);
           return it == self.clips.end() ? nullptr : &*it;
       }

       template <typename Self>
       [[nodiscard]] auto* asset(this Self& self, uint64_t id) noexcept {
           auto it = std::ranges::find(self.assets, id, &NleMediaAsset::id);
           return it == self.assets.end() ? nullptr : &*it;
       }
   };
   ```
3. **无尾逗号序列化与无异常控制流**：
   - `NleProject::toJson()` 采用前置逗号或显式索引拼接，**严禁在数组末尾生成破坏 RFC 8259 规范的尾逗号（形如 `[{...},]`）**；
   - `NleProject::fromJson()` 彻底移除 `throw/catch`，内部解析错误直接以 `Result<T>`（`std::expected`）自底向上传递。

---

### 任务 4：单遍离散事件时间扫描线执行图调度
**新建/修改文件**：[`include/rtx/render_graph.h`](../../include/rtx/render_graph.h)、[`src/core/render_graph.cpp`](../../src/core/render_graph.cpp)、[`src/core/hw_transcode_stages.cpp`](../../src/core/hw_transcode_stages.cpp)

1. **物理删除 `VideoClaim` 与 `CompiledRenderGraph` 类包装**：
   在 `render_graph.h` 中直接声明纯函数，无中间包装类：
   ```cpp
   // include/rtx/render_graph.h
   struct RenderGraphSegment {
       std::string source_path;
       int64_t source_in_flicks{0};
       int64_t source_out_flicks{0};
       int64_t timeline_in_flicks{0};
       int64_t timeline_out_flicks{0};
       bool hard_cut{false};
       bool force_keyframe{false};
   };

   Result<std::vector<RenderGraphSegment>> compileRenderGraph(const NleProject& project);
   ```
2. **落地单遍坐标扫描线算法**：
   在 `src/core/render_graph.cpp` 中严格遵循 38 号文第 133–190 行规范实现：
   - 收集所有有效视频片段的 `start_flicks` 与 `start_flicks + duration` 存入 `cuts` 向量，`std::ranges::sort` 并 `unique` 形成离散时间切片 `[t0, t1)`；
   - 单遍遍历每个切片，使用 `std::views::filter` 过滤覆盖该区间的片段，以 `std::ranges::min_element` 根据轨道优先级（`lane_stack`）决出胜出片段；
   - 自动检测并合并同源相邻切片，在跨素材切点精准设置 `hard_cut = true` 与 `force_keyframe = true`。
3. **固化 NVENC 关键帧标记下发（C-05 闭环）**：
   在 `src/core/hw_transcode_stages.cpp` 的 `encodeWorkerBody()` 中确保 GPU 分支显式赋值：
   ```cpp
   encode_frame.keyframe = item.force_keyframe ? 1 : 0;
   ```

---

### 任务 5：零堆分配 AVX2 混音总线与 `std::mdspan` WSOLA 升级
**新建/修改文件**：[`include/rtx/audio_mixer_avx2.h`](../../include/rtx/audio_mixer_avx2.h)、[`src/core/audio_mixer_avx2.cpp`](../../src/core/audio_mixer_avx2.cpp)、[`src/core/wsola_stretch.cpp`](../../src/core/wsola_stretch.cpp)

1. **彻底拔除 `VoicePrep` 与 `AudioMixerScratchpad`**：
   `audio_mixer_avx2.h` 中仅定义代数类型别名与无异常自由函数：
   ```cpp
   // include/rtx/audio_mixer_avx2.h
   using AudioVoice = std::pair<std::span<const float>, float /*gain*/>;

   void mixAvx2(std::span<const AudioVoice> voices, std::span<float> dst) noexcept;
   ```
2. **AVX2 单指令融合乘加 `_mm256_fmadd_ps`**：
   在 `src/core/audio_mixer_avx2.cpp` 中，AVX2 路径必须调用单指令 FMA：
   ```cpp
   const __m256 vgain = _mm256_set1_ps(voice.second);
   // 循环内调用：
   const __m256 acc = _mm256_loadu_ps(d);
   const __m256 src = _mm256_loadu_ps(s);
   _mm256_storeu_ps(d, _mm256_fmadd_ps(src, vgain, acc));
   ```
3. **`std::mdspan` 二维多维切片与静态只读汉宁窗**：
   在 `src/core/wsola_stretch.cpp` 中：
   - 移除每会话动态分配的汉宁窗向量，替换为静态全局只读数组 `alignas(32) static const float kHannTable[...]`；
   - 引入 C++23 `std::mdspan<float, std::dextents<size_t, 2>>` 将交错音频缓冲封装为 `[frame, channel]` 视图，以 `view[frame, ch]` 彻底消灭手工 `i * ch + c` 步长算术。

---

### 任务 6：GUI 画布旧符号拔除、判空清零与跨线程防竞态
**修改文件**：[`src/gui/workstation_canvas.h`](../../src/gui/workstation_canvas.h)、[`src/gui/workstation_canvas.cpp`](../../src/gui/workstation_canvas.cpp)、[`src/gui/rtx_gui.cpp`](../../src/gui/rtx_gui.cpp)

1. **彻底拔除旧时间线符号（Gate 11.3-1 负向扫描零容忍）**：
   - 将新片段结构体统一命名为 **`TimelineClip`**（严禁定名为 `NleClip` 导致撞车旧正则）；
   - 彻底删除 `NleTimeline`、`paintNleTimeline`、`registerNleTimelineWindowClass`、`kFlicksPerMs`、`msToFlicks` 等历史符号；
   - `seekPlaybackFromSlider()` 统一采用 `std::chrono::duration_cast<Flicks>`，彻底消灭裸整数毫秒算术。
2. **按值持有消除 15 处重复判空防御**：
   在 `WorkstationApp` 中直接按值持有 `NleProject project;` 与 `EditStacks history;`，各业务入口以 `NleProject&` 引用传递，彻底物理删除 15 处 `if (g_app.nleProject == nullptr)` 冗余防御。
3. **消除导出线程全局状态竞态**：
   在 `onNleExportSequence()` 中，将跨线程状态 `g_guiTranscoding` 改造为 `std::atomic<bool>`，导出状态文字通过堆所有权安全投递，并在 `WM_DESTROY` 窗口销毁序列中保证显式调用 `export_thread.request_stop()` 与 `export_thread.join()` 后再释放窗口资源。

---

### 任务 7：单元测试编写（绝对隔离，严禁全局重载 `operator new`）
**新建/修改文件**：[`tests/test_nle_vertical_slice.cpp`](../../tests/test_nle_vertical_slice.cpp)、[`tests/test_audio_mixer_avx2.cpp`](../../tests/test_audio_mixer_avx2.cpp)

1. **`test_nle_vertical_slice.cpp` 全覆盖**：
   覆盖 `NleProject` 7 类编辑操作、Undo/Redo 状态回滚、`RelinkAsset` 校验、`toJson()` 无尾逗号往返一致性及 `compileRenderGraph` 多段硬切 IDR 标记断言。
2. **`test_audio_mixer_avx2.cpp` 零污染验证**：
   **绝对禁止编写全局 `void* operator new(size_t)` 替换代码**。热循环零堆分配验证必须通过调用方维护的栈定容数组 `std::array<AudioVoice, 32>`、局部 `_CrtSetAllocHook` 钩子或纯静态编译期契约达成，确保运行 `rtx_tests.exe` 时其余测试不受任何全局分配器劫持。

---

## 二、 严格准出验收门禁（Gate 11.3-R）

高级工程师在交付成果前，必须在代码结构上满足以下四项硬性指标（本地集成时由协调负责人运行 CMake 脚本执行刚性拦截）：

| 门禁编号 | 门禁检验项 | 验证命令 / 判定标准 | 刚性指标 |
| :--- | :--- | :--- | :--- |
| **Gate 11.3-R1** | **旧符号与违规全局宏零残留** | 负向扫描 `src/gui/workstation_canvas.*` 与 `tests/`：<br>`NleClip|NleTimeline|VoicePrep|AudioMixerScratchpad|VideoClaim` | **严格 0 命中（0 matches）** |
| **Gate 11.3-R2** | **全局 `operator new` 劫持清零** | 负向扫描 `tests/test_audio_mixer_avx2.cpp`：<br>`void\*\s+operator\s+new` | **严格 0 命中（0 matches）** |
| **Gate 11.3-R3** | **静态类型存续审计（Std-First）** | 运行 `cmake -P cmake/AuditTypes.cmake` | **0 违规，退出码为 0** |
| **Gate 11.3-R4** | **SLOC 技术赤字平账兑现** | 运行 `cmake -P cmake/AuditSloc.cmake` | **纯 SLOC $\le 30,664$ 行**（净减 -400 至 -600 行） |

---
*CineRTX 项目总协调负责人（Coordinator） 签发*  
*2026年11月21日*
