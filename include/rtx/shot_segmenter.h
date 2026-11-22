// SPDX-License-Identifier: GPL-3.0-or-later WITH Plugin-Exception
#pragma once

#include <array>
#include <cstdint>

namespace rtx {

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 多字段领域模型：具名字段承载序列化/校验/反射契约，压进 std::tuple 会丢失全部字段语义与默认值契约。
// Invariant: 字段集合与默认值即对外契约（schema/测试逐字段锚定）；只增不删。
struct ShotTransitionDecision {
    bool is_scene_cut = false;       // True if (prev, curr) crosses a shot boundary
    bool reset_vsr_temporal = false; // Must pass NVSDK_NGX_Parameter_Reset=1 on curr frame
    bool force_encoder_idr = false;  // Must force IDR keyframe on curr frame
    uint64_t shot_group_id = 0;      // Monotonically increasing Shot-Group (GOP) index
    float global_sad_norm = 0.0f;
    float outlier_block_ratio = 0.0f;
};

// @rtx-type-justification
// Category: Domain-Model
// Why-Std-Fails: 多字段领域模型：具名字段承载序列化/校验/反射契约，压进 std::tuple 会丢失全部字段语义与默认值契约。
// Invariant: 字段集合与默认值即对外契约（schema/测试逐字段锚定）；只增不删。
struct FrameDiffMetrics {
    float mean_sad = 0.0f;
    float max_block_sad = 0.0f;
    float changed_block_ratio = 0.0f;
    float hist_cosine_dist = 0.0f;
};

// @rtx-type-justification
// Category: NonTrivial-StateMachine
// Why-Std-Fails: 协调多资源异步生命周期的宿主状态机：std::unique_ptr/std::jthread/std::tuple 无法表达其声明序即析构逆序、线程亲和与受控降级路径的交叉不变量。
// Invariant: 成员声明序即依赖序（先声明者后析构）；状态迁移仅在持锁线程或属主线程发生。
class ShotGroupSegmenter {
public:
    ShotGroupSegmenter() noexcept = default;

    void reset() noexcept {
        shot_group_id_ = 0;
        frames_in_shot_ = 0;
        ema_motion_ = 0.03f;
    }
    [[nodiscard]] ShotTransitionDecision evaluate(
        const std::array<uint32_t, 64>& block_sad,
        int width, int height, int max_sample_val = 255) noexcept;

    [[nodiscard]] ShotTransitionDecision evaluateTransition(
        uint64_t frame_index,
        const std::array<uint32_t, 64>& block_sad,
        int width, int height, int max_sample_val = 255) noexcept {
        (void)frame_index;
        return evaluate(block_sad, width, height, max_sample_val);
    }
    [[nodiscard]] ShotTransitionDecision evaluateTransition(
        uint64_t frame_index,
        const FrameDiffMetrics& metrics) noexcept;

    [[nodiscard]] uint64_t currentShotGroupId() const noexcept { return shot_group_id_; }

private:
    uint64_t shot_group_id_ = 0;
    uint32_t frames_in_shot_ = 0;
    float ema_motion_ = 0.03f;
};

} // namespace rtx
