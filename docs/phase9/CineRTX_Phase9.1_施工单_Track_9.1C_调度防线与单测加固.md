# CineRTX Phase 9.1 施工单：Track 9.1-C 调度防线与单测加固

> **工单编号**: `CINERTX-P9.1-TRACK-C`  
> **任务名称**: `转码调度跨会话状态解耦、全败硬熔断阻断与全量单测加固`  
> **责任小组**: `系统调度与质量保障组`  
> **主导依据**: [`docs/phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md`](docs/phase9/CineRTX_Phase9_终审裁决与全链路真权重闭环SuperPower返工规范.md)  
> **关联缺陷**: 🟠 **RW-P9-04 (P1 严重)**、🔴 **RW-P9-01 (P0 阻断)**  
> **核心目标**: 彻底消除静态变量导致的跨转码任务状态污染死锁，建立插帧 100% 失败强行报错阻断机制，升级单测以覆盖 C ABI 纯净性与 FP16 精度

---

## 一、 被拒缺陷机理穿透 (RW-P9-04)

### 1. 缺陷 1：`static thread_local` 日志计数器导致常驻 GUI 跨任务失聪
在 [`src/core/hw_transcode.cpp`](src/core/hw_transcode.cpp#L1042) 中：
```cpp
// 历史被拒代码 (hw_transcode.cpp:1042)：
static thread_local int s_fruc_error_logs = 0;
if (s_fruc_error_logs < 5) {
    ++s_fruc_error_logs;
    // 打印错误日志...
}
```
**严重后果**：
- 当用户在 Win32 GUI 或 WebUI 常驻服务中连续执行多个转码任务时，由于工作线程复用，第 1 个任务如果报错满 5 次，`s_fruc_error_logs` 变成了 5 且永远不会被重置！
- 后续的第 2、3、N 个转码任务即便发生了严重的插帧崩溃或数据丢失，日志系统也会因该静态变量而**永久失聪**，排错信息被完全吞没！

### 2. 缺陷 2：插帧 100% 失败时转码仍以 ExitCode 0 成功退出
当用户明确指定 `--target-fps 120 --rife-engine ...` 请求 RIFE 补帧时，若因显存不足或模型错误导致所有插帧全部失败（`fruc_failures > 0 && frames_interpolated == 0`）：
旧逻辑在转码主循环结束后直接返回了 `Ok()`，生成的文件仍为源视频帧率（24fps），且进程以退出码 0 退出！这严重违反了 Phase 9 DoD 第 4 条“严禁静默回退”契约。

---

## 二、 施工目标与详细修改点

### 任务 9.1-C1：消除静态计数器，转为会话级局部变量

- **目标文件**：[`src/core/hw_transcode.cpp`](src/core/hw_transcode.cpp#L955-L1042)
- **修改方案**：
  1. 在 `runNvidiaRemasterTranscode` 函数栈帧内（约第 955 行，`bool has_prev_frame = false;` 附近）定义会话级变量：
     ```cpp
     int fruc_error_logs = 0;
     ```
  2. 彻底删除第 1042 行的 `static thread_local int s_fruc_error_logs = 0;`，将对 `s_fruc_error_logs` 的读写全部直接替换为会话局部变量 `fruc_error_logs`。

---

### 任务 9.1-C2：增加收尾阶段插帧全败熔断硬阻断检查

- **目标文件**：[`src/core/hw_transcode.cpp`](src/core/hw_transcode.cpp)
- **修改方案**：
  在 `runNvidiaRemasterTranscode` 退出主循环且完成 `finishPipeline()` 之后、`finalize()` 之前，增加强熔断校验：
  ```cpp
  // 强熔断防线：若用户请求了 RIFE 插帧但插帧 100% 失败，严禁以 0 成功退出
  if (rife_active && report.fruc_failures > 0 && report.frames_interpolated == 0) {
      return err(ErrorCode::Internal,
                 std::format("RIFE frame interpolation failed on all {} intermediate frame(s)",
                             report.fruc_failures),
                 session.bridgeLoader().lastError(session.bridge()));
  }
  ```

---

### 任务 9.1-C3：扩充 `tests/test_rife.cpp` 单测覆盖

- **目标文件**：[`tests/test_rife.cpp`](tests/test_rife.cpp)
- **施工要求**：
  1. **验证 C ABI 纯净性**：在 `rife_four_phase_monotonic_progression` 用例中，增加通过动态加载的 `rtx_ngx_bridge_interpolate_rife` C ABI 接口执行插帧，断言其在 `isBuiltinSynth()` 时绝不会触发 `NvofEngine` 覆写，且 4 相位输出严格单调演进；
  2. **新增 FP16 缓存复用测试**：验证在半精度 `DataType::kHALF` 模式下，帧对缓存 `can_reuse_pair` 更新时间步后输出数值随 $\alpha$ 递增而单调变化，绝不发生冻结；
  3. 保持全仓 75 项单测 100% PASS。

---

## 三、 本地独立验证指令与判定标准

1. **单测全绿复测**：
   ```powershell
   ctest --test-dir .vs/build/cmake/windows-msvc-ninja --output-on-failure
   ```
   **断言**：全量 75 项单测通过（100% tests passed, 0 tests failed out of 75）。

2. **全败熔断回归测试**：
   故意给一个不存在或损坏的 RIFE 引擎并强制补帧，验证进程必须返回非零退出码并打印明确的错误信息，严禁以 0 退出。

---

## 四、 交付验收标准 (DoD)

- [ ] `hw_transcode.cpp` 彻底清除 `static thread_local` 计数器，会话间状态 100% 隔离；
- [ ] 补齐全败强阻断逻辑，全败情况下绝对返回非零错误码；
- [ ] `tests/test_rife.cpp` 增加对 C ABI 零覆写与 FP16 缓存的强契约测试；
- [ ] 75/75 单测全绿通过 Gate 9.5 门禁。
