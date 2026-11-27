# **Wave 3-A：实时 60fps AI 增强播放器与 WASAPI 同步施工单**

## **1\. 元信息与架构宪法**

### **1.1 施工元数据**

* **波次标识**：Wave 3 \- Stage A  
* **依赖前置**：Wave 1-A (视口总线 `ViewportBus`)、Wave 2-A/B (AI 推理引擎与算子链) 必须已完成合入。  
* **并行任务**：与 Wave 3-B (高级 HDR 调色板与视口控制 UI) 并行推进。  
* **核心使命**：废弃"玩具级原始帧旁路演示"逻辑，将 `PlaybackSession` 全面升级为**真·实时 60fps 全链路 AI 增强播放管线**。

### **1.2 宪法级红线约束**

* **\[RED-01\] 具名类型严格配额**：全局具名类型计数严格控制在 `AuditTypes <= 34/35`。严禁为播放器管线新增任何具名 `struct` 或 `class`。所有新增内部状态必须作为已有 `PlaybackSession` 的私有成员（如 `std::atomic` 基础类型字段），方法必须以静态无状态自由函数（Static Stateless Free-functions）形态实现在匿名命名空间内。  
* **\[RED-02\] 零额外静态 DLL 依赖**：静态导入符号配额保持 `AuditImports GUI <= 4`。WASAPI 及 DirectX 组件必须基于已有动态载入机制或系统标准 Windows SDK 标头。  
* **\[RED-03\] 严格遵循 \[DEC-011\] 运行时零 TRT 编译**：播放器播放与 Seek 期间**绝对禁止触发 TensorRT 现场编译/优化**（杜绝 5–10 分钟卡死冻结）。仅允许载入事先预构建的画幅桶 `.engine` 模板；若匹配失败则安全降级至 Cuda/Shader 基础增强，绝不现场 Invocation。

---

## **2\. 问题代码坐标与架构断层分析**

当前仓库存在严重的"增强算子与播放管线脱节"断层，需在 Wave 3-A 进行精确手术：

```
[ 当前断层结构 (缺陷) ]
NVDEC / Demux ---> createHeadlessPreviewSink ---> rtx_yuv_to_rgb10_buf ---> Mailbox ---> D3D12Presenter
                    (旁路直通: 0% VSR, 0% TrueHDR, 0% 3D LUT, 0% RIFE/NVOFA)

[ GUI 视口计算断层 ]
gui/workstation_canvas.h:79-88 (active_vsr_target) ---> 悬空纯 OSD 打印, 管道零感知
```

| 问题代码坐标 | 缺陷现象描述 | 根因分析与架构断层 |
| :---- | :---- | :---- |
| `src/core/playback_session_video.cpp:151-167`  `src/core/ngx_remaster.cpp:111-203` | `createHeadlessPreviewSink` 仅将 NVDEC 解码获得的 YUV 原始帧经 `rtx_yuv_to_rgb10_buf` 打包输出。 | **旁路硬编码**：播放器管线完全跳过了 AI 增强引擎，VSR、TrueHDR、3D LUT 及 RIFE/NVOFA 帧率倍增算子全部处于 0% 接入状态。 |
| `src/gui/workstation_canvas.h:79-88`  `src/gui/rtx_gui.cpp:266-286` | 全局静态函数 `active_vsr_target()` 导出的目标分辨率仅在 OSD 渲染文本上呈现。 | **状态断层**：计算出的视口目标尺寸从未下发给 `PlaybackSession`，算子无法进行视口感知的动态代理缩放（Proxy Scaling）。 |

---

## **3\. 规约化执行步骤 (TDD Driven)**

### **Task 1：打通播放器 GPU 实时增强管线与视口代理**

```
[ 重构后统一管线结构 ]
NVDEC / SW-Upload
       │
       ▼
Unified SceneCutGuard  ──► (场景切片检测)
       │
       ▼
FRUC Engine (60fps)    ──► (RIFE / NVOFA 相位插帧)
       │
       ▼
VSR Engine             ──► (视口感知 Proxy Scaling 动态降载)
       │
       ▼
TrueHDR Engine         ──► (SDR -> HDR10 PQ)
       │
       ▼
3D LUT Kernel          ──► (色彩空间转换 / 胶片 Look)
       │
       ▼
Split-Screen Kernel    ──► (A/B 原帧与增强帧左右 2px 准星分屏)
       │
       ▼
Latest-Frame Mailbox ──► D3D12Presenter
```

#### **Step 1.1 (Red Light Test)**

编写 `tests/test_playback_pipeline.cpp`，断言播放器输出的 Surface 携带增强标志位且尺寸符合视口代理设定。p  
// 验证 Task 1 核心要求的单元测试存根  
TEST\_CASE("Playback Pipeline Pipeline Integration & Dynamic Scaling", "\[playback\]") {  
PlaybackSession session;  
session.initialize\_dummy\_decoder(1920, 1080, 24.0f);

```
// 设置视口缩放原子尺寸
session.set_target_viewport_bounds(1280, 720);
session.set_split_screen_ratio(0.5f);

session.step_frame(1);

auto frame = session.peek_latest_frame();
REQUIRE(frame != nullptr);
REQUIRE(frame->is_ai_enhanced == true);
REQUIRE(frame->width == 1280);
REQUIRE(frame->height == 720);
```

}

````

#### Step 1.2 Implementation
1. **清理旁路与重构算子链**：
   重构 `src/core/playback_session_video.cpp`，彻底废弃 `createHeadlessPreviewSink` 的直通模式，接入统一 GPU 帧总线与算子链。
2. **消除悬空全局函数**：
   删除 `workstation_canvas.h:79-88` 中的 `active_vsr_target()` 静态函数。在 `PlaybackSession` 中增加原子成员变量：
   ```cpp
   // 无需新增 struct，作为 PlaybackSession 私有原子成员
   std::atomic<uint32_t> target_viewport_w_{1920};
   std::atomic<uint32_t> target_viewport_h_{1080};
   std::atomic<float>    split_screen_ratio_{0.0f}; // 0.0=纯原始, 1.0=纯增强
````

3. **视口感知代理降载 (Viewport-Aware Proxy Scaling)**：  
   在渲染线程的帧边界 safe point 计算视口等比缩放倍率。当物理源帧（如 4K）在 `16.67ms` 实时预算内开销过高时，基于视口物理像素计算最小有效超分目标尺寸，避免算力浪费并确保稳帧 60fps。  
4. **GPU 侧 A/B 分屏 CUDA Kernel**：  
   在算子链末端引入 `k_split_screen_blend` CUDA 算子，在同一时间戳下接收"未增强原始帧（YUV转RGB10）"与"全链路 AI 增强帧"，根据 `split_screen_ratio_` 动态合成左右分屏，在交界处绘制 2px 宽（`#FFFFFF`）准星分割线。

---

## **Task 2：有理数 60fps 节拍器与 WASAPI 时钟锁相**

### **Step 2.1 (Red Light Test)**

编写 `tests/test_wasapi_clock_sync.cpp`，测试精确分数 PTS 计算与 Deadline Bypass 熔断机制。

```
TEST_CASE("WASAPI Clock & Deadline Bypass Logic", "[audio_sync]") {
    PlaybackSession session;
    session.set_source_fps(24000, 1001); // 23.976 NTSC
    session.set_output_target_fps(60000, 1001); // 59.94Hz 或 60Hz
    
    // 模拟音频时钟滞后 12ms (超过 8.0ms 死线)
    session.inject_mock_audio_clock_drift_ms(12.0f);
    session.process_next_tick();
    
    REQUIRE(session.get_ai_bypass_count() > 0);
    REQUIRE(session.is_last_frame_bypassed() == true);
}
```

### **Step 2.2 Implementation**

1. **NTSC 精确有理数时间基**：  
   全播放会话使用 `int64_t` 分数表达 PTS 与 Frame Index，彻底抹平浮点数累加误差：  
   \$\$\\text{Timestamp} \= \\text{FrameIndex} \\times \\frac{\\text{Numerator}}{\\text{Denominator}}\$\$  
   完美支持 `24000/1001` (23.976)、`30000/1001` (29.97)、`60000/1001` (59.94)、`24/1`、`25/1`、`30/1`、`60/1` 等任意画幅节拍。  
2. **分数帧率时间槽映射 (Fractional Phase Mapping)**：  
   设输出第 \$k\$ 帧（目标 \$F\_{out} \= 60\\text{ Hz}\$）对应源时间位置：  
   \$\$\\text{pos} \= k \\times \\frac{F\_{in}}{F\_{out}}\$\$  
   * 若 \$\|\\text{pos} \- \\text{round}(\\text{pos})\| \< 10^{-3}\$：零插帧开销，直接送出源帧 \$I\_{\\text{round}(\\text{pos})}\$。  
   * 否则：选取相邻源帧对 \$(I\_{\\lfloor \\text{pos} \\rfloor}, I\_{\\lfloor \\text{pos} \\rfloor \+ 1})\$ 与分数相位 \$\\alpha \= \\text{frac}(\\text{pos}) \\in (0, 1)\$，推入 RIFE/NVOFA 引擎合成中间帧。针对 23.976fps \-\> 60fps，系统自动生成相位循环序列 `[0.0, 0.4, 0.8, 0.2, 0.6]`。  
3. **WASAPI Master Clock 硬件锁相与 Deadline Bypass 熔断**：  
   * 以 `WasapiAudioOutput` 真实渲染采样点算出的 PTS 作为 Master Clock，支持 `playback_speed`（`0.25x ~ 2.0x`）调制。  
   * 建立容量为 4\~6 帧的 `Bounded Paced Ring Buffer` 异步解算。  
   * **死线熔断机制**：渲染线程在送显前检查 `PTS_target - PTS_audio`。若延迟 \$\> 8.0\\text{ ms}\$，触发**单帧 AI 旁路熔断**——跳过本帧 RIFE 插帧与 VSR 推理，直接输出解包原帧，原子递增 `ai_bypass_count_`，绝对禁止视频 AI 计算反压导致 WASAPI 缓冲区空转爆音。

---

## **Task 3： Seek / Pause / StepFrame 时域缓存无残影重置**

### **Step 3.1 (Red Light Test)**

编写 `tests/test_seek_temporal_reset.cpp`，断言 Seek 后的首帧清空时域历史缓存。p  
TEST\_CASE("Seek Operation Temporal Cache Reset", "\[seek\]") {  
PlaybackSession session;  
session.load\_media("test\_clip.mp4");  
session.play();

```
// 运行 10 帧积累 RIFE/VSR 时域缓存
for(int i = 0; i < 10; ++i) session.process_next_tick();

// 执行 随机 Seek
session.seek_pts(5000000); // Seek to 5s

REQUIRE(session.get_temporal_cache_state() == TemporalState::ResetFlushed);
REQUIRE(session.get_pipeline_latency_ms() < 5.0f);
```

}

```

##### Step 3.2 Implementation
1. **Seek / Step 响应控制链**：
   当触发 `seek(target_pts)` 或 `stepFrame(-1/+1)` 时，立即执行以下原子序列：
   * 挂起有界预读环，原子清空 Ring Buffer 内残留帧。
   * 重置 `ShotGroupSegmenter` 场景切片状态机。
   * 向 RIFE/NVOFA/VSR 算子上下文下发 `force_reset_temporal = true` 信号，立即释放或抹平历史 GPU Textures 缓存。
2. **静态单帧预览响应**：
   Seek 停止点或 Pause 步进点处的首帧，跳过时域插帧（RIFE），但实施**完整静态单帧增强**（VSR 空间超分 + TrueHDR + 3D LUT），并立即刷入视口 Mailbox，确保拖拽进度条时高响应、无拉丝、无跨镜头残影闪烁。

---

### 4. 验收门禁与测试矩阵

在提交代码前，必须通过以下全部硬性指标：
```

```
                          [ 验收测试流 ]
                                 │
       ┌─────────────────────────┼─────────────────────────┐
       ▼                         ▼                         ▼
```

60s A/V Sync Test         50x Stress Seek Test       Headless Audio Test  
(A/V Drift \<= ±16.67ms)    (Zero Deadlock/Artifact)   (Zero Beeper/Zero Noise)

```

| 门禁分类 | 测试用例 / 场景 | 成功判定标准 | 测量工具 / 方法 |
| :--- | :--- | :--- | :--- |
| **A/V 锁相精度** | 23.976fps H.264/HEVC 开启 RIFE 60fps + VSR + TrueHDR 持续播放 60s | `A/V Drift` 稳定保持在 `±16.67 ms`（1 帧）以内；无卡死，`ai_bypass_count` 无异常暴涨。 | `WasapiAudioOutput` 硬件 PTS 与渲染视口 Surface 提示符时间差日志校验。 |
| **交互稳定性** | 连续执行 50 次随机 `Seek` + `StepFrame(+1/-1)` + 变速 `Speed(0.25x <-> 2.0x)` | 零死锁、零残影闪帧、音频硬件无爆音/断音，Memory Leak 为 0 B。 | 单元自动化测试集 `test_seek_temporal_reset` 压力循环。 |
| **无声环境与架构** | CI/CD 单元测试模式运行 | 自动挂载 Virtual Silent Sink，无蜂鸣音、无硬件设备依赖冲突；全工程无新增具名类型。 | `AuditTypes` 符号检查脚本 (严格 `<= 34/35`)。 |

---

### 5. 代码变动与文件结构清单
```

src/  
├── core/  
│   ├── playback\_session.h        \# \[修改\] 增加原子视口尺寸/分屏比例/时域重置状态成员  
│   ├── playback\_session\_video.cpp\# \[重构\] 废弃直通旁路，接入 FRUC \-\> VSR \-\> HDR \-\> LUT 算子链  
│   └── wasapi\_audio\_output.cpp   \# \[修改\] 导出 Precise Master PTS 与 Hardware Drift 遥测  
├── gui/  
│   ├── workstation\_canvas.h      \# \[修改\] 移除 active\_vsr\_target 悬空函数，改为视口槽绑定  
│   └── rtx\_gui.cpp               \# \[修改\] UI 滑块事件原子写入 PlaybackSession 视口与分屏槽  
└── tests/  
├── test\_playback\_pipeline.cpp\# \[新增\] 增强管线与动态代理缩放单元测试  
├── test\_wasapi\_clock\_sync.cpp\# \[新增\] 60fps 分数帧率与 WASAPI 锁相测试  
└── test\_seek\_temporal\_reset.cpp\# \[新增\] Seek/Step 无残影重置测试