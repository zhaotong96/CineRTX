# CineRTX 历史待办全量闭环台账与 v0.3+ 远期展望

> **目录定位**：`docs/TODO/` 本目录仅保留本总表；全部历史分册已提炼入
> [`docs/archive/Phase1_to_Phase7_Evolution_Digest.md`](../archive/Phase1_to_Phase7_Evolution_Digest.md) 后归档。
> **维护责任人**：CineRTX 架构委员会暨项目负责人
> **更新日期**：2026-11-14
> **核心军规基准**：[`docs/spec/architecture.md`](../spec/architecture.md) §1.3

---

## 1. 核心工程铁律：奥卡姆剃刀与绝无冗余开关 (Occam's Razor & Zero Switches)

**【加任何开关之前，必须先反思三遍，确认是否有绝对不可替代的必然性】**

CineRTX 坚决奉行“如无必要，勿增实体”的极简主义设计哲学：
1. **禁绝一切形式的随意开关**：严禁随意增加环境变量开关、命令行幽灵参数、CMake 条件编译宏或内部降级旁路。
2. **反思三遍门禁**：在考虑引入任何开关前，必须连续执行三次独立深入反思：
   - *第一遍反思*：这个开关解决的是真实物理冲突，还是为了逃避推导确定性最优解而偷懒？
   - *第二遍反思*：能否通过内建的自适应推导、确定性最佳实践或架构闭环直接达成，而无需用户/环境配置？
   - *第三遍反思*：这个开关是否把本该由系统承担的设计决策与认知负担转嫁给了用户或运维？
3. **单一确定性产品路径**：系统所有行为保持确定、自包含、无分支，拒绝将简单问题复杂化。

---

## 2. 历史待办全量闭环台账（2026-11-14 收官裁定）

全部历史分册已竣工合流，逐项闭环裁定如下（提炼详情见演进归档）：

| 历史待办分册 | 终局裁定 | 存活落点（当前权威代码/文档） |
| :--- | :--- | :--- |
| 音频流零拷贝透传与混流 | **100% 闭环** | AVPacket 零拷贝 Stream Copy + 全音轨/字幕直通（`src/io/`、`src/core/hw_transcode.cpp`） |
| 双端同步与专业调参 | **100% 闭环** | `kParamSchema` 19 项 SSOT 参数契约三端同步（`include/rtx/param_schema.h`） |
| 智能几何与竖屏自适应 | **100% 闭环** | 9:16 竖屏智能旋转 + Pillarbox 消色差黑边（`--pillarbox`，竖屏 `1.6875x` 动态倍率） |
| FFmpeg 7.1.1 选型与演进 | **100% 闭环** | FFmpeg 7.1.1 手术刀级静态内联锁定；`tools/build_ffmpeg_static_*.sh` 白名单体系 |
| GUI 调试日志去开关化 | **100% 闭环** | `OutputDebugStringW` + `[CineRTX]` 前缀零依赖内核日志（`include/rtx/logger.h`，DebugView 监看） |
| 国际化与本地化架构 | **100% 闭环** | `rtx::trW` 113 词条 constexpr 双语词典 + `Ctrl+L` 原地热切（`include/rtx/i18n.h`） |
| 打包与分发合规闭环 | **100% 闭环** | ~34 MB 绿色免安装发布包 + SHA-256 清单（`tools/package_release.py`） |
| 阶段五路线图与规格 | **100% 闭环** | 全媒体可用性 Wave 1~4 全部交付（音频保真、竖屏 4K 直出、绿色发布闭环） |
| 阶段六主待办 | **100% 闭环** | 架构全面瘦身与 C++26 纯化收官（原生控件矩阵、100% RAII、AV1 默认解锁、大刀阔斧瘦身） |
| RIFE CLI 测试夹具 | **能力闭环 / 推理级归入实机路径** | `--build-rife-engine` ONNX→TensorRT `.engine` 构建已交付；ABI/桥接/场景切逻辑由 4 项单测覆盖（`tests/test_rife.cpp`），GPU 推理验证归入实机验收 |
| UI 重构与插件状态 | **100% 闭环** | Win32 原生 "3+1" 四态工作站（NVIDIA 绿 `#76B900`，`src/gui/`）+ 插件健康诊断探针 |
| 双路真补帧架构 | **100% 闭环** | NVOFA 硬件光流（路线 A）+ TensorRT RIFE v4.25（路线 B）共存于 `rtx_ngx_bridge.dll`（ABI v4） |

**结论**：`docs/TODO/` 不再承载任何未闭环工程待办。当前权威系统实现一律以根目录
[`README.md`](../../README.md)、[`docs/spec/architecture.md`](../spec/architecture.md)
与 [`docs/README.md`](../README.md) 全景索引为准。

---

## 3. v0.3+ 远期展望（非承诺性路线备忘）

以下为架构委员会备案的远期演进方向，均属**评估性备忘**，未经专家终审立项前不得开工：

1. **NTSC 系时基扩展**：现行 `705,600,000 Hz` Flicks 对 23.976/29.97/59.94 fps 存在小数余数，评估乘 1001 系因子的扩展 Flicks 以实现 NTSC 系零余数对齐。
2. **FFmpeg 8.x 主线演进评估**：跟踪上游新特性，维持手术刀白名单静态裁剪体系下的增量升级路径。
3. **零拷贝线级证据补全**：引入 Nsight Systems 在验收主机上产出独立于进程内计数的 PCIe 流量画像，闭合 Gate 2.2 的 nsys 证据缺口。
4. **物理 HDR10 徽标现场确认**：元数据与码流级验证已全绿，物理显示端徽标需现场目视确认。
5. **长稳 soak 常态化**：将 2 小时连续满载（当前记录为环境受阻的 174 分钟累计满载）纳入常规硬件资格认证流程。
6. **Linux 无头云原生形态**：纯 CUDA 无头引擎与容器化部署曾在早期路线中立项并部分验证，恢复前须重新过专家终审。
7. **CPU 广播级编码轨道与 CRF 恒定质量码控演进**：依托底层的 `cpu_video_encoder.h/.cpp` 既有架构，评估在纯 GPU 硬件管线之外恢复接入 SVT-AV1 / x265 CPU 软编与 CRF 码控，以满足专业母版压制诉求。

---

## 4. 施工与发布依赖顺序

```mermaid
flowchart LR
    A["阶段三收官合拢<br>(文档漂移清零 + CI 契约闭环)"] --> B["阶段三终审验收总报告<br>(专家呈报)"]
    B --> C["v0.3+ 远期展望逐项立项<br>(经架构委员会三反思门禁)"]

    style A fill:#10b981,stroke:#047857,stroke-width:2px,color:#ffffff
    style B fill:#f59e0b,stroke:#b45309,stroke-width:2px,color:#ffffff
    style C fill:#3b82f6,stroke:#1d4ed8,stroke-width:2px,color:#ffffff
```
