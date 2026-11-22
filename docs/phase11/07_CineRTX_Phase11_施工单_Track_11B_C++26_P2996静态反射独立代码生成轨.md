# CineRTX Phase 11 施工单 Track 11B：C++26 P2996/P3394 静态反射独立代码生成轨

- **施工单编号**：`CINERTX-P11-TASK-11B`
- **优先级与依赖**：**P0（无前置依赖，与 Track 11A 严格并行）**
- **承接工程师**：编译器工具链与契约架构工程师（Compiler/Toolchain & Schema Engineer）
- **母本依据**：[`04_CineRTX Phase 11 冲突评判与方案.md`](./04_CineRTX Phase 11 冲突评判与方案.md) §2.1、§4（Track 11B）、§5（Gate 11B-1）
- **独占文件边界**：
  - 新建：`schema/schema_model.hpp`、`schema/reflection_codegen.cpp`、`schema/CMakeLists.txt`、`schema/generated/param_schema.generated.hpp`、`schema/generated/param_schema.generated.inc`、`schema/generated/schema.json`、`tests/test_param_schema_reflection.cpp`
  - 修改：`include/rtx/param_schema.h`、`src/core/param_schema.cpp`、`src/cli/main.cpp`、`.github/workflows/ci.yml`
  - **严禁触碰**：`src/core/hw_transcode*`、`src/gui/*`、`src/shim/*`、`CMakeLists.txt` 版本号区（由 Track 11A 独占）

---

## 一、 施工目标与隔离边界设计

在 [`v0.2.0`](https://github.com/zhaotong96/CineRTX/commit/52103a7363c9b431eb4ab8ea8da86007ebe2dd49) 中，虽然 20 项参数已进入 `kParamSchema`，但 [`src/core/param_schema.cpp`](../../src/core/param_schema.cpp) 与 [`src/cli/main.cpp`](../../src/cli/main.cpp) 内部仍存在 **4 处手写字段重复**（JSON 解析、JSON 序列化、Schema 导出、CLI 参数分发）。由于截至 2026 年 11 月 [MSVC 14.53 Preview 尚无 P2996/P3394 支持](https://devblogs.microsoft.com/cppblog/msvc-build-tools-preview-updates-november-2026/)，本轨采用 **“GCC 16 反射代码生成 + MSVC C++23 无缝消费”** 解耦架构：

```text
schema/schema_model.hpp (纯 C++26 契约，零 Win32/CUDA/FFmpeg/NGX 依赖)
        │
        ▼  [GCC 16: -std=c++26 -freflection]
schema/reflection_codegen.cpp (^^TranscodeProfile + [: :] + template for + P3394 注解)
        │
        ├──► schema/generated/param_schema.generated.hpp  (标准 C++23 元数据表 & 成员指针绑定)
        ├──► schema/generated/param_schema.generated.inc  (JSON 解析/序列化/CLI 映射自动展开)
        └──► schema/generated/schema.json                 (供 WebUI / CI 契约校验)
        │
        ▼  [MSVC /MT /std:c++latest & Zig 0.13 生产链直接消费，零反射语法依赖]
src/core/param_schema.cpp  +  src/cli/main.cpp
```

---

## 二、 原子 TDD 施工任务（Atomic Tasks）

### Task 11B-1：编写纯 C++26 契约模型（`schema/schema_model.hpp`）
- 严禁 `#include` 任何 Windows SDK、CUDA、TensorRT、NGX 或 FFmpeg 头文件，仅依赖标准库 `<string>`、`<cstdint>`。
- 使用 P3394 注解类型标注全部 20 项 `TranscodeProfile` 参数：
  ```cpp
  // schema/schema_model.hpp (SPDX: GPL-3.0-or-later WITH Plugin-Exception)
  #pragma once
  #include <string>

  namespace rtx::schema_meta {

  struct cli_flag         { const char* name; };
  struct json_key         { const char* name; };
  struct i18n_label       { const char* str_id; };
  struct enum_vals        { const char* csv; };
  struct int_range        { int min_val; int max_val; };
  struct normalized_lower {};
  struct hidden_in_cli    {};

  struct TranscodeProfile {
      [[=cli_flag("--input"),          =json_key("input"),          =i18n_label("ParamInput")]]
      std::string input{};

      [[=cli_flag("--output"),         =json_key("output"),         =i18n_label("ParamOutput")]]
      std::string output{};

      [[=cli_flag("--codec"),          =json_key("codec"),          =i18n_label("ParamCodec"),
        =enum_vals("av1,hevc,av1_cpu,hevc_cpu"), =normalized_lower{}]]
      std::string codec{"av1"};

      [[=cli_flag("--preset"),         =json_key("preset"),         =i18n_label("ParamPreset"),
        =int_range{0, 13}]]
      int preset = 5;

      [[=cli_flag("--rate-control"),   =json_key("rateControl"),    =i18n_label("ParamRateControl"),
        =enum_vals("cq,crf,vbr,cbr,qp"), =normalized_lower{}]]
      std::string rate_control{"cq"};

      [[=cli_flag("--qp"),             =json_key("qp"),             =i18n_label("ParamCq"),
        =int_range{0, 51}]]
      int qp = 22;

      [[=cli_flag("--pipeline-order"), =json_key("pipelineOrder"),  =i18n_label("ParamPipelineOrder"),
        =enum_vals("vsr-first,fg-first"), =normalized_lower{}]]
      std::string pipeline_order{"vsr-first"};
      // ... 完整补齐现有 include/rtx/param_schema.h 全部 20 项参数，默认值 100% 一致
  };

  } // namespace rtx::schema_meta
  ```

### Task 11B-2：实现 GCC 16 P2996/P3394 代码生成器（`schema/reflection_codegen.cpp` & `schema/CMakeLists.txt`）
1. 使用 [GCC 16](https://gcc.gnu.org/gcc-16/changes.html) `-std=c++26 -freflection` 编译 `reflection_codegen.cpp`：
   - 通过 `std::meta::nonstatic_data_members_of(^^rtx::schema_meta::TranscodeProfile, ...)` 与 `template for` 遍历全部成员；
   - 通过 `std::meta::annotations_of(member)` 提取 `cli_flag`、`json_key`、`i18n_label`、`enum_vals`、`int_range`、`normalized_lower`；
   - **编译期硬校验（`consteval` / `static_assert`）**：
     - 每个成员必须具备 `json_key` 与 `i18n_label`；
     - 全局所有成员的 `json_key` 必须唯一，非空 `cli_flag` 必须唯一；若有重复直接触发**编译期失败**！
   - **稳健默认值读取**：在生成器内实例化一个默认对象 `const rtx::schema_meta::TranscodeProfile defaults{};`，用 `defaults.[:member:]` 直接读取真实运行时默认值，避免脆弱地解析复杂初始化器 AST。
2. 生成三份带 `// GENERATED FILE — DO NOT EDIT` 头部与 SHA-256 源指纹的产物：
   - `schema/generated/param_schema.generated.hpp`（生成标准 C++23 `kParamSchema` 与 `kFieldBindings` 成员指针描述符表）；
   - `schema/generated/param_schema.generated.inc`（生成类型分发辅助函数：`forEachProfileField(profile, visitor)`）；
   - `schema/generated/schema.json`（与 `--schema-json` 的 `params` 字段结构 1:1 匹配）。

### Task 11B-3：主工程 MSVC C++23 消费对接（`param_schema.h/.cpp` & `cli/main.cpp`）
1. `include/rtx/param_schema.h` 包含 `schema/generated/param_schema.generated.hpp`，使 `kParamSchema` 成为生成产物。
2. 重构 `src/core/param_schema.cpp`：
   - `parseProfileFromJson()`、`profileToJson()` 与 `exportSchemaJson()` 全部改为调用 `forEachProfileField` 驱动，彻底消灭 20 项手写重复；
   - **明确保留显式跨字段业务规则校验**（静态反射不替代业务逻辑）：`av1_cpu`/`hevc_cpu` 与 `crf`/`cq` 互转、`resolution=native` 与宽高覆盖、RIFE 路径检查、输出路径自动派生；
   - **加固零依赖 JSON 解析器**：补齐字符串转义（`\"`, `\\`, `\uXXXX`）、嵌套对象边界与**重复 Key 拒绝**语义。
3. 重构 `src/cli/main.cpp`：常规单值 CLI 参数解析改由生成表的 `cli_flag` 映射驱动，仅保留特殊布尔翻转旗标（如 `--no-vsr`、`--pillarbox`）与子命令（`--probe`、`--schema-json`、`--session-stdio`）。

### Task 11B-4：CI 自动化 Diff 门禁与非阻断 `clang-p2996` 探索支线
1. 在 `.github/workflows/ci.yml` 新增 Linux `reflection-codegen-check` Job：安装/使用 GCC 16 运行 `reflection_codegen`，对 `schema/generated/` 执行 `git diff --exit-code`，确保任何人在修改 `schema_model.hpp` 后不会忘记重新生成产物。
2. 编写 `tests/test_param_schema_reflection.cpp`：验证 20 项参数 JSON 往返保真、重复 Key 拒绝、转义字符边界、跨字段业务校验，以及单处新增测试字段的生成验证脚本。
3. （可选非阻断支线）记录一条实验性脚本探索 `clang-p2996`（`clang-cl`）构建可行性，不阻塞主干发布。

---

## 三、 量化验收门禁 Checklist（Gate 11B-1）

- [ ] **GCC 16 反射生成闭环**：`g++-16 -std=c++26 -freflection schema/reflection_codegen.cpp -o reflection_codegen && ./reflection_codegen` ExitCode `0`，产出 3 份生成文件。
- [ ] **编译期防重负向测试**：在 `schema_model.hpp` 中人为制造重复 `json_key` 或重复 `cli_flag`，确认 GCC 16 在**编译期**报错中止。
- [ ] **单一事实源实证**：新增一个临时测试字段仅修改 `schema/schema_model.hpp` 一处，重新运行生成器后，JSON 解析、序列化、CLI 与 `schema.json` 自动同步生效。
- [ ] **生产链零破坏**：Windows MSVC `/std:c++latest`（`cmd /c build.bat`）与 Zig 0.13 预设均 100% 编译通过，`rtx_tests.exe`（含 `test_param_schema_reflection`）全部 PASS。
- [ ] **CI 一致性门禁**：`.github/workflows/ci.yml` 的 `reflection-codegen-check` Diff 检查为 0 差异；`AuditSpdx.cmake` PASS。
