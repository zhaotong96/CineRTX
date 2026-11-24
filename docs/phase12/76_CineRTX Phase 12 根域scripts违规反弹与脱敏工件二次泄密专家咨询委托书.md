# CineRTX Phase 12 根域 scripts 违规反弹与脱敏工件二次泄密专家咨询委托书（净版）

**工程协调推进组 · 受控呈报工件**

---

## 基础元数据

| 属性项 | 内容详细 |
| :--- | :--- |
| **委托书编号** | `CINERTX-P12-DELEGATION-076`（已依 `CINERTX-P12-FINAL-PURITY-077` 原位净化） |
| **归档路径** | `docs/phase12/76_CineRTX Phase 12 根域scripts违规反弹与脱敏工件二次泄密专家咨询委托书.md` |
| **呈报主体** | 工程协调推进组 (Engineering Coordination Group) |
| **受呈主体** | 多媒体系统工程与 GPU 异构计算首席领域专家组 (Extreme Modern C++ Purist & Architecture Board) |
| **排查标的** | 根域 `scripts/` 全目录、第 73/74/75 号文档、`cmake/PackageRelease.cmake` |
| **前序依据** | `docs/phase11/38_CineRTX 现代 C++ 极限重构审计.md`（第四部分：物理铲除 `scripts/` 与 `schema/`）、`[DEC-002]` 资产物理隔离与奥卡姆剃刀铁律 |
| **日期** | 2026-11-24 |

---

## 一、 呈报背景与穿透排查结论

在 `v0.4.0` 发版收官前夕，项目负责人穿透审计交付资产并指出两项核心偏差，经工程协调推进组核查全部属实：

1. **铁证一（根域 `scripts/` 违规反弹）**：Phase 11.3 第 38 号终审审计与 `[DEC-002]` 早已明令物理铲除根域 `scripts/`，确立“一切门禁原生收拢于 `cmake/`、一切测试原生收拢于 `tests/`”的纯洁性红线；此前新增的外部 PowerShell / Python 脚本均属违规破窗反弹。
2. **铁证二（73 号施工单字面二次泄密）**：73 号施工单原稿在列举违规示例时直接写出了本地盘符用户目录、个人账户标识及本地 `file://` URI，构成受控文档二次泄密。
3. **铁证三（100% CMake-Native 自愈能力）**：全仓静态门禁与发布打包已完全由 `cmake/*.cmake` 承载，隐私审计亦可通过纯原生 `cmake/AuditPrivacy.cmake` 零外部依赖实现。

---

## 二、 专家裁决请求（已由 77 号终审裁决书全票批复）

1. 授权物理铲除根域 `scripts/` 全目录；
2. 确立纯原生 `cmake/AuditPrivacy.cmake` 为第五大静态硬门禁并接入 `cmake/PackageRelease.cmake`；
3. 原位净化第 73–76 号文档并统一收敛为纯 `cmake -P` 原生指令；
4. 固化唯一确定的三步纯 CMake 原生发版流水线，完成 `v0.4.0` 正式封板。
