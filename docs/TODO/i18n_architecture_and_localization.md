# CineRTX 国际化 (i18n) 与多语言架构改造技术规范

> **文档标识**：`SPEC-I18N-TODO-002`  
> **所属目录**：[`docs/TODO/i18n_architecture_and_localization.md`](file:///[REDACTED_PROJ]/docs/TODO/i18n_architecture_and_localization.md)  
> **起草日期**：2026-10-25  
> **状态**：待评审 / 演进路线规划 (Draft / RFC)  
> **适用目标**：[`src/gui/rtx_gui.cpp`](file:///[REDACTED_PROJ]/src/gui/rtx_gui.cpp)、[`src/cli/main.cpp`](file:///[REDACTED_PROJ]/src/cli/main.cpp)、核心错误汇报链路

---

## 1. 现状审查与核心痛点 (Problem Statement)

### 1.1 现状盲区
经过对 CineRTX 现有源码的全面审计，目前的字符串与多语言体系存在严重的**单语言硬编码耦合**：
1. **GUI 视图层硬编码中文**：在 [`src/gui/rtx_gui.cpp`](file:///[REDACTED_PROJ]/src/gui/rtx_gui.cpp) 中，所有的 UI 标签、卡片标题、占位引导语及遥测指标名称均直接以 `L"..."` 中文字符串字面量写死在绘图函数内部：
   - `L"RTX 零拷贝转码引擎"`
   - `L"将 MP4 / MKV 文件拖入窗口，然后开始硬件转码。"`
   - `L"输入视频"`、`L"编码  HEVC"`、`L"预设  P7"`
   - `L"任务遥测"`、`L"吞吐"`、`L"帧数"`、`L"耗时"`、`L"零拷贝"` 等。
2. **CLI 终端输出中英混杂**：命令行解析器输出包含部分英文参数名与中文提示文本。
3. **开源国际化阻碍**：作为发布至 GitHub 的开源专业级视频重制与转码引擎，纯中文硬编码严重阻碍了全球开发者与海外用户的体验，极不符合工业级开源基础设施的规范。

---

## 2. i18n 架构设计原则与约束 (Architectural Principles)

1. **延续极简原生 0 外部依赖原则 (KISS & Zero External Dependencies)**：
   - 坚决杜绝引入庞大的第三方重型国际化库（如 GNU `gettext`、`libiconv`、`ICU`、`boost.locale`）；
   - 保持纯 C++20 / Win32 原生体系，单 exe 零伴生运行时依赖。
2. **极速零开销查询 (Zero-Allocation & O(1) Enum-Indexed Lookup)**：
   - GUI 运行于 60 FPS Win32 GDI 即时态自绘循环中；
   - 严禁在每一帧绘图时执行 `std::map<std::string, std::string>` 的哈希查找或堆内存动态分配；
   - 采用**强类型枚举索引（Enum ID-based Indexing）**，编译期偏移量直接定位，寻址开销严格为 $O(1)$，内存开销为 0。
3. **多层语言协商机制 (Multi-tier Language Negotiation)**：
   - 优先级 1：命令行显式参数（`--lang en` / `--lang zh-CN`）；
   - 优先级 2：环境变量 `CINERTX_LANG`（或 `LANG`）；
   - 优先级 3：宿主系统语言环境探测（Windows API `GetUserDefaultUILanguage()`）；
   - 默认兜底：国际通用英语（`en-US`）。
4. **统一双语契约 (Bilingual Baseline)**：
   - 首期基线完整覆盖 `en-US` (美式英语，默认标准) 与 `zh-CN` (简体中文)。

---

## 3. 技术架构方案与核心数据结构 (Technical Architecture)

### 3.1 强类型字符串枚举定义 (`include/rtx/i18n.h`)

通过集中管理的宏定义或枚举类定义全库所需文本键：

```cpp
#pragma once
#include <cstdint>
#include <string_view>

namespace rtx::i18n {

enum class Language : uint8_t {
    EnUs = 0,   // English (United States) - Default
    ZhCn = 1,   // Simplified Chinese
    Count
};

enum class StrId : uint32_t {
    AppTitle = 0,
    SubtitleTagline,
    GpuProbing,
    GpuUnavailable,
    DropCardTitle,
    DropCardHintEmpty,
    DropCardPipelineDesc,
    DropCardOutputPrefix,
    CodecButtonHevc,
    CodecButtonH264,
    PresetButtonPrefix,
    StartButtonReady,
    StartButtonRunning,
    StartButtonHalt,
    StartButtonEmpty,
    TelemetryTitle,
    TelemetrySubHeader,
    TelemetryProgressPercent,
    TelemetryProcessing,
    TelemetryColThroughput,
    TelemetryColFrames,
    TelemetryColElapsed,
    TelemetryColZeroCopy,
    TelemetryZeroCopyPass,
    TelemetryZeroCopyFail,
    StatusInitReady,
    StatusTranscoding,
    StatusSuccess,
    StatusCancelled,
    StatusFailed,
    Count
};

// 获取宽字符文本（GUI 使用）
const wchar_t* trW(StrId id, Language lang = currentLanguage()) noexcept;

// 获取 UTF-8 文本（CLI 与日志使用）
const char* trUtf8(StrId id, Language lang = currentLanguage()) noexcept;

// 语言上下文管理
Language currentLanguage() noexcept;
void setLanguage(Language lang) noexcept;
Language detectSystemLanguage() noexcept;
Language parseLanguageCode(std::string_view code) noexcept;

} // namespace rtx::i18n
```

### 3.2 扁平静态表驱动实现 (`src/core/i18n.cpp`)

采用紧凑的二维静态指针数组存储，保证纯静态数据段存储，无运行时构造开销：

```cpp
namespace {

using namespace rtx::i18n;

struct StringEntryW {
    const wchar_t* en;
    const wchar_t* zh;
};

// 编译期保证表长度与枚举完全对齐
constexpr StringEntryW kTableW[] = {
    // AppTitle
    {L"CineRTX Zero-Copy Transcode Engine", L"RTX 零拷贝转码引擎"},
    // SubtitleTagline
    {L"Win32 · Single EXE · Zero Runtime", L"Win32 · 单 exe · 无托管运行时"},
    // GpuProbing
    {L"Detecting GPU / NVENC capabilities...", L"拖入视频后检测 GPU / NVENC..."},
    // DropCardTitle
    {L"INPUT SOURCE", L"输入视频"},
    // DropCardHintEmpty
    {L"Drag & Drop MP4 / MKV video here", L"将 MP4 / MKV 文件拖入窗口"},
    // DropCardPipelineDesc
    {L"NVDEC Decode · NVENC Encode · Bit-Depth Preserved", L"NVDEC 解码 · NVENC 编码 · 保持源分辨率与位深"},
    // TelemetryTitle
    {L"Task Telemetry", L"任务遥测"},
    // TelemetryColThroughput
    {L"Throughput", L"吞吐"},
    // TelemetryColFrames
    {L"Frames", L"帧数"},
    // TelemetryColElapsed
    {L"Elapsed", L"耗时"},
    // TelemetryColZeroCopy
    {L"Zero-Copy", L"零拷贝"},
    // TelemetryZeroCopyPass
    {L"D2H 0 B · H2D 0 B (HW Verfied)", L"D2H 0 B · H2D 0 B (硬件零回传)"},
    // ...
};

static_assert(sizeof(kTableW) / sizeof(kTableW[0]) == static_cast<size_t>(StrId::Count),
              "i18n table size must exactly match StrId::Count");

} // namespace
```

### 3.3 系统语言自动协商逻辑

```cpp
Language detectSystemLanguage() noexcept {
    const LANGID langId = GetUserDefaultUILanguage();
    const PRIMARYLANGID primary = PRIMARYLANGID(langId);
    if (primary == LANG_CHINESE) {
        return Language::ZhCn;
    }
    return Language::EnUs;
}
```

---

## 4. GUI 与 CLI 迁移施工方案

### 4.1 GUI 渲染层（`src/gui/rtx_gui.cpp`）改造
在绘图函数 `drawFrame(HDC dc, int width, int height)` 中：
- 引入简短宏别名：`#define _TR(id) rtx::i18n::trW(rtx::i18n::StrId::##id)`
- 将原硬编码替换为：
  ```cpp
  // 改造前:
  text(dc, L"RTX 零拷贝转码引擎", ...);
  // 改造后:
  text(dc, _TR(AppTitle), ...);

  // 改造前:
  text(dc, L"任务遥测", ...);
  // 改造后:
  text(dc, _TR(TelemetryTitle), ...);
  ```
- **语言切换响应**：
  - 响应快捷键 `Ctrl + L` 或点击状态栏语言切换按钮；
  - 调用 `rtx::i18n::setLanguage(nextLang)`，并将 `g_dirty = true` 触发全窗 `InvalidateRect(hwnd, nullptr, FALSE)` 重新自绘，立即无缝生效。

### 4.2 CLI 终端层（`src/cli/main.cpp`）改造
- 增加命令行全局开关：`--lang [en|zh]`；
- 统一通过 `rtx::i18n::trUtf8(...)` 输出 Usage 帮助文档与进度遥测；
- 配合 Windows 控制台 `SetConsoleOutputCP(CP_UTF8)`，确保中英双语在任意代码页 Windows 终端中均零乱码。

---

## 5. 实施里程碑与排期计划 (Implementation Milestones)

| 阶段 | 实施任务 | 交付物与验证标准 |
| :---: | :--- | :--- |
| **Phase 4.1** | **核心抽象与双语字典落地** | 创建 [`include/rtx/i18n.h`](file:///[REDACTED_PROJ]/include/rtx/i18n.h) 与 [`src/core/i18n.cpp`](file:///[REDACTED_PROJ]/src/core/i18n.cpp)，注册全量枚举与静态表；编写单元测试验证枚举与字符串严格 1:1 对齐。 |
| **Phase 4.2** | **GUI 视图层全面去硬编码** | 全面替换 [`src/gui/rtx_gui.cpp`](file:///[REDACTED_PROJ]/src/gui/rtx_gui.cpp) 中的 `L"..."` 中文字面量；增加系统语言自适应探测。 |
| **Phase 4.3** | **CLI 终端参数集成与对齐** | 改造 [`src/cli/main.cpp`](file:///[REDACTED_PROJ]/src/cli/main.cpp)，增加 `--lang` 参数，完成帮助信息双语化。 |
| **Phase 4.4** | **无损自动化测试** | 在 CI/CD 单元测试中增加多语言覆盖率测试用例（覆盖 `en-US` 与 `zh-CN` 双模式渲染）。 |
