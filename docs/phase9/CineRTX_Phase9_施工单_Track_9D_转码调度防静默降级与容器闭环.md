# CineRTX Phase 9：SuperPower 施工单 (Track 9D)
## 转码调度流水线防静默降级、容器帧率闭环与实机全量回归

- **任务编号**：`SP-P9-TRACK-9D`
- **执行责任**：核心调度引擎与多媒体容器封装工程师
- **涉及模块与文件**：
  - `src/core/hw_transcode.cpp`
  - `src/io/ffmpeg_io.cpp`
  - `src/cli/main.cpp`
  - `tests/test_rife.cpp`
- **依赖前置**：Track 9A、Track 9B、Track 9C（汇总合拢与端到端回归）

---

## 一、缺陷机理与施工目标

### 1.1 缺陷定位
1. **静默吞错掩盖系统崩溃**：`hw_transcode.cpp:1027-1030` 在 `interpolateRife` 返回非 `RTX_OK` 时，未打印任何告警，直接无条件执行 `processFrame(prev_frame)`。所有 4 个插值时间点被静默填充为上一帧，向用户交付充满 1:5 重复帧的伪高刷视频。
2. **场景转场帧僵直**：当检测到场景硬切（`scene_cut != 0`）时，对整组相位全量复制旧镜头 $F_k$，导致镜头切换处出现 5 帧僵直停顿。
3. **输出容器头元数据丢失**：`hw_transcode.cpp:446` 计算了目标帧率 `frame_rate_ = 120/1` 并配置给了 NVENC，但漏更新了 `output_info_.frame_rate`；`ffmpeg_io.cpp:882` 仅写入了 `stream_->avg_frame_rate`，漏写了容器关键参数 `stream_->r_frame_rate`。

### 1.2 施工目标
- 彻底拔除静默吞错逻辑：插帧失败时输出明确诊断日志 `[FRUC-ERROR]` 并记录失败计数；
- 场景硬切与异常降级采用**最近邻相位阶跃**（$\alpha < 0.5 \to F_k, \alpha \ge 0.5 \to F_{k+1}$）；
- 容器流信息完整同步 120fps 元数据；
- 编写合成移动条 $\alpha$ 演进单测，并在实机上执行 24 $\to$ 120fps 全量转码，达成终审闭环。

---

## 二、逐行施工与代码变更清单

### 2.1 改造 `src/core/hw_transcode.cpp`
1. **同步容器输出帧率元数据**（约第 446 行）：
   ```cpp
   frame_rate_ = chooseFrameRate(options, info_);
   output_info_.frame_rate = frame_rate_; // 补齐漏掉的输出信息赋值！
   ```

2. **重构插帧异常与场景硬切分支**（约第 1012–1031 行）：
   ```cpp
   if (status == RTX_OK) {
       if (scene_cut != 0) {
           ++report.fruc_scene_cuts;
           // 最近邻阶跃：前半段输出前一帧，后半段切换到新镜头，消除 5 帧停顿
           const RtxFrameDesc& fallback_target = (alpha < 0.5f) ? prev_frame : frame;
           auto res = processFrame(fallback_target, out_pts_90k, /*release_slot=*/false);
           if (!res.has_value()) return res;
       } else {
           ++report.frames_interpolated;
           RtxFrameDesc interp_frame = prev_frame;
           interp_frame.planes[0] = fruc_out_buffer;
           interp_frame.planes[1] = fruc_out_buffer + static_cast<uint64_t>(pitch) * prev_frame.height;
           interp_frame.pts = out_pts_90k;
           interp_frame.slot_index = 0xFFFFFFFF;
           auto res = processFrame(interp_frame, out_pts_90k, /*release_slot=*/false);
           if (!res.has_value()) return res;
       }
   } else {
       // 严禁静默吞错！限频打印致命诊断日志
       static int s_logged_fruc_errs = 0;
       if (s_logged_fruc_errs++ < 5) {
           logError(std::format("[FRUC-ERROR] RIFE interpolation failed (code={}): {}",
                                status, session.bridgeLoader().lastError(session.bridge())));
       }
       // 降级回退同样采用最近邻过渡
       const RtxFrameDesc& fallback_target = (alpha < 0.5f) ? prev_frame : frame;
       auto res = processFrame(fallback_target, out_pts_90k, /*release_slot=*/false);
       if (!res.has_value()) return res;
   }
   ```

### 2.2 改造 `src/io/ffmpeg_io.cpp`
在 `FfmpegMuxer::open` 中（约第 882 行）：
```cpp
const AVRational frame_rate{info.frame_rate.num > 0 ? static_cast<int>(info.frame_rate.num) : 25,
                            info.frame_rate.den > 0 ? static_cast<int>(info.frame_rate.den) : 1};
stream_->avg_frame_rate = frame_rate;
stream_->r_frame_rate   = frame_rate; // 补齐 real frame rate 设置！
```

### 2.3 补充单元测试 `tests/test_rife.cpp`
编写针对 $\alpha \in \{0.2, 0.4, 0.6, 0.8\}$ 的单调演进断言测试：
```cpp
RTX_TEST(rife_four_phase_monotonic_progression) {
    // 构造测试帧对：F0 高亮条在 x=100，F1 高亮条在 x=120
    // 依次请求 alpha = 0.0, 0.2, 0.4, 0.6, 0.8, 1.0
    // 断言中间 4 个插值帧互不相同，质心位置单调递增，彻底杜绝 1:5 重复帧！
}
```

---

## 三、验收标准与终审门禁 (DoD)

1. **容器头合规**：使用 `ffprobe` 检验产物，`r_frame_rate` 与 `avg_frame_rate` 均为 `120/1`；
2. **插帧有效**：`report.frames_interpolated` 精确非零；
3. **像素差分阶跃消失**：逐帧 MAE 分析中，`Frame 15 -> 16 -> 17 -> 18` 差分连续且与大运动匹配，不存在连续 4 帧差分近似为 0 的重复帧。
