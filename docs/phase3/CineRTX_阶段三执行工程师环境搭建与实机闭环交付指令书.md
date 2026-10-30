# **CineRTX 纯 GPU 转码与非编创作引擎：阶段三执行工程师轻量开发环境搭建与实机闭环交付指令书**

**文档标识**：`DIRECTIVE-PHASE3-ENGINEER-CLOSURE-20261026`  
**下达机构**：CineRTX 架构审查与质量控制委员会  
**受令对象**：阶段三全栈执行工程组  
**关联交付物**：[`[REDACTED]/CineRTX_phase3_fullstack.patch`](file:///C:/[REDACTED]/CineRTX_phase3_fullstack.patch) 与 [`[REDACTED]/report.md`](file:///C:/[REDACTED]/report.md)  
**技术依据**：
1. 阶段三前置静态自包含施工方案：[`docs/phase3/CineRTX_FFmpeg全量静态链接全栈施工方案.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase3/CineRTX_FFmpeg全量静态链接全栈施工方案.md)（`SPEC-FFMPEG-STATIC-CONSOLIDATION-20261026`）
2. 阶段三全栈工程实施方案：[`docs/phase3/CineRTX_阶段三全栈工程施工方案.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/phase3/CineRTX_阶段三全栈工程施工方案.md)（`SPEC-PHASE3-CONSTRUCTION-20261025`）
3. Gate 3 验收规范：[`docs/spec/gate3.md`](file:///C:/PUBLIC/[REDACTED_PROJ]/docs/spec/gate3.md)

---

## 一、 现状评审与交付定性：为什么当前交付物“尚未真正完工”？

架构审查委员会对执行工程师提交的补丁及报告进行了全量代码审计，审查结论如下：

### 1.1 肯定项（已完成约 75% 的静态架构与源码铺设）
1. **代码编写面面俱到**：56 个文件、+6,315 行代码，已全量覆盖 Gate 3.0 前置攻坚（宏/存根清除、`MemoryAvioContext`、静态供应链、PE 审计升级）及 W7–W9 全部主线特性（NvOFFRUC ABI、128 位 Flicks 时基、零分配 i18n、RIFE 引擎、PingPong 双缓冲、遥测状态栏、DirectComposition 视口、AVX2 混音器）；
2. **测试用例扩充达标**：单元测试用例从 113 项精准扩展至 **137 项**（+24 项），CTest 门禁扩充至 6 项；
3. **静态合规与防线严密**：SPDX 审计通过（`licenses: GPL=78 MIT=42 binaries=0 models=0`），`COPYING.EXCEPTION` 升至 v1.2，模型权重拦截生效；
4. **补丁应用干净**：补丁格式规范，在主干 `32a396f` 上应用 0 冲突。

### 1.2 致命硬伤与未完成项（缺失关键的 25% 物理构建与运行闭环）
正如执行工程师在 `report.md` 第 38 行所坦诚说明：
> *“本机无 MSVC / CMake / MSYS2 / nvcc，**未实机编译与运行 ctest**。因此 Gate 3.0 的 GATE-3.0-AUDIT（PE 导入表）、体积包络与隔离目录实机转码，需在具备 VS2022 + 静态 FFmpeg 归档的机器上执行……后闭环。”*

在工业级质量管理体系中，**未经真实物理编译器编译通过、未实际运行并通过测试用例、未生成可执行二进制的代码草案，绝不能判定为生产可用的交付物！** 核心缺失如下：
1. **零物理编译（Zero Physical Build）**：6315 行新增 C++20 代码从未经过编译器验证，是否存在符号冲突、命名空间歧义或链接器未解析外部符号（LNK2019/LNK2038）尚未可知；
2. **缺失手术刀静态库实体**：`tools/build_ffmpeg_static_msvc.sh` 未实际运行，未生成合规的 `avformat.lib`（> 450 KB）等静态库，导致 `RtxFfmpegStatic.cmake` 必然触发 `FATAL_ERROR` 阻断构建；
3. **Gate 3.0 与 Gate 3 门禁未经实测**：`audit_imports.ps1` 对最终 PE 二进制的扫描、单 EXE 8~12MB 黄金体积测量、137 项单测实跑、独立空白隔离目录双击 4K MP4 实机转码均未完成。

---

## 二、 极简开发环境搭建指南（彻底告别数十 GB 的庞大 VS IDE）

执行工程师之所以未能在本地完成编译，是受制于“以为必须安装数十 GB 臃肿的 Visual Studio IDE”的思维定势。事实上，CineRTX 架构原生支持轻量环境，请根据本地机器条件**任选以下一种极简环境（推荐方案 A）**：

### 方案 A：极速免安装绿色工具链（Zig 0.13.0 + Ninja，合计 ~60 MB，首选推荐）
无需任何管理员权限、无需运行安装程序、不写入注册表，5 分钟内搭建完毕：

1. **下载免安装绿色包**：
   - 下载 Zig 0.13.0：[`zig-windows-x86_64-0.13.0.zip`](https://ziglang.org/download/0.13.0/zig-windows-x86_64-0.13.0.zip)（~50 MB）；
   - 下载 Ninja 构建器：[`ninja-win.zip`](https://github.com/ninja-build/ninja/releases/latest/download/ninja-win.zip)（~1 MB）；
2. **解压与就绪**：
   - 解压至任意纯英文目录，例如 `C:\dev\zig\` 与 `C:\dev\ninja\`；
   - 将上述路径加入当前 PowerShell 会话环境：
     ```powershell
     $env:PATH = "C:\dev\zig;C:\dev\ninja;" + $env:PATH
     ```
3. **验证工具链**：
   ```powershell
   zig version   # 应输出 0.13.0 (内置完整的 LLVM Clang 18 + LLD)
   ninja --version # 应输出 1.12+
   ```
4. **一键驱动项目编译**：
   CineRTX 原生内置了该工具链文件，直接调用：
   ```powershell
   cmake -S . -B build_zig -G Ninja -DCMAKE_TOOLCHAIN_FILE=cmake/toolchain-zig.cmake -DRTX_ZIG_EXECUTABLE=C:/dev/zig/zig.exe
   ```

---

### 方案 B：微软官方轻量 CLI 工具包（VS Build Tools，~1.5 GB，零 IDE 界面）
如果希望保持 100% 官方 MSVC 编译器特性但彻底剔除臃肿的 IDE：

1. 打开管理员 PowerShell，执行一键静默安装命令（仅安装 C++ 编译内核与 Win11 SDK，不装任何 IDE 图形界面）：
   ```powershell
   winget install Microsoft.VisualStudio.2022.BuildTools --override "--passive --wait --add Microsoft.VisualStudio.Workload.VCTools --includeRecommended"
   ```
2. 安装完成后，直接在终端启动：
   ```powershell
   & "C:\Program Files (x86)\Microsoft Visual Studio\2022\BuildTools\VC\Auxiliary\Build\vcvars64.bat"
   ```
   即刻获得 `cl.exe`、`link.exe`、`lib.exe` 与 Windows SDK 编译环境。

---

## 三、 FFmpeg 手术刀静态库编译与供应链闭环

构建系统中的 `RtxFfmpegStatic.cmake` 设有 450 KB 导入桩体积熔断器，执行工程师必须生成真正的静态库：

1. **获取官方源码**：
   下载并解压官方 [FFmpeg 7.1.1 源码包](https://ffmpeg.org/releases/ffmpeg-7.1.1.tar.xz) 至本地，例如 `C:\dev\ffmpeg-7.1.1`；
2. **执行手术刀裁剪编译**：
   在已挂载 MSVC（或 Clang/Zig）环境的 MSYS2 / Git Bash 终端中运行项目已有的构建脚本：
   ```bash
   ./tools/build_ffmpeg_static_msvc.sh C:/dev/ffmpeg-7.1.1 C:/dev/ffmpeg-static-dist
   ```
3. **核验证书与静态库物理大小**：
   ```powershell
   $libs = Get-ChildItem -Path "C:\dev\ffmpeg-static-dist\lib" -Filter "*.lib"
   foreach ($lib in $libs) {
       Write-Host "$($lib.Name): $($lib.Length) bytes"
   }
   # 断言: avformat.lib 与 avcodec.lib 必须大于 450,000 bytes (> 450 KB)!
   ```

---

## 四、 实机编译与全量门禁实测闭环流程

执行工程师必须在本地完成以下全套实机验证流程，记录完整的终端输出：

### 4.1 物理编译通过
```powershell
# 传入静态库目录配置
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=RelWithDebInfo

# 执行多核并行编译 (必须 0 错误、0 LNK2019、0 LNK2038 编译成功!)
cmake --build build --config RelWithDebInfo
```

### 4.2 PE 导入表与单 EXE 黄金体积包络审计（Gate 3.0）
```powershell
pwsh -File tools/audit_imports.ps1 -BuildDir build
```
* **准出合格标准**：
  1. 输出 `result: PASS`；
  2. 0 个 NVIDIA 专有库静态导入；
  3. **0 个 FFmpeg 动态库导入（`avcodec*.dll`、`avformat*.dll` 等严格为 0）**；
  4. 构建目录**零外挂媒体 DLL 散落**；
  5. `rtx-transcode-gui.exe` 体积稳定处于 **8.0 MB 至 12.0 MB** 黄金包络内。

### 4.3 137 项单元测试与 6 项 CTest 门禁实测全绿
```powershell
ctest --test-dir build --output-on-failure
```
* **准出合格标准**：
  - `rtx_unit_tests`：137/137 项测试通过率 100%；
  - 6 项 CTest 门禁全绿（包含内存流解复用自环测试、Flicks 24小时漂移测试、ABI 契约测试、视口自测）。

### 4.4 独立空白隔离目录实机转码跑通
```powershell
# 建立纯净沙盒隔离目录
New-Item -Path "C:\PUBLIC\[REDACTED_SANDBOX]" -ItemType Directory -Force | Out-Null
Copy-Item "build\rtx-transcode-gui.exe" "C:\PUBLIC\[REDACTED_SANDBOX]\"

# 剥离一切可能存在的 FFmpeg 环境变量
$env:PATH = ($env:PATH.Split(';') | Where-Object { $_ -notmatch "ffmpeg" }) -join ';'

# 在独立沙盒中双击运行，输入实际视频转码
Set-Location "C:\PUBLIC\[REDACTED_SANDBOX]"
.\rtx-transcode-gui.exe --input "C:\PUBLIC\[REDACTED_SAMPLE]" --output "output_4k.mp4" --backend nvidia
```
* **准出合格标准**：单文件 EXE 独立启动，100% 成功生成合法 4K MP4 视频，控制台与 UI 零环境缺失报错。

---

## 五、 最终合格交付物清单要求

请执行工程师完成上述工作后，重新打包交付：
1. **最终可运行代码补丁**：`CineRTX_phase3_verified.patch`；
2. **完工终审测试报告**：包含以下真实执行日志文本：
   - `tools/check_spdx.ps1` 输出日志；
   - `cmake --build` 编译完成输出（0 warning / 0 error）；
   - `tools/audit_imports.ps1` 绿灯日志（包含单 EXE 字节数）；
   - `ctest` 6 项门禁及 137 项单测 100% PASS 输出；
   - 独立隔离目录实机转码运行输出与生成的 JSON 遥测指标。

收到上述真实闭环物料后，委员会将正式签发阶段三（Phase 3 / v0.2.0）全量通过决议。
