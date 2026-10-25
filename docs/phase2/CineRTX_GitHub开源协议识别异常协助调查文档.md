# **CineRTX 纯 GPU 转码引擎：GitHub 开源许可证识别异常 (License: Other) 深度调查与专家咨询请求书**

**文档标识**：`DOSSIER-LICENSE-INVESTIGATION-20261025`  
**呈报对象**：CineRTX 领域专家委员会（开源法务与供应链安全审查组 / 系统架构组）  
**调查负责人**：CineRTX 工程组与系统架构团队  
**基准仓库**：[`https://github.com/zhaotong96/CineRTX.git`](https://github.com/zhaotong96/CineRTX.git)  
**涉及核心文件**：
- 主许可证文件：[`LICENSE`](file:///C:/[REDACTED]/LICENSE)
- 例外条款文件：[`COPYING.EXCEPTION`](file:///C:/[REDACTED]/COPYING.EXCEPTION)
- 垫片子许可证：[`LICENSE-MIT`](file:///C:/[REDACTED]/LICENSE-MIT) 与 [`LICENSE-APACHE`](file:///C:/[REDACTED]/LICENSE-APACHE)
- 自述与法律指引：[`README.md`](file:///C:/[REDACTED]/README.md)
- 本地合规校验门禁：[`tools/check_spdx.ps1`](file:///C:/[REDACTED]/tools/check_spdx.ps1)

---

## 一、 异常现象与定性复现

在将 CineRTX 项目推送到 GitHub 远程仓库后，仓库主页右上角 `About` 区域的许可证徽标（License Badge）未能按预期展示为标准的 **`GPL-3.0 license`** 或 **`GNU General Public License v3.0`**，而是被 GitHub 系统识别为 **`Other`**。

调用 GitHub REST API (`GET /repos/zhaotong96/CineRTX`) 获取的仓库元数据证实了此异常：

```json
{
  "license": {
    "key": "other",
    "name": "Other",
    "spdx_id": "NOASSERTION",
    "url": null,
    "node_id": "MDc6TGljZW5zZTA="
  }
}
```

**问题定性**：  
此现象虽不改变项目在法律层面的实际授权效力，但对于开源社区具有显著的负面影响：
1. **开发者信任成本增加**：在 GitHub 社区，标有 `Other` 的仓库常被潜在贡献者与企业用户预设为“包含专有自创限制”或“非标准 OSI 认证协议”，阻碍外部采用；
2. **开源索引排他性**：GitHub 搜索按 `license:gpl-3.0` 进行协议过滤时，本仓库将被完全排除在外；
3. **供应链扫描误报**：依赖 GitHub 官方 License API 的自动化法务合规审计工具（如 FOSSA、Snyk、Black Duck）会将项目标记为 `NOASSERTION / Unresolved`，触发供应链审查阻断。

因此，亟需彻底查清 GitHub 识别引擎的底层判定因果链，并由领域专家委员会裁定最优的解耦与修复方案。

---

## 二、 底层识别因果链深度溯源（Root Cause Analysis）

工程组结合 GitHub 官方采用的许可证检测开源引擎 **Licensee**（Ruby Gem，版本 v9.x）的源码与匹配策略，对工作区进行了穿透式比对分析，锁定了以下三层诱因：

```
                              ┌──────────────────────────────────────────────┐
                              │          GitHub 仓库首页 License: Other       │
                              └──────────────────────▲───────────────────────┘
                                                     │ 触发
                               ┌─────────────────────┴────────────────────────┐
                               │   Licensee 探测器返回 key: "other" (NOASSERTION)   │
                               └─────────────────────▲────────────────────────┘
                                                     │
           ┌─────────────────────────────────────────┼─────────────────────────────────────────┐
           │                                         │                                         │
┌──────────┴──────────┐                   ┌──────────┴──────────┐                   ┌──────────┴──────────┐
│   诱因 1: 物理篡改   │                   │ 诱因 2: 未注册例外  │                   │ 诱因 3: 多文件平铺  │
│ LICENSE 头部混编例外 │                   │ SPDX 不识别私有规则 │                   │ 根目录多 License 冲突│
│ (破坏 GPLv3 纯洁度)  │                   │ 相似度跌破 98% 门限 │                   │ (MIT/Apache 并存)   │
└─────────────────────┘                   └─────────────────────┘                   └─────────────────────┘
```

### 1. 直接诱因：`LICENSE` 文件头部前置了 17 行自定义例外文本

查看远程提交记录，提交 `e666586`（*Update LICENSE to include NVIDIA RTX Linking Exception*）直接将例外描述硬编码在了 [`LICENSE`](file:///C:/[REDACTED]/LICENSE#L1-L18) 的最顶端：

```text
==============================================================================
CineRTX Plugin-Exception to GNU General Public License v3.0
==============================================================================

In addition to the permissions granted under the GNU General Public License v3.0,
the copyright holders of CineRTX grant permission to link this program with the
header files, static stub libraries (such as nvsdk_ngx_s.lib) and dynamic/shared libraries of NVIDIA proprietary SDKs...
...
[前置 17 行自创文本]
==============================================================================
                    GNU GENERAL PUBLIC LICENSE
                       Version 3, 29 June 2007
```

**Licensee 探测算法机理**：
1. **ExactMatcher 失败**：Licensee 的 `ExactMatcher` 依赖文件与 `choosealicense.com` / SPDX 官方发布的标准许可证文本的 SHA1 严格一致。前置插入 17 行文本后，哈希比对必然失效；
2. **DiceMatcher 跌破置信度阈值**：Licensee 的模糊匹配器采用 **Sørensen–Dice 系数**（词元 Bigram 重合率，默认阈值高达 98%）。将非标准法律文本直接混合进正文，大幅稀释了 Bigram 统计权重；
3. **首部模式（Header Matcher）失效**：Licensee 对 GPL 家族有专门的标题行扫描锚点（需以 `GNU GENERAL PUBLIC LICENSE` 开头）。文件前 17 行充满自定义的专有 SDK 声明，直接打断了许可证标题的正则锚定。

### 2. 标准性诱因：`CineRTX Plugin-Exception` 属于未在 SPDX 注册的私有例外

SPDX 规范虽然原生支持 `WITH <Exception-Identifier>` 复合语法（如 `GPL-3.0-or-later WITH GCC-exception-3.1` 或 `GPL-2.0-or-later WITH Classpath-exception-2.0`），但前提是该例外必须属于 SPDX 官方认可的例外列表（SPDX Exceptions List）。

本项目自创的 `CineRTX Plugin-Exception` 在 SPDX 官方白名单中不存在。当 Licensee 扫描到一个声称是 GPL 但正文包含未注册例外文本的文件时，其降级保护机制（Fallback Logic）会判定该文本为“带有未知私有约束的衍生变体”，从而强制返回：
```ruby
license.key = "other"
license.name = "Other"
```

### 3. 架构性诱因：违背了 FSF 官方“许可证逐字纯洁性（Verbatim Copy）”原则

自由软件基金会（FSF）在 GPLv3 官方文本尾部明确规定：
> *"Everyone is permitted to copy and distribute verbatim copies of this license document, but changing it is not allowed."*

根据 GPLv3 第 7 节（Additional Permissions）的标准法律工程实践：
- GPLv3 文本本身必须保持 **100% 官方原版、逐字未动**；
- 额外的追加许可（Additional Permission / Exception）应当作为 **独立的法律附件**（如 `COPYING.EXCEPTION`）伴随分发，或者在自述文件（`README.md`）与各源码文件的 SPDX 头部中引用，**绝不应在 `LICENSE` 主文件内强行拼接混编**。

### 4. 辅助诱因：根目录多许可证文件的平铺并存

当前仓库根目录下平铺放置了：
- `LICENSE`（主许可）
- `COPYING.EXCEPTION`（例外条款）
- `LICENSE-MIT`（垫片库许可 A）
- `LICENSE-APACHE`（垫片库许可 B）

虽然 Licensee 在设计上优先匹配名为 `LICENSE` 的文件，但当 `LICENSE` 因混编文本导致置信度不足时，多文件共存会加剧探测器的歧义评分，促使其直接落入 `other` 分支。

---

## 三、 解决方案对比与工程推演

为使 GitHub 仓库主页既能正确点亮 **`GPL-3.0 license`** 权威开源标识，又在法律层面 100% 保障与 NVIDIA 专有 SDK 动态链接的法务例外安全，工程组梳理出三套实施路线：

| 评估维度 | 方案一：主件原版纯洁 + 独立例外解耦（行业金标推荐） | 方案二：GPLv3 单文件尾部追加第 7 节声明 | 方案三：REUSE 2.0 标准规范目录化 (`LICENSES/`) |
| :--- | :--- | :--- | :--- |
| **文件组织结构** | - `LICENSE`：恢复为 100% 官方纯净 GPLv3<br>- `COPYING.EXCEPTION`：完整承载例外<br>- `README.md`：声明主许可与追加授权 | - `LICENSE`：前面为纯净 GPLv3，在正文结束后追加第 7 节授予声明<br>- 废除 `COPYING.EXCEPTION` | - 根目录保留纯净 `LICENSE`<br>- 新建 `LICENSES/` 目录，存放各 SPDX 标准文件<br>- 根目录清理多余许可证文件 |
| **GitHub 徽标识别** | **100% 稳定识别为 `GPL-3.0`**（哈希一致） | **高风险依然显示为 `Other`**（Dice 系数受损） | **100% 识别为 `GPL-3.0`**，且符合 Linux 基金会规范 |
| **FSF 法务纯洁性** | 完全合规（严格遵循 GPLv3 逐字分发原则） | 存在法律争议（混编可能被视为篡改正文） | 完全合规（严密的多许可与例外隔离） |
| **垫片双许可处理** | `include/shim/` 文件头声明 MIT/Apache-2.0，根目录保留或移入规范子目录 | 垫片库许可证与主文件界限模糊 | 垫片许可证清晰归档于 `LICENSES/` |
| **施工改动成本** | **极低（只需剥离 `LICENSE` 前 17 行前缀）** | 较低 | 中等（需重构整个许可目录并调整脚本） |

---

## 四、 拟推荐施工细则（方案一详解）

### 1. `LICENSE` 文件物理纯化
彻底移除 [`LICENSE`](file:///C:/[REDACTED]/LICENSE) 顶部的 17 行自创文本，使文件首行严格回归官方正文：
```text
                    GNU GENERAL PUBLIC LICENSE
                       Version 3, 29 June 2007

 Copyright (C) 2007 Free Software Foundation, Inc. <https://fsf.org/>
 Everyone is permitted to copy and distribute verbatim copies
 of this license document, but changing it is not allowed.

                            Preamble
...
```

### 2. `COPYING.EXCEPTION` 法律效力权威锚定
保持当前 [`COPYING.EXCEPTION`](file:///C:/[REDACTED]/COPYING.EXCEPTION)（版本 1.0，2026-10-21，包含 Issue 12 静态桩库修正）的独立性与完整性。该文件严格依据 GPLv3 第 7 节定义追加许可（Additional Permission），明确涵盖动态库与 `nvsdk_ngx_s.lib` 静态寻址桩库。

### 3. `README.md` 与文档双向明示
在 [`README.md`](file:///C:/[REDACTED]/README.md) 的“许可证与合规”章节首段进行权威澄清：
> **License & Exceptions**:  
> CineRTX core engine is licensed under the **GNU General Public License v3.0 or later** ([`LICENSE`](LICENSE)), with the **CineRTX Plugin-Exception** ([`COPYING.EXCEPTION`](COPYING.EXCEPTION)) allowing dynamic linkage with NVIDIA proprietary SDKs. Shims and headers located in `include/shim/` and `src/shim/` are permissively licensed under **MIT OR Apache-2.0** ([`LICENSE-MIT`](LICENSE-MIT) / [`LICENSE-APACHE`](LICENSE-APACHE)).

### 4. 自动化合规脚本自愈
工作区现有的 [`tools/check_spdx.ps1`](file:///C:/[REDACTED]/tools/check_spdx.ps1) 原生校验全库文件的 SPDX 头（`GPL-3.0-or-later WITH Plugin-Exception` 与 `MIT OR Apache-2.0`），剥离 `LICENSE` 前置文本完全不影响本地审计输出（仍为 PASS）。

---

## 五、 呈请领域专家委员会决议咨询清单 (Questions for Sign-off)

请专家委员会重点就以下 4 个法务与系统架构维度予以审议并签署裁定：

1. **法务等价性裁定**：
   将例外条款完全转移至独立的 `COPYING.EXCEPTION` 并在 `LICENSE` 中保持纯净的 GPLv3 正文，在著作权法与开源司法实践中是否与单文件混编具有完全相同、甚至更为坚固的法律约束力？
2. **根目录辅助许可文件处置**：
   根目录现有的 `LICENSE-MIT` 与 `LICENSE-APACHE` 是否建议继续保留在根目录，还是建议移入 `third_party/` 或 `docs/legal/` / `LICENSES/` 目录，以彻底排除任何多文件竞争诱发 Licensee 误判的可能性？
3. **开源社区认知对齐**：
   诸如 Dolphin Emulator、OBS Studio、Blender 等具备专有例外或驱动隔离机制的知名开源项目，在 GitHub 均规范显示为 `GPL-2.0` 或 `GPL-3.0` 徽标。本工程采用方案一恢复 `GPL-3.0` 徽标，是否完全符合开源治理道德与社区公认惯例？
4. **签批执行授权**：
   专家组若批准方案一，工程组将立即实施 `LICENSE` 文件纯化，并请示最终 GitHub 提交与推送。

---

**呈报单位**：CineRTX 系统工程组  
**日期**：2026-10-25
