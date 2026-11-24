# CineRTX Phase 12 遮蔽规范统一与隐私门禁自泄露风险专家咨询委托书

- **文件编号**：`CINERTX-P12-DELEGATION-084`
- **归档路径**：`docs/phase12/84_CineRTX Phase 12 遮蔽规范统一与隐私门禁自泄露风险专家咨询委托书.md`
- **委托主体**：CineRTX 工程协调推进组（Engineering Coordination Board）
- **受托专家**：多媒体系统工程与 GPU 异构计算首席领域专家组 / 隐私与合规终审官
- **审计基线**：Commit `d4a63cf`（`feat(phase12): complete Phase 12 C++26 Std-First refactor, headless DLL clearance, pure CMake gates, and seal v0.4.0`）
- **签发日期**：2026-11-24
- **当前状态**：**HOLD git push — 全仓锁定态本地待命，提请专家对遮蔽符号语义规范与隐私门禁脚本自泄密风险进行最高级别法医级仲裁**

---

## 一、 委托背景与核心关切

在 Commit `d4a63cf` 达成 Phase 12 全量提交本地压缩合并后，工程协调推进组与项目负责人在复核 [83号报告](83_CineRTX%20Phase%2012%20%E5%85%A8%E4%BB%93%E9%94%81%E5%AE%9A%E6%80%81%E9%9A%90%E7%A7%81%E6%B3%84%E9%9C%B2%E9%A3%8E%E9%99%A9%E7%BB%88%E5%AE%A1%E6%8E%92%E6%9F%A5%E6%8A%A5%E5%91%8A.md) 及现行 [AuditPrivacy.cmake](../../cmake/AuditPrivacy.cmake) 时，敏锐捕捉到两项关乎开源合规与信息安全底层的重大哲学与工程隐患：

1. **遮蔽占位符符号漂移与二次信息泄密**：近期文档中大量涌现自定义尖括号占位符（如 `userHome`、`repoRoot`、`testHost`、`hostPublicDir` 的尖括号形式），不仅违背了项目自 Phase 1 至 Phase 11 确立的 `[REDACTED_*]` 黄金标准，更因尖括号语义描述与 Markdown/HTML 标签解析冲突造成二次线索外溢。
2. **门禁脚本特征库自身沦为“泄密总账”的严重反噬**：`cmake/AuditPrivacy.cmake` 虽通过 `string(CONCAT ...)` 拼装规避了自检报错，但公开代码中的拼装碎片（`"z"` `"t"`、`"M"` `"y"`、`"PUB"` `"LIC"`、`"we"` `"ak"`）等同于向全世界公开了作者用户名、私有盘符路径与测试机凭据。

为此，协调推进组正式立案，呈请首席领域专家组就上述两大议题出具权威法医鉴定与终审裁决方案。

---

## 二、 专家咨询核心议题与法医级剖析

### 议题一：尖括号 `<...>` 遮蔽方式违背历史 `[REDACTED]` 规范与二次泄密定性

#### 1. 历史基线与既定规范（SSOT）
经查证全仓演进档案（[`docs/archive/Phase1_to_Phase7_Evolution_Digest.md:83`](../archive/Phase1_to_Phase7_Evolution_Digest.md) 及 Phase 8/9/10/11 历次终审报告），CineRTX 项目早已正式确立了遵循工业级安全合规标准的 **`[REDACTED_*]` 方括号大写占位符体系**：
- 宿主机名与环境：`[REDACTED_HOST]`
- 本地私有路径：`[REDACTED_PATH]`
- 内部 IP 地址：`[REDACTED_IP]`
- 私有代码库与用户名：`[REDACTED_REPO]`、`[REDACTED_USER]`
- 临时构建目录：`[REDACTED_TEMP_DIR]`

#### 2. 当前尖括号 `<...>` 的物理缺陷与二次泄密危害
近期在 Phase 12 收口中，部分文档与脚本自动化替换生成了诸如 `userHome`、`repoRoot`、`testHost`、`hostPublicDir`、`maintainer@example.com` 的尖括号形式：
1. **Markdown / HTML 渲染破坏**：在 GitHub Flavored Markdown (GFM) 中，未转义的尖括号占位符会被解释器优先尝试解析为 HTML 标签或原生 AutoLink。若标签名非标准 HTML，可能导致正文在部分渲染器中被静默吞没或排版割裂。
2. **拓扑语义二次泄密（Inference Attack）**：
   - 使用 `hostPublicDir`、`testHost`、`userHome` 等具象尖括号词汇，直接向外界暴露了开发团队内部的环境拓扑（即存在非标准公开共享盘、存在专用测试机 SSH 会话、存在本地私密映射）；
   - 特别是形如 `\\[REDACTED_HOST]` 或邮箱格式的尖括号占位符，其结构本身就泄露了网络通信模式与上下文语义，构成了实质性的**二次情报泄露**。
3. **规范断代与坏味道**：使历史文档与新文档在遮蔽标准上发生分裂，破坏了项目奥卡姆剃刀与极简一致性要求。

**【请专家裁定】**：
1. 是否应在全仓范围内彻底废除尖括号 `<...>` 遮蔽风格，全面回退并强制统一至 Phase 1~11 既有的 `[REDACTED_*]` 规范？
2. 在具体的脱敏落盘中，如何界定统一的替换映射表（例如：宿主机绝对路径 $\to$ `[REDACTED_PATH]` 或相对路径，测试机信息 $\to$ `[REDACTED_HOST]`）？

---

### 议题二：`cmake/AuditPrivacy.cmake` 规则特征库自身是否构成严重二次泄密？

#### 1. 物理事实与取证物证
审查现行 [`cmake/AuditPrivacy.cmake`](../../cmake/AuditPrivacy.cmake) 第 21–31 行源码：
```cmake
string(CONCAT _LEGACY_HANDLE "z" "t")
set(_POSIX_HANDLE_RX "[/\\\\]Users[/\\\\]${_LEGACY_HANDLE}([^A-Za-z0-9_-]|$)")
string(CONCAT _CLOUD_M "M" "y")
string(CONCAT _CLOUD_G "Goo" "gle")
set(_CLOUD_DRIVE_RX "(${_CLOUD_M}|${_CLOUD_G})[ \t]+Drive")
string(CONCAT _HOST_PUB_RX "[A-Za-z]:[/\\\\]" "PUB" "LIC[/\\\\]CineRTX")
string(CONCAT _TEST_SSH_RX "(^|[^A-Za-z0-9_])" "we" "ak" "@(127\\.0\\.0\\.1|localhost|[0-9]+\\.[0-9]+\\.[0-9]+\\.[0-9]+)")
```

#### 2. “特征库即泄密源（Signature-as-Leakage）”的本质解剖
1. **自欺欺人的“自检掩蔽”**：
   `string(CONCAT "z" "t")`、`"PUB" "LIC"`、`"we" "ak"` 的动态拼接机制，其唯一的技术效果是让 `cmake/AuditPrivacy.cmake` 在扫描自身时不匹配字面量，**防止自己报出 Privacy Hit**。
2. **源码即情报字典的公开泄密**：
   作为开源仓库，任何下载或浏览该文件的第三方人员（包括安全扫描器与潜在攻击者），只需阅读这几行拼装代码，就能**毫不费力、100% 确凿地得知**：
   - 项目核心作者在本机的用户名（`[REDACTED_USER]`）；
   - 本地工程位于云端同步盘（`[REDACTED_CLOUD_DRIVE]`）；
   - 宿主机曾经使用的物理绝对路径（`[REDACTED_PATH]`）；
   - 历史测试机曾使用的 SSH 登录账号前缀（`[REDACTED_HOST]`）。
3. **门禁反噬悖论**：
   我们在 30 余篇受控文档中耗费大量精力逐一清除的私有痕迹，居然在公开的门禁脚本中被完整、结构化、集中式地“汇编建档”！门禁脚本自身沦为了**全仓最大、最精准的个人隐私索引表**。

**【请专家裁定】**：
1. `cmake/AuditPrivacy.cmake` 现行的私有特定字符拼装机制，在法医级与开源合规标准下，**是否构成毫无疑义的二次泄密？**
2. **如何实施根治性重构？**
   - *方向 A（通用启发式防御）*：完全剔除任何与具体作者、具体账号相关的字面片段，仅保留通用模式匹配（例如：匹配通用 Windows 用户目录 `[A-Za-z]:[/\]+Users[/\]+[A-Za-z0-9._-]+`，通用云盘特征 `(Drive|Sync)[/\]+`，通用私网 IP `_LAN_IP_RX`，通用密钥 `_SECRET_RX`）；
   - *方向 B（体外安全字典注入）*：若必须检测特定私密关键字，是否应将其剥离出代码库，改由 CI 环境变量或本地专用配置文件在运行期动态注入，严禁进入版本库公开追踪？

---

## 三、 专家交付物与裁决指令要求

请首席领域专家组对照 CineRTX 极限工程纯洁度哲学，出具 **`85_CineRTX Phase 12 遮蔽规范统一与隐私门禁去自泄露终审裁决书.md`**，明确以下结论：

1. **定性结论**：正式对尖括号二次泄密及 `AuditPrivacy.cmake` 规则特征库自泄密进行权威技术定性；
2. **标准规范**：给出全仓统一的 `[REDACTED_*]` 替换标准规范与清洗字典；
3. **源码重构**：提供彻底脱敏、纯通用化、零自身特征泄密的全新 `cmake/AuditPrivacy.cmake` 标准代码范式；
4. **收口指令**：给出将上述两项整改合并合入当前 Commit `d4a63cf`（保持 Phase 12 单 Commit 纯洁性）的精确施工令与验收判定标准。
