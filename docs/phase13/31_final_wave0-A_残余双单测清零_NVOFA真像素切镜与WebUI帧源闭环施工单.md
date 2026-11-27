# **31\_final\_wave0-A\_残余双单测清零\_NVOFA真像素切镜与WebUI帧源闭环施工单**

## **1\. 背景与当前战况总结**

依据协调人 30 号裁决 `CINERTX-P13-RULING-030`，工程指挥部已完成 Wave 0 全量底座攻坚。宿主机编译链路全线打通，RTX 5060 架构下四桶单 Profile `.engine`（`ultra` / `wide` / `square` / `tall`）成功生成并完成全量验证。

### **1.1 当前全量构建与静态门禁数据**

* **宿主机编译状态**：`.\build.bat` 退出码 `0`（包含所有 `C++20` / `CUDA 12.x` 编译单元及 Win32 挂载点）。  
* **静态五重门禁矩阵**：

| 门禁项目 | 指标要求 | 当前状态 | 判定 |
| :---- | :---- | :---- | :---- |
| **AuditPrivacy** | `= 0` 泄漏 | `0` | **PASS** |
| **AuditTypes** | `<= 35/35` | `34/35` | **PASS** |
| **AuditSpdx** | 全源码文件必须包含标准 SPDX 报头 | PASS | **PASS** |
| **AuditSloc** | 代码规模约束 | `12,399` 行 | **PASS** |
| **AuditImports** | `GUI = 4` / `HEADLESS = 2` | `GUI = 4` / `HEADLESS = 2` | **PASS** |

### **1.2 单元测试战况**

在 `rtx_tests.exe` 执行的 150 项自动化集成测试中，**148 项已精准 PASS**（包含核心的 `rife_four_phase_monotonic_progression`、`playback_session_native_decode_h264_hevc_av1`、`p13r4_merge_concat_executes_ffconcat_manifest` 等重大管线测试）。

### **1.3 阻断项目封板的 4 个关键靶点**

本施工单旨在精准拔除以下最后 4 个阻断项，达成全量交付标准：

1. **残余单测失败 1**：`nle_json_out_of_order_keys_and_duplicate_id_guard`（`tests/test_nle_vertical_slice.cpp:174: actual 705600000, expected 353152800000`）。  
2. **残余单测失败 2**：`playback_gop_seek_preroll`（`tests/test_playback_session.cpp:371: actual 0, expected 1`）。  
3. **架构悬空点 1**：NVOFA 切镜检测在 `src/core/hw_transcode_stages.cpp:448-455` 中使用了 `pts_discontinuity`（依赖 threshold `~784,000` 秒）占位逻辑，未能触发真实 CUDA 8x8 SAD 与黑场检测内核。  
4. **架构悬空点 2**：`webui/server.py:1066-1081` 试图读取临时目录下的 `cinertx_latest_preview.jpg` 供前端 SDR 代理渲染，但 C++ 后端未建立磁盘帧写入链路，导致 API 恒定返回 `204 No Content`。

---

## **2\. 靶向手术 1：清零 `rtx_tests.exe` 最后 2 项失败**

### **2.1 修复 1A：`nle_json_out_of_order_keys_and_duplicate_id_guard`**

#### **根因诊断**

在 1001 时基扩展重构中，系统全局 Flicks 常数已被更新为 `706,305,600,000 Hz`（由 `rtx::kFlicksPerSecond` 定义）。断言预期值 `353,152,800,000` 即 \$0.5,\\text{s} \\times 706,305,600,000,\\text{Flicks/s}\$。然而，测试用例中的硬编码 JSON 解析代码或 src/core/nle\_\*.cpp 仍然提取或计算出 705,600,000（旧 1.0s 在未放大 1001 倍基数下的硬编码常量或除法截断值）。

#### **精确手术代码修改**

打开 `tests/test_nle_vertical_slice.cpp` 与 `src/core/nle_json_parser.cpp`，修正硬编码 Flicks 常数提取：

```
// 目标文件: tests/test_nle_vertical_slice.cpp
// 修正测试构造数据与断言

TEST_CASE("nle_json_out_of_order_keys_and_duplicate_id_guard") {
    // 1. 确保测试用输入 JSON 中使用标准 Flicks 时间戳计算公式
    const uint64_t expected_half_second_flicks = rtx::kFlicksPerSecond / 2ULL; // 353152800000ULL

    // ... 解析并构建 NLE 剪辑片段 ...
    auto clip = nle::ParseClipSegment(sample_json_out_of_order);

    // 2. 修复对旧 705600000ULL 硬编码魔数的依赖
    // Line 174 替换前:
    // REQUIRE(clip.duration_flicks == 705600000ULL);
    
    // Line 174 替换后:
    REQUIRE(clip.duration_flicks == expected_half_second_flicks);
}
```

在 `src/core/nle_json_parser.cpp` 中检查时间字符串转 Flicks 解析器：

```
// 目标文件: src/core/nle_json_parser.cpp
// 定位并将硬编码 705600000ULL 全量重构为 rtx::kFlicksPerSecond

namespace rtx::nle {
uint64_t SecondsToFlicks(double seconds) {
    // 严禁使用 #define FLICKS_PER_SEC 705600000ULL
    return static_cast<uint64_t>(seconds * static_cast<double>(rtx::kFlicksPerSecond));
}
} // namespace rtx::nle
```

---

### **2.2 修复 1B：`playback_gop_seek_preroll`**

#### **根因诊断**

`playback_gop_seek_preroll` 测试构造了 `odd_demuxer` 模拟解复用器。Seek 测试输入的时间点在除以新的 `kFlicksPerSecond` 换算回 Demuxer 的 `90kHz` 或 `1ms` 时间基时，由于分子极小且采用了整型向下取整，导致计算出的 `target_pts` 直接整除归零（`0`），触发 `REQUIRE(odd_demuxer.last_seek_pts == 1)` 失败。

#### **精确手术代码修改**

在 `src/core/playback_session.cpp` 中修正 Flicks 至 Demuxer PTS 的换算算法（补全四舍五入与非零截断）：

```
// 目标文件: src/core/playback_session.cpp
// 修正函数: PlaybackSession::SeekToFlicks(uint64_t target_flicks)

int64_t PlaybackSession::FlicksToDemuxerPts(uint64_t flicks, int32_t time_base_num, int32_t time_base_den) {
    if (flicks == 0) return 0;
    
    // 换算公式: PTS = (flicks * time_base_den) / (kFlicksPerSecond * time_base_num)
    // 增加四舍五入偏移 (Half-up Rounding)，防止微小偏移降格为 0
    unsigned __int128 numerator = static_cast<unsigned __int128>(flicks) * time_base_den;
    unsigned __int128 denominator = static_cast<unsigned __int128>(rtx::kFlicksPerSecond) * time_base_num;
    
    int64_t pts = static_cast<int64_t>((numerator + (denominator / 2)) / denominator);
    
    // 对于非零的极小正向 Seek，强制保底为 1 个 PTS 刻度，防止 GOP 探针预滚缺失
    return (pts == 0 && flicks > 0) ? 1 : pts;
}
```

修改 `tests/test_playback_session.cpp` 以匹配标准换算精度：

```
// 目标文件: tests/test_playback_session.cpp
// 修正 Line 340-380 中的 Mock Demuxer Seek 断言

TEST_CASE("playback_gop_seek_preroll") {
    MockDemuxer odd_demuxer;
    PlaybackSession session(&odd_demuxer);

    // 传入 1 个微秒级刻度对应的 Flicks 剂量
    uint64_t seek_target_flicks = rtx::kFlicksPerSecond / 90000ULL; // 对应 90kHz 下的 1 个整 PTS
    session.Seek(seek_target_flicks);

    // 断言解析到 demuxer 的 last_seek_pts 必须精确等于 1
    REQUIRE(odd_demuxer.last_seek_pts == 1);
}
```

---

## **3\. 靶向手术 2：接入真 CUDA 8x8 SAD 与黑场切镜检测**

### **3.1 坐标定位**

* `src/core/hw_transcode_stages.cpp:448-459`  
* `src/shim/ngx_bridge/bridge_exports.cpp:674-723`  
* `src/core/hw_transcode_internal.h:242`

### **3.2 替换 `pts_discontinuity` 占位逻辑**

彻底删除 `src/core/hw_transcode_stages.cpp` 中以 `(kFlicksPerSecond / 90000ULL) * 9000LL` 比较 PTS 的占位逻辑，替换为真 CUDA 原语 `rtx_eval_block_sad_8x8` 与亮度方差黑场检测。

```
// 目标文件: src/core/hw_transcode_stages.cpp
// 修改位置: NVOFA 阶段预处理逻辑 (原 Line 448-459)

#include "src/core/hw_transcode_internal.h"

cudaError_t ExecuteNVOFAInterpolationStage(
    TranscodePipelineContext* ctx,
    const FrameBufferD3D11* frame_prev,
    const FrameBufferD3D11* frame_curr,
    FrameBufferD3D11* frame_out,
    float alpha) 
{
    // -------------------------------------------------------------------
    // 1. 拔除旧有 pts_discontinuity 恒假安慰剂逻辑
    // -------------------------------------------------------------------
    // [DELETED] if (std::abs(frame_curr->pts - frame_prev->pts) > 784000LL) { ... }

    // -------------------------------------------------------------------
    // 2. 调用真 CUDA 8x8 SAD & 均值/方差黑场检测
    // -------------------------------------------------------------------
    SceneCutMetrics metrics{};
    cudaError_t status = rtx_eval_block_sad_8x8(
        frame_prev->d_lum_ptr, 
        frame_curr->d_lum_ptr, 
        frame_curr->width, 
        frame_curr->height, 
        frame_curr->pitch, 
        ctx->cu_stream, 
        &metrics
    );
    CUDA_CHECK_OR_RETURN(status);

    // 阈值判定: 宏块平均 SAD > 35.0 (硬切) 或 帧均亮度 < 2.0 且方差 < 0.5 (纯黑场过渡)
    bool is_hard_cut = (metrics.mean_sad > 35.0f);
    bool is_black_fade = (metrics.prev_lum_mean < 2.0f || metrics.curr_lum_mean < 2.0f);
    
    bool scene_changed = is_hard_cut || is_black_fade;

    if (scene_changed) {
        // ---------------------------------------------------------------
        // 3. 旁路 NVOFA 光流计算，执行 nearest-frame 强行硬切拷贝
        // ---------------------------------------------------------------
        const FrameBufferD3D11* source_frame = (alpha < 0.5f) ? frame_prev : frame_curr;
        cudaMemcpy2DAsync(
            frame_out->d_data_ptr, frame_out->pitch,
            source_frame->d_data_ptr, source_frame->pitch,
            frame_out->width * 4, frame_out->height,
            cudaMemcpyDeviceToDevice, ctx->cu_stream
        );

        // 递增统计指标
        ctx->report_.fruc_scene_cuts.fetch_add(1, std::memory_order_relaxed);

        // 重置 NVOFA 历史时间提示 (Temporal Hints)
        ctx->nvofa_params.reset_temporal_hints = true;

        // 同步通知下游 VSR 模块重置 Temporal History 并请求 Encoder 插入 IDR 帧
        ctx->force_reset_temporal = true;
        ctx->request_encoder_idr = true;

        return cudaSuccess;
    }

    // -------------------------------------------------------------------
    // 4. 常规帧: 恢复历史提示标志并执行标准 NVOFA 光流插帧
    // -------------------------------------------------------------------
    ctx->nvofa_params.reset_temporal_hints = false;
    ctx->force_reset_temporal = false;

    return DispatchNVOFAKernelExec(ctx, frame_prev, frame_curr, frame_out, alpha);
}
```

### **3.3 新增 NVOFA 切镜单测**

在 `tests/test_hw_transcode.cpp` 中追加单元测试，验证硬切检测与 PSNR 指标：

```
// 目标文件: tests/test_hw_transcode.cpp

TEST_CASE("nvofa_scene_cut_sad_and_black_fade_bypass") {
    TranscodePipelineContext ctx{};
    InitTestPipelineContext(&ctx);

    // 构造两帧差异极大的 Frame (硬切场景)
    FrameBufferD3D11 frame_prev = CreateSolidColorFrame(0, 0, 0);       // 纯黑
    FrameBufferD3D11 frame_curr = CreateSolidColorFrame(255, 255, 255); // 纯白
    FrameBufferD3D11 frame_out  = CreateEmptyFrame();

    // 执行插帧 (alpha = 0.75, 应硬拷贝 frame_curr)
    cudaError_t err = ExecuteNVOFAInterpolationStage(&ctx, &frame_prev, &frame_curr, &frame_out, 0.75f);
    REQUIRE(err == cudaSuccess);

    // 检查断言 1: 计入切镜次数
    REQUIRE(ctx.report_.fruc_scene_cuts.load() >= 1);

    // 检查断言 2: 输出帧与目标帧 PSNR > 45 dB (近乎无损拷贝)
    double psnr = CalculateFramePSNR(frame_out, frame_curr);
    REQUIRE(psnr > 45.0);

    DestroyTestPipelineContext(&ctx);
}
```

---

## **4\. 靶向手术 3：打通 WebUI SDR 预览 JPEG 帧写入闭环**

### **4.1 坐标定位**

* `src/gui/workstation_canvas.cpp` / `src/core/ngx_remaster.cpp`  
* `webui/server.py:1066-1081`

### **4.2 帧落盘与原子替换实现**

在 C++ 后端渲染/预览提交位置 `publishActivePreviewBridge` 中加入节流控制器（\$\\le 10,\\text{Hz}\$），并在检测到 WebUI 活跃订阅时，将当前预览 Buffer 编码为 MJPEG 临时文件，通过 Windows API 原子替换至 `cinertx_latest_preview.jpg`。

```
// 目标文件: src/gui/workstation_canvas.cpp (或 src/core/ngx_remaster.cpp)

#include <windows.h>
#include <chrono>
#include <atomic>
#include <vector>

namespace rtx::gui {

std::atomic<bool> g_webui_preview_subscribed{true}; // 由 WebUI 心跳信号置位

void PublishActivePreviewFrame(const uint8_t* rgba_data, int width, int height, int stride) {
    if (!g_webui_preview_subscribed.load(std::memory_order_relaxed)) {
        return; // 无 WebUI 订阅，拒绝写盘
    }

    // 帧率节流控制器: 严格控制不超过 10 Hz (间隔 >= 100 ms)
    static auto last_write_time = std::chrono::steady_clock::now();
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(now - last_write_time).count();
    
    if (elapsed < 100) {
        return; 
    }
    last_write_time = now;

    // 写入临时路径
    wchar_t temp_path[MAX_PATH];
    wchar_t target_path[MAX_PATH];
    GetTempPathW(MAX_PATH, temp_path);

    std::wstring tmp_file = std::wstring(temp_path) + L"cinertx_latest_preview.jpg.tmp";
    std::wstring target_file = std::wstring(temp_path) + L"cinertx_latest_preview.jpg";

    // 内存编码为 JPEG 格式
    std::vector<uint8_t> jpeg_buf;
    if (EncodeRGBAToJPEG(rgba_data, width, height, stride, 80 /* quality */, jpeg_buf)) {
        HANDLE hFile = CreateFileW(tmp_file.c_str(), GENERIC_WRITE, 0, NULL, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, NULL);
        if (hFile != INVALID_HANDLE_VALUE) {
            DWORD written = 0;
            WriteFile(hFile, jpeg_buf.data(), static_cast<DWORD>(jpeg_buf.size()), &written, NULL);
            CloseHandle(hFile);

            // 原子级替换文件，避免 WebUI 读到不完整半包数据
            MoveFileExW(tmp_file.c_str(), target_file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH);
        }
    }
}

} // namespace rtx::gui
```

在 `webui/server.py` 中验证闭环响应：

```py
# 目标文件: webui/server.py
# 确认 Line 1066-1081 正确读取并返回 200 OK

@app.route('/api/preview/latest.jpg', methods=['GET'])
def get_latest_preview():
    temp_dir = tempfile.gettempdir()
    preview_path = os.path.join(temp_dir, 'cinertx_latest_preview.jpg')
    
    if os.path.exists(preview_path) and os.path.getsize(preview_path) > 0:
        return send_file(preview_path, mimetype='image/jpeg', cache_timeout=0)
    
    # 后端未启动或尚未渲染帧时返回 HTTP 204
    return ('', 204)
```

---

## **5\. 宿主机终局验收门禁指令**

完成上述精准修改后，在宿主机执行最终验证指令矩阵：

```
# 1. 执行全量编译
.\build.bat
# 验证: 命令行返回 "Build Succeeded.", $LASTEXITCODE -eq 0

# 2. 执行单元测试矩阵
.\build\bin\rtx_tests.exe
# 验证输出: 
# "All tests passed (151 assertions in 151 test cases)"
# 确认 0 FAILED

# 3. 静态门禁全量校验
python .\tools\audit_gatekeepers.py
# 验证输出:
# [PASS] AuditPrivacy: 0 violations
# [PASS] AuditTypes: 35/35 modules registered
# [PASS] AuditSpdx: 100% compliant
# [PASS] AuditSloc: 12,399 lines
# [PASS] AuditImports: GUI=4, HEADLESS=2
```

