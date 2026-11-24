**终审裁决：暂缓推送（HOLD git push）**——已完成对最新基线（Commit 40148dda8f，chore(release): purge scripts/, enforce CMake-native AuditPrivacy, and seal v0.4.0）的五大维度全仓库穿透扫描。核心 C++ 源码、生产二进制 GPLv3 隔离与根域目录结构均已达标，但检出 **2 项 P0 阻断推送缺陷**（推送即 100% 导致 GitHub Actions CI 崩溃 \+ 受控文档二次隐私泄密）与 **4 项 P1 高危漂移**，必须修复后再执行 git push origin main \--tags。

### **一、 P0 阻断推送项（必须先修，否则推送即出事故或泄密）**

> 1. **P0-1｜CI 流水线与 AuditImports.cmake 契约断裂（推送即 100% 红灯崩溃）**  
   * **违规位置**：[.github/workflows/ci.yml]([REDACTED_URL]) 第 73、118、168 行 vs [cmake/AuditImports.cmake]([REDACTED_URL]) 第 20–21 行。  
   * **物理事实**：Wave 12.3 将 AuditImports.cmake 重构为强制接收 \-DTARGET\_BIN=\<path\> \-DMODE=\<HEADLESS\|GUI\> 的双模 PE 审计脚本（缺 TARGET\_BIN 直接 FATAL\_ERROR），但 ci.yml 的全部 3 个作业（linux-release、windows-msvc-ninja、zig-windows）仍在调用旧接口 cmake \-DBUILD\_DIR=... \-P cmake/AuditImports.cmake。此外，Linux ELF 作业调用 Windows PE 解析器也会直接失败，且 CI 尚未接入 AuditTypes.cmake、AuditSloc.cmake 与 AuditPrivacy.cmake。  
   * **修复要求**：在 ci.yml 的顶层审计作业补齐 AuditTypes、AuditSloc、AuditPrivacy；在 Windows 作业中改为对 CineRTX.exe（-DMODE=GUI）与 rtx\_headless\_cli.exe（-DMODE=HEADLESS）分别传 \-DTARGET\_BIN=...；移除 Linux 作业中对 PE 解析器的错误调用（改用 ldd/readelf 或仅限 Windows 运行）。  
> 2. **P0-2｜受控文档隐私二次泄密与 AuditPrivacy.cmake 扫描盲区**  
   * **盲区 A（77 号文档自身字面泄密）**：[docs/phase12/77\_CineRTX Phase 12 根域scripts物理清剿与CMake原生隐私门禁终审裁决书.md]([REDACTED_URL]) 虽声称“零敏感词”，但第 52–54、103、112–113 行的内嵌代码块未做 string(CONCAT ...) 拆分，直接字面暴露了个人账户名与云盘目录特征（因正则含 \[/\\\\\] 字符类而绕过了 AuditPrivacy.cmake 自检）。  
   * **盲区 B（宿主机非 Users 盘符路径与测试机账号漏网）**：[cmake/AuditPrivacy.cmake]([REDACTED_URL])（L19–29）未覆盖 [REDACTED_PATH] 盘符路径与测试机用户名，导致以下文件仍有宿主机痕迹残留：  
     * [REDACTED_PATH] 仍残留在 [docs/phase12/07\_\*]([REDACTED_URL])、[11\_\*]([REDACTED_URL])、[20\_\*]([REDACTED_URL])、[28\_\*]([REDACTED_URL])、[32\_\*]([REDACTED_URL])、[38\_\*]([REDACTED_URL])、[39\_\*]([REDACTED_URL])、[43\_\*]([REDACTED_URL])、[44\_\*]([REDACTED_URL])、[56\_\*]([REDACTED_URL]) 及 [docs/phase11/32\_\*]([REDACTED_URL])。  
     * 测试机 SSH 账号 [REDACTED_HOST] 仍残留在 [docs/phase11/22\_\*]([REDACTED_URL]) 与 [docs/phase11/26\_\*]([REDACTED_URL]) 顶部“验收环境”行。  
   * **盲区 C（扫描范围缺口）**：AuditPrivacy.cmake（L32–60）未纳入 .github/workflows/\*.yml、build.bat、.gitmodules、\*.cu、\*.hlsl 与 webui/state.json。  
   * **修复要求**：同步用 AuditPrivacy.cmake 现行的 string(CONCAT ...) 实现替换 doc77.md 第 50–115 行代码块；在 AuditPrivacy.cmake 补入 [REDACTED_PATH]、测试机账号规则及上述文件后缀后，再跑一遍 cmake \-DFIX=ON \-P cmake/AuditPrivacy.cmake。

### **二、 P1 高危问题（违反设计哲学、项目约定或跨端契约漂移）**

| 编号 | 维度 | 精确位置与实证 | 修复建议 |
| :---- | :---- | :---- | :---- |
| **P1-1** | 设计哲学（离线确定性） | [CMakeLists.txt]([REDACTED_URL]) L320–352：配置期仍通过 curl 与 file(DOWNLOAD) 静默联网拉取 RIFE v4.25 ONNX，局部违背 HYG-P12-04“配置期零静默联网”约定。 | 将模型下载改为显式构建目标或仅在离线缓存缺失时提示，避免 cmake \--preset 阶段隐式外联。 |
| **P1-2** | 规范文档严重过期 | [docs/spec/compliance.md]([REDACTED_URL])（L47/51/93/109/128/148/176/178/192/208）、[architecture.md]([REDACTED_URL])、[gate1.md]([REDACTED_URL])、[gate3.md]([REDACTED_URL]) 仍指向已物理铲除的 tools/\*.ps1 / tools/build\_ffmpeg\_static\_\*.sh 及已拔除的隐藏环境变量 RTX\_NVIDIA\_DIR；[testing.md]([REDACTED_URL]) 仍列举已废弃的 test\_abi\_layout / rtx\_core.dll。 | 至少在推送前将 docs/spec/compliance.md 更新至 cmake/Audit\*.cmake、cmake/PackageRelease.cmake 与零环境变量现状，避免公开合规文档与仓库实态矛盾。 |
| **P1-3** | 三端 SSOT 契约漂移 | [webui/README.md]([REDACTED_URL])（L73–75、92、99–101）虽标 v0.4.0，却仍宣称支持已从 C++26 核心移除的 av1\_cpu（SVT-AV1）、hevc\_cpu（x265）与 crf；[webui/server.py]([REDACTED_URL])（L47、49）仍残留 av1\_cpu / hevc\_cpu 死键值。 | 同步移除 webui/README.md 与 webui/server.py 中已废弃的 CPU 软编描述和死条目，回归纯 GPU 20 参数 SSOT 契约。 |
| **P1-4** | 不存在的 Preset 命令 | [CMakePresets.json]([REDACTED_URL]) 仅定义了 windows-msvc-ninja（输出 .vs/build/cmake/windows-msvc-ninja），但 [cmake/PackageRelease.cmake]([REDACTED_URL]) L6 及 [doc74]([REDACTED_URL])、[doc77]([REDACTED_URL])（L233–238）、[doc78]([REDACTED_URL])（L43–51）均写了不存在的 windows-msvc-relwithdebinfo 与 build/windows-msvc-relwithdebinfo/RelWithDebInfo。 | 统一更正为 windows-msvc-ninja 与 .vs/build/cmake/windows-msvc-ninja，与 README.md / README\_zh.md 保持一致。 |

### **三、 P2 建议与备案项（不阻断发布，可顺手清理）**

* **P2-1（测试目标导入表洁癖）**：[CMakeLists.txt]([REDACTED_URL]) 第 717 行 target\_link\_libraries(rtx\_tests PRIVATE rtx\_ngx\_bridge) 使未分发的单测程序静态引用了 rtx\_ngx\_bridge.dll 导入库（生产产物 CineRTX.exe 与 rtx\_headless\_cli.exe **均未链接**，不构成 GPL 分发违约；后续可将单测也改为纯动态加载）。  
* **P2-2（Git 历史作者邮箱提示）**：.git/logs/refs/heads/main 显示前 4 次早期历史提交（e666586..32a396f，已存在于远端）含真实个人邮箱 [REDACTED_EMAIL]，第 5 次至最新第 53 次提交（含全部 Phase 12 提交）均已规范使用 GitHub noreply 邮箱，Phase 12 新增历史 **0 泄露**。  
* **P2-3（历史文档小瑕疵）**：[docs/phase11/34\_\*]([REDACTED_URL]) 含 \[http\://33\](http\://33)\_CineRTX... 失效相对链接；[docs/phase11/00\_CineRTX 演进方向调研.md]([REDACTED_URL]) 残留一处 \[span\_28\](start\_span)\[span\_28\](end\_span) 排版标记。  
* **P2-4（README 补齐第五门禁）**：[README.md]([REDACTED_URL])（L94–99）与 [README\_zh.md]([REDACTED_URL])（L94–98）建议补列 cmake \-P cmake/AuditPrivacy.cmake。

### **四、 已确认 100% 绿灯通过的核心维度**

* **GPLv3 \+ Plugin-Exception 生产二进制物理隔离**：[CMakeLists.txt]([REDACTED_URL])（L398、L510）确证 nvsdk\_ngx\_s.lib 仅被独立 SHARED 库 rtx\_ngx\_bridge.dll 私有链接，主程序 CineRTX.exe 与 rtx\_headless\_cli.exe 零专有静态符号；根域 LICENSE、[COPYING.EXCEPTION]([REDACTED_URL])、LICENSE-MIT、LICENSE-APACHE 四法务文件齐全；[cmake/AuditSpdx.cmake]([REDACTED_URL]) 全覆盖且含二进制/模型 blob 拦截。  
* **现代 C++26 源码卫生与硬门禁**：全仓 include/ 与 src/ 检索 std::getenv、commctrl.h、\#pragma comment(lib、RTX\_EMBEDDED\_NGX\_BRIDGE、rtx\_ngx\_bridge\_static 全部 **0 命中**（所有历史备份已隔离在仓外 \_CineRTX\_bak\_archive/）；[AuditTypes.cmake]([REDACTED_URL])（\<= 35 硬上限）与 [AuditSloc.cmake]([REDACTED_URL])（Core \<= 23,000）均已切换为默认 FATAL\_ERROR 硬阻断。  
* **仓库目录结构与 \[DEC-002\] 纯洁度**：根目录仅保留合法受控目录（.git、.github、cmake、docs、include、src、tests、third\_party、webui），scripts/、tools/、schema/ 均已物理归零；仓内无任何 .bak/.tmp/.orig/.pyc/(1) 冲突副本；[webui/state.json]([REDACTED_URL]) 为 100% 干净默认模板（零本地路径或个人任务记录）；[.gitignore]([REDACTED_URL]) 完整覆盖构建目录、二进制与 Python 缓存。

### **五、 推送前最小收口步骤**

> 1. 修复 .github/workflows/ci.yml 的 AuditImports.cmake 参数并补入 AuditTypes / AuditSloc / AuditPrivacy。  
> 2. 净化 docs/phase12/77\_\* 的代码块字面量，扩展 cmake/AuditPrivacy.cmake 覆盖 [REDACTED_PATH]、测试机账号及 .yml/.bat/.cu/.hlsl/.json，执行 cmake \-DFIX=ON \-P cmake/AuditPrivacy.cmake 与 cmake \-P cmake/AuditPrivacy.cmake。  
> 3. 同步更新 docs/spec/compliance.md、webui/README.md、webui/server.py（L47/49）及 PackageRelease.cmake L6 的 preset 名称。  
> 4. 本地复跑五重原生门禁与单测全部绿灯后，再执行最终 git commit、git tag \-f \-a v0.4.0 与 git push origin main \--tags。

需要我直接帮你生成上述 P0 \+ P1 的精确补丁（或出具 79 号终审收口施工单落盘至 docs/phase12/）吗？