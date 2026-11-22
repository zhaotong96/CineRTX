# CineRTX Phase 11 施工单 Track 11D：DirectComposition 零回读视口、统一 NLE 引擎与 AVX2 音频矩阵

- **施工单编号**：`CINERTX-P11-TASK-11D`
- **优先级与依赖**：**P1/P2（依赖 Track 11A + 11B + 11C 均合龙后启动）**
- **承接工程师**：图形合成（DComp/D3D12）与 NLE 引擎工程师（Graphics & NLE Engine Engineer）
- **母本依据**：[`04_CineRTX Phase 11 冲突评判与方案.md`](./04_CineRTX Phase 11 冲突评判与方案.md) §3（第一点）、§4（Track 11D 垂直切片 A/B/C）、§5（Gate 11D-1/2）
- **独占文件边界**：
  - 修改：`src/shim/ngx_bridge/d3d12_interop.h`、`src/shim/ngx_bridge/d3d12_interop.cpp`、`src/gui/preview_viewport.h`、`src/gui/preview_viewport.cpp`、`src/gui/rtx_gui.cpp`、`src/core/job_queue.cpp`、`cmake/AuditImports.cmake`
  - 新建：`include/rtx/nle_project.h`、`src/core/nle_project.cpp`、`include/rtx/render_graph.h`、`src/core/render_graph.cpp`、`include/rtx/audio_mixer_avx2.h`、`src/core/audio_mixer_avx2.cpp`、`src/core/wsola_stretch.cpp`、`tests/test_nle_vertical_slice.cpp`、`tests/test_audio_mixer_avx2.cpp`
  - **严禁触碰**：`src/shim/ngx_bridge/rife_*`、`src/shim/loader/nv_encoder.cpp`（由 Track 11E 独占）

---

## 一、 施工目标与三阶段垂直切片路线

本轨按严格顺序交付 3 个可独立验收的垂直切片，严禁跳步堆砌：
1. **垂直切片 A**：DirectComposition 零主机回读（0 B/s D2H）共享设备合成视口 + HDR/SDR 自适应 + `EncoderJobQueue` 预览接线；
2. **垂直切片 B**：独立于 Win32 GUI 的版本化 `NleProject` 领域模型 + Preview/Export 统一 `RenderGraph` + 最小可验收 `1V+1A` 剪辑与多段硬切导出切片；
3. **垂直切片 C**：32-bit Float Planar + `alignas(64)` 无锁 SPSC 环 + AVX2 SIMD 8 路混音与纯 C++ WSOLA 保调变速。

---

## 二、 原子 TDD 施工任务（Atomic Tasks）

### 垂直切片 A：DirectComposition 零回读合成视口升级

#### Task 11D-1：暴露共享 D3D12 借用设备宿主接口（`src/shim/ngx_bridge/d3d12_interop.h/.cpp`）
- 在 `D3D12Interop`（注意 `src/shim/` 须严格保持 `MIT OR Apache-2.0` SPDX 头！）上暴露只读/借用宿主接口（`borrowedD3d12Device()` 与适配器 LUID），使 GUI 复用已按 CUDA LUID 匹配的无头 `ID3D12Device`，淘汰此前 `PreviewViewport` 私自创建的第二个 `ID3D12Device`。
- 新增独立提交给视口的 **`D3D12_COMMAND_QUEUE_PRIORITY_NORMAL`** 图形命令队列（**严禁书写不存在的 `D3D12_COMMAND_QUEUE_PRIORITY_LOW` 枚举**，亦严禁滥用 `GLOBAL_REALTIME`），通过命令列表上限、Fence 节流与提交深度控制视口开销。

#### Task 11D-2：动态加载 `dcomp.dll` 与 `CreateSwapChainForComposition` 重构（`src/gui/preview_viewport.h/.cpp`）
1. 使用 Track 11A 的 `UniqueModule` 通过 `LoadLibraryExW(L"dcomp.dll", nullptr, LOAD_LIBRARY_SEARCH_SYSTEM32)` 动态解析 `DCompositionCreateDevice`，同步更新 `cmake/AuditImports.cmake` 与 `preview_viewport.h` 文档白名单。
2. 严格遵守 [`IDXGIFactory2::CreateSwapChainForComposition`](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_2/nf-dxgi1_2-idxgifactory2-createswapchainforcomposition) 官方契约：
   - `SwapEffect = DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL`（不可用 `FLIP_DISCARD`）；
   - `Width` / `Height` 必须为非零正整数；
   - `Scaling = DXGI_SCALING_ASPECT_RATIO_STRETCH`；
   - `Flags = DXGI_SWAP_CHAIN_FLAG_FRAME_LATENCY_WAITABLE_OBJECT`。
3. 零主机回读路径：直接消费 CUDA-D3D12 共享纹理（允许 GPU 内部 Copy/ToneMap Pass，但 **0 B/s D2H 像素回读**）；通过 `IDXGISwapChain4::CheckColorSpaceSupport` 动态适配显示器：
   - Windows 11 HDR 显示器：`DXGI_FORMAT_R16G16B16A16_FLOAT` + `DXGI_COLOR_SPACE_RGB_FULL_G10_NONE_P709`（scRGB 线性空间）直出 TrueHDR；
   - SDR 显示器：`DXGI_FORMAT_R8G8B8A8_UNORM` + 独立轻量 Compute Shader 执行 ACES Filmic 色调映射，**绝不污染送往 NVENC 的原始 4K 显存表面**。
4. 在 `src/core/job_queue.cpp` 与 `src/gui/rtx_gui.cpp` 中补齐队列模式下的零拷贝预览纹理句柄与 `split_ratio` 接线（顺手修复 `rtx_gui.cpp` 历史失败任务弹窗去重 P2 卫生项）。

---

### 垂直切片 B：独立 NLE 领域模型与 Preview/Export 统一 `RenderGraph`

#### Task 11D-3：版本化 `NleProject` 数据契约与 Command Pattern 撤销栈（`include/rtx/nle_project.h` & `src/core/nle_project.cpp`）
```cpp
// include/rtx/nle_project.h (SPDX: GPL-3.0-or-later WITH Plugin-Exception)
#pragma once
#include <cstdint>
#include <memory>
#include <string>
#include <vector>
#include "rtx/time_model.h"

namespace rtx::nle {

struct MediaAsset {
    std::string asset_id;         // 稳定 UUID，不依赖数组下标
    std::string relative_path;    // 相对工程文件目录
    std::string last_known_path;  // 断链时支持 Relink
    int64_t duration_flicks = 0;
    Rational frame_rate{24, 1};
    bool offline = false;
};

struct Clip {
    std::string clip_id;
    std::string asset_id;
    int64_t source_in_flicks = 0;
    int64_t source_out_flicks = 0;
    int64_t timeline_start_flicks = 0;
    Rational playback_speed{1, 1};
    float gain_db = 0.0f;
    float pan = 0.0f;             // -1.0 .. +1.0
    std::string lut_path;
};

struct Track {
    enum class Kind : uint8_t { Video, Audio, Subtitle };
    std::string track_id;
    Kind kind = Kind::Video;
    bool muted = false;
    std::vector<Clip> clips;
};

struct Sequence {
    std::string name{"Main Sequence"};
    Rational frame_rate{60, 1};
    TimecodeFormat timecode_fmt{};
    std::vector<Track> tracks;
};

struct NleProject {
    uint32_t schema_version = 1;
    std::vector<MediaAsset> assets;
    Sequence sequence;
};

// 可逆编辑命令（严禁持有任何 GPU 资源句柄！）
class IEditCommand {
public:
    virtual ~IEditCommand() = default;
    virtual Result<void> apply(NleProject& proj) = 0;
    virtual Result<void> undo(NleProject& proj) = 0;
};
// 实现：InsertClipCmd, TrimClipCmd, SplitClipCmd, MoveClipCmd, DeleteClipCmd, RelinkAssetCmd
} // namespace rtx::nle
```
- 实现 `NleProject` 的确定性 JSON 序列化/反序列化（含 `schema_version` 校验与素材断链 Relink）。

#### Task 11D-4：Preview / Export 统一 `RenderGraph`（`include/rtx/render_graph.h` & `src/core/render_graph.cpp`）
- 将 `Sequence` 编译为不可变的时间段算子图 `CompiledRenderGraph`：
  - **严格语义一致性**：Preview 与 Export 共享同一份 `CompiledRenderGraph` 的片段时间映射、边界切点与音画同步关系；质量档位仅控制节点分辨率或是否跳过昂贵算子（VSR/FRUC）；
  - **片段硬切边界保护**：跨 `Clip` 硬切边界自动标记 `reset_temporal = true` 与 `force_keyframe = true`，防止 VSR/TrueHDR 跨镜头曝光泄漏；
  - 首期交付 **1V + 1A 轨道 + 导入/移动/Trim/Split/Delete + Undo/Redo + 多段硬切导出** 最小垂直切片，验收通过后再叠加多轨叠化（Cross-Dissolve）。

---

### 垂直切片 C：AVX2 SIMD 多轨音频矩阵与 WSOLA 保调变速

#### Task 11D-5：无锁 SPSC 环 + AVX2 混音与 WSOLA（`include/rtx/audio_mixer_avx2.h`, `src/core/audio_mixer_avx2.cpp`, `src/core/wsola_stretch.cpp`）
1. 统一解码归一化：通过静态 `libswresample` 将所有 NLE 音轨输出为 **32-bit Float Planar PCM（48 kHz / 96 kHz）**；
2. 无锁 SPSC 环形缓冲：读写游标各自以 `alignas(64) std::atomic<size_t>` 物理隔离在不同缓存行，杜绝 MESI 伪共享；
3. AVX2 SIMD 8 路并行混音：使用 `_mm256_fmadd_ps` 并行计算多轨增益、等功率声相、淡入淡出与总线防削波软限幅器（Soft-Clipper / Limiter），附标量回退路径；
4. 纯 C++ WSOLA 重叠相加（Waveform Similarity Overlap-Add）保调变速：支持 `0.25x–2.0x` 片段变速而保持原声调不变。
5. 编写 `tests/test_nle_vertical_slice.cpp` 与 `tests/test_audio_mixer_avx2.cpp`。

---

## 三、 量化验收门禁 Checklist（Gate 11D-1 & Gate 11D-2）

- [ ] **静态扫描（API 与红线合规）**：
  - `rg "D3D12_COMMAND_QUEUE_PRIORITY_LOW" include/ src/` 返回 0 匹配；
  - `rg "DXGI_SWAP_EFFECT_FLIP_SEQUENTIAL" src/gui/preview_viewport.cpp` 命中；
  - `rg "LoadLibraryExW.*dcomp\.dll" src/gui/` 命中（零静态链接 `dcomp.lib`）；
  - 静态审查 `IEditCommand` 子类确认零 GPU/COM/CUDA 句柄成员。
- [ ] **Gate 11D-1（DirectComposition 零回读视口门禁）**：
  - 实证播放与队列监看路径 `D2H 像素拷贝字节数 == 0 B`；
  - HDR/SDR 跨屏切换与连续 `resizeSwapChain` 100 次无空域撕裂、黑边闪烁或显存泄漏。
- [ ] **Gate 11D-2（NLE 垂直切片与 AVX2 音频门禁）**：
  - `test_nle_vertical_slice` 通过：1V+1A 工程保存/重开/Relink、Trim/Split/Move/Delete、Undo/Redo 全对称，多段硬切导出帧数与时间线逐帧映射一致；
  - `test_audio_mixer_avx2` 通过：AVX2 与标量参考误差 `< 1e-5`、多轨叠加峰值不超过 `[-1.0, 1.0]`、WSOLA `0.5x` 变速时长翻倍且基频漂移 `< 2%`；
  - `AuditSpdx.cmake`、`AuditImports.cmake` 与全量 `rtx_tests.exe` 100% PASS。
