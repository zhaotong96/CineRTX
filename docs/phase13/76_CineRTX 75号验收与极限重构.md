# **CineRTX Phase 13 74号返工终审验收报告与单EXE极限Modern C++重构演进方案**

基于宿主机实测结果与项目架构宪法，74\_CineRTX Phase 13 极限重构与演进返工汇报.md（下称 74\_）**不予验收、正式驳回**，工作区源码必须立即回退至已封板基线 Commit 0beda87（MSVC /W4 /WX 零错误零警告、rtx\_tests.exe 157/157 PASS、AuditPrivacy=0、AuditSpdx=PASS、Core SLOC=12,496）1。74\_ 在宿主机直接引发编译失败、回退既有的 WebUI/D3D12 预览触发注入、引入 2 处 AuditPrivacy 隐私违规，并在未经架构裁决前单方面改写构建门禁与编译选项1。需要说明的是，74\_ 全量文本未出现在当前可核验的归档文件中2，本终审报告严格依据 75\_CineRTX Phase 13 74号返工验收阻塞与防覆写协同治理委托书.md 提供的宿主机实测事实以及 01\_、61\_、63\_ 既有基线档案作出裁决，不作无依据推断1。  
针对 75\_ 提出的路线分歧，本裁决正式**废止早期阶段及 38\_ 终审中妥协保留多 EXE 的条款**：rtx\_headless\_cli.exe 没有任何存在的借口，必须从构建目标、发行包与测试边界中彻底拔除，全项目收敛为单一 /SUBSYSTEM:WINDOWS 可执行文件 CineRTX.exe1。与此同时，74\_ 擅自删除 CINERTX\_ENABLE\_GPL\_CODECS 并默认开启 FFmpeg \--enable-gpl 的做法同样予以否定——但并非退回保留隐性开关，而是以**单一路线彻底清除 GPL 编码组件与开关本身**：FFmpeg 严格锁死为纯 LGPL 构建，CPU 软编码统一收敛至 BSD 许可系的 SVT-AV1，在零额外编译开关的前提下消除与闭源 rtx\_ngx\_bridge.dll 的许可证传染冲突1。  
在二进制卫生与源码架构层面，后续演进将以 MSVC /MT 静态链接为唯一工具链基线，制定从当前 {gdi32.dll, user32.dll, bcrypt.dll, kernel32.dll, kernelbase.dll} 5 项静态导入向 \<= 3 \-\> \<= 2 \-\> \== 1（仅保留 kernel32.dll）逼近的精确剥离路径；在代码层面全面推行 C++20/23/26 标准库替代运动，拔除一切可由标准库设施替代的自定义类、冗余防御与全局状态，并将 74\_ 中确具工程价值的 MergeConcat 异构路由增量以最小函数级补丁在 0beda87 上复现落地1。

## **一、74号返工验收终审裁决与防覆写协同治理**

### **1.1 宿主机实测阻塞定位**

74\_ 暴露的并非孤立编码瑕疵，而是工程端在 50\_、54\_、74\_ 连续三次出现的“基于旧快照整文件覆写摧毁已封板代码”事故，并叠加了未经授权篡改合规门禁的越权行为1。宿主机实测的四项客观阻塞均已构成不可放行条件1。

| 阻塞维度 | 宿主机实测证据 | 根因定性 | 处置裁决 |
| :---- | :---- | :---- | :---- |
| 编译直接中断 | .\\build.bat 退出码 1，src/core/hw\_transcode\_stages.cpp(555) 报 C2039: 'consumeNvofSceneCut': is not a member of 'rtx::NgxRemasterBridge'1 | 工程师端基于旧快照整文件覆写 include/rtx/ngx\_remaster.h 与 src/core/ngx\_remaster.cpp，静默删除 0beda87 已落地的 NVOFA 切镜消费接口1 | 立即将源码全量回退至 0beda87，禁止任何整文件替换1 |
| 既有基线功能回退 | src/core/job\_queue.cpp:L359 删掉 isPreviewEnabled() 对 opts.preview 的回调注入1 | 旧快照覆写导致 31\_ 建立的 WebUI 预览落盘与 D3D12 预览环触发开关再度断链1 | 在 0beda87 核验恢复预览注入并补充回归断言1 |
| AuditPrivacy 隐私门禁失败 | 检出 2 处违规：docs/phase13/73\_...md:24 泄露 Windows 用户路径，docs/phase13/74\_...md:10 泄露云盘明文名称1 | 文档提交前未执行隐私正则扫描门禁1 | 全部替换为 \[REDACTED\_\*\] 脱敏占位并固化前置阻断4 |
| 未裁决越权改门禁与合规风险 | 擅自删去 rtx\_headless\_cli、将 cmake/AuditImports.cmake 放宽至 \<= 5、删除 RtxFfmpegStatic.cmake Zig 分支与 CINERTX\_ENABLE\_GPL\_CODECS 并默认开启 \--enable-gpl1 | 混淆重构方向与单边绕过门禁，且放任 kernelbase.dll 违宪渗入与 GPL 许可证污染1 | 驳回放宽 IAT 与开启 \--enable-gpl 的改动；单 EXE 与零开关按本裁决执行1 |

### **1.2 三项委托诉求的权威批复**

> 1. **诉求 1（基线保护与止损）**：即刻将工作区源码完整回退至干净基线 0beda871。自本裁决签发日起，严禁接收任何整文件覆盖式交付；所有代码修改必须基于当前封板基线生成最小函数级上下文补丁（Unified Diff），由协调负责人通过补丁检查后再行合入。  
> 2. **诉求 2（路线冲突唯一定调）**：正式废止 38\_ 中保留 rtx\_headless\_cli 与 CINERTX\_ENABLE\_GPL\_CODECS=OFF 开关的既有条款，确立**单 EXE（拔除 rtx\_headless\_cli）、零编译开关、纯 LGPL FFmpeg \+ SVT-AV1、IAT 逐波收紧与极限 Modern C++ 标准库替代**为 Phase 13 唯一施工路线，同步依据本方案修订 cmake/AuditImports.cmake 与 cmake/AuditTypes.cmake1。  
> 3. **诉求 3（Wave B 实施分工）**：74\_ 中具备价值的功能增量——areConcatInputsHomogeneous() 异构输入判别与 MergeConcat 异构路由至 runNvidiaRemasterRenderGraph() 及其单元测试——直接由协调负责人在宿主机基于 0beda87 按最小增量复现并完成真机验证，不再向外部重派大跨度返工单，从源头上杜绝旧快照回流1。

### **1.3 防覆写机械化护栏**

仅靠人工审阅无法拦截连续三次的旧快照回流，必须在构建流水线植入三项刚性卡点：

> 1. **受保护契约符号守卫**：在 CMake 配置或测试前置阶段对 include/rtx/ngx\_remaster.h 与核心队列接口执行关键签名存在性检查（如确认 consumeNvofSceneCut 与预览状态注入逻辑未丢失），任何静默删减直接阻断编译。  
> 2. **门禁脚本独立变更锁**：将 cmake/Audit\*.cmake 与 cmake/RtxFfmpegStatic.cmake 设为受保护基线边界，功能波次补丁严禁顺带放宽门禁指标；任何门禁阈值调整必须绑定架构终审批准的独立提交。  
> 3. **构建与审计强串行**：build.bat 入口前置执行 AuditPrivacy 与 AuditSpdx，构建尾端强制串联 AuditImports、AuditTypes 与 rtx\_tests.exe，任一门禁非零退出即判定整体构建失败1。

## **二、单EXE宪法重立与Win32混合入口工程方案**

### **2.1 彻底拔除 rtx\_headless\_cli.exe**

保留 rtx\_headless\_cli.exe 的历史原因是为了将无头 CLI 进程从 GUI 消息循环中剥离，并在 AuditImports.cmake 中维持 HEADLESS \== 2 的独立审计样本1。这种人为分裂直接违背了规格文档早期确立的“禁止多 EXE”铁律，同时导致 CMakeLists.txt、PackageRelease.cmake 及帮助文档（src/cli/main.cpp::printHelp() 与 param\_schema.cpp::formatCliHelp() 长期漂移）产生双份维护冗余4。  
物理拔除工作零妥协执行：CMakeLists.txt 与打包脚本中彻底删除 rtx\_headless\_cli 目标，独立命令行入口收编为 CineRTX.exe 启动分派逻辑中的无头模式分支，命令行帮助文本唯一收口至 param\_schema.cpp4。除开发期测试宿主 rtx\_tests.exe 外，最终交付目录仅允许存在单一可执行文件 CineRTX.exe 与专有隔离动态库 rtx\_ngx\_bridge.dll（及推理引擎权重文件）3。

### **2.2 /SUBSYSTEM:WINDOWS 混合入口的 Windows 内核机制与陷阱规避**

实现单 EXE 兼顾桌面 GUI 与终端 CLI 时，若采用 /SUBSYSTEM:CONSOLE 并在启动 GUI 前调用 FreeConsole()，在 Windows 11 桌面双击启动时终端窗口仍会出现短暂闪烁后销毁的恶劣体验7。因此，CineRTX.exe 必须严格锁死为 **/SUBSYSTEM:WINDOWS**（默认不分配控制台），仅当解析到命令行参数进入无头模式时按需绑定父进程控制台16。  
然而，直接无脑调用 AttachConsole(ATTACH\_PARENT\_PROCESS) 配合 freopen\_s("CONOUT\$", ...) 存在两个隐蔽的 Windows Shell 级深坑，必须依据句柄类型精确避让：

> 1. **cmd.exe 文件重定向覆写陷阱**：在 cmd.exe 中执行 CineRTX.exe \--help \> out.txt 时，cmd.exe 会在调用 CreateProcessW 前临时替换自身的标准输出句柄，使子进程直接继承到一个 GetFileType() 返回 FILE\_TYPE\_DISK 的文件句柄6。此时若未经检测直接调用 AttachConsole(ATTACH\_PARENT\_PROCESS)，系统会覆写子进程继承的标准输出句柄6，而再执行 freopen\_s("CONOUT\$", ...) 更会把原本应当写入 out.txt 的文本强制打印回控制台屏幕20。因此，必须先通过 GetStdHandle 与 GetFileType 判别 FILE\_TYPE\_DISK / FILE\_TYPE\_PIPE 状态，在调用 AttachConsole 前缓存已重定向的句柄并在附加后用 SetStdHandle 予以恢复，且严禁对已重定向的流执行 freopen\_s20。  
> 2. **PowerShell \> 重定向局限（实证不可绕过行为）**：微软技术社区与 PowerShell 问题追踪记录明确指出，在 PowerShell 5.1 与 PowerShell 7（pwsh.exe）中执行 app.exe \> log.txt 时，PowerShell 通过 STARTUPINFO（STARTF\_USESTDHANDLES）传递匿名管道句柄（FILE\_TYPE\_PIPE），但针对 /SUBSYSTEM:WINDOWS 进程，交互式 PowerShell 不会等待 GUI 子系统进程退出并持续读取管道，而是可能提前关闭读取端，导致写端遭遇 STATUS\_PIPE\_CLOSING 或产生空文件6。这是一个宿主机 Shell 与 Windows 子系统契约决定的限制，不可通过应用层技巧“静默修复”；在 CineRTX 测试脚本与 PowerShell 使用手册中必须明确规定：PowerShell 下捕获命令行输出应使用 CineRTX.exe \--help \| Out-File log.txt 或 Start-Process \-Wait \-RedirectStandardOutput22。  
> 3. **无控制台双击启动与中文编码**：当用户从资源管理器直接双击无参数启动 GUI 时，GetStdHandle 为 NULL，入口函数完全跳过控制台附加逻辑，零句柄浪费、零窗口闪烁7；当确实附加到终端输出 UTF-8 文本时，须同步调用 SetConsoleOutputCP(CP\_UTF8)，避免控制台按系统默认 OEM 代码页解码输出导致中文乱码21。

### **2.3 零自定义类的 Modern C++ 入口控制台绑定实现**

该逻辑纯粹服务于进程启动时的无状态环境接线，禁止为此设计任何 ConsoleGuard 或 CliHost 类，以一个内联自由函数与 Lambda 表达式即可完成全部职责：

C++  
\#**include** \<windows.h\>  
\#**include** \<cstdio\>

namespace rtx {

inline void bind\_parent\_console\_if\_needed() noexcept {  
    const auto is\_redirected \= \[\](DWORD std\_id) noexcept {  
        HANDLE h \= ::GetStdHandle(std\_id);  
        if (h \== nullptr \|\| h \== INVALID\_HANDLE\_VALUE) return false;  
        const DWORD type \= ::GetFileType(h);  
        return type \== FILE\_TYPE\_DISK \|\| type \== FILE\_TYPE\_PIPE;  
    };

    const HANDLE saved\_out \= ::GetStdHandle(STD\_OUTPUT\_HANDLE);  
    const HANDLE saved\_err \= ::GetStdHandle(STD\_ERROR\_HANDLE);  
    const bool out\_redir \= is\_redirected(STD\_OUTPUT\_HANDLE);  
    const bool err\_redir \= is\_redirected(STD\_ERROR\_HANDLE);  
    if (out\_redir && err\_redir) return;

    if (::AttachConsole(ATTACH\_PARENT\_PROCESS)) {  
        if (out\_redir) ::SetStdHandle(STD\_OUTPUT\_HANDLE, saved\_out);  
        if (err\_redir) ::SetStdHandle(STD\_ERROR\_HANDLE, saved\_err);  
        FILE\* dummy \= nullptr;  
        if (\!out\_redir) (void)::freopen\_s(\&dummy, "CONOUT\$", "w", stdout);  
        if (\!err\_redir) (void)::freopen\_s(\&dummy, "CONOUT\$", "w", stderr);  
        ::SetConsoleOutputCP(CP\_UTF8);  
    }  
}

} // namespace rtx

## **三、静态MT链接与PE导入表极净治理路径**

### **3.1 五大静态导入 DLL 的法医解剖与剥离策略**

在 01\_ 归档的 \[DEC-018\] 基线中，GUI 静态导入白名单原本锁定 \<= 4（kernel32, user32, gdi32, bcrypt），而 kernelbase 已明确列于 10 项静态导入黑名单内4。当前工作区出现的 {gdi32.dll, user32.dll, bcrypt.dll, kernel32.dll, kernelbase.dll} 5 项静态导入（这也是 74\_ 被迫将 AuditImports 放宽至 \<= 5 的原因）1，实质上标志着黑名单防线已被击穿。以下逐项审查这 5 项 DLL 的物理成因与剥离路线：

| 静态 DLL | 典型引入机制与排查方法 | 是否可移除 | 分步剥离实施方案 |
| :---- | :---- | :---- | :---- |
| kernel32.dll | Win32 进程装载基座、LoadLibraryExW / GetProcAddress 与基础线程文件句柄 | 不可移除（OS 底线） | 永久保留，作为全项目静态导入表理论极限（Static IAT \== 1） |
| bcrypt.dll | FFmpeg configure 脚本默认自动探测 BCryptGenRandom \-lbcrypt 并开启 HAVE\_BCRYPT (libavutil/random\_seed.c)14；或宿主源码直接链接 bcrypt.lib | **完全可移除** | **Wave 0 立即拔除**：在 cmake/RtxFfmpegStatic.cmake 显式传入 \--disable-bcrypt，确认生成的 config.h 满足 \#define HAVE\_BCRYPT 026；核查 dumpbin /imports 后清除主工程对 bcrypt.lib 的直接链接，若某业务确实需要加密级随机数（而非普通非加密 PRNG），则经由 sys\_dynload 在运行时惰性解析 BCryptGenRandom |
| kernelbase.dll | MSVC 静态运行时 /MT（libcpmt.lib(atomic\_wait.obj)）中的 \_\_std\_atomic\_wait\_direct 调用 WaitOnAddress / WakeByAddressSingle 拉入 Synchronization.lib13；或代码/底层库直调解析至 KERNELBASE.dll 的 Win10+ API 与调试桩30 | 完全可移除（且本属 \[DEC-018\] 黑名单）4 | **Wave 0 立即拔除**：在宿主机执行 dumpbin /imports CineRTX.exe 锁定 KERNELBASE.dll 名下的具体导入符号；若命中 WaitOnAddress/WakeByAddressSingle，将源码中触发该路径的 C++20 std::atomic::wait / 计数信号量替换为锚定基础 OS 原语的 std::mutex \+ std::condition\_variable 并剔除 Synchronization.lib 链接；若命中特定 Win10+ API（如线程命名），一律移入 sys\_dynload 动态解析 |
| gdi32.dll | 遗留 Win32 STATIC/EDIT/COMBOBOX/LISTBOX 控件、GDI 字体创建与位块传输4 | **完全可移除** | **Wave D 拔除**：按 01\_ 选定的合规路径，通过 sys\_dynload 动态加载 d2d1.dll \+ dwrite.dll \+ dcomp.dll 接管全部自绘控件与文本排版4，清理全部 Gdi32.lib 符号 |
| user32.dll | Win32 窗口注册、消息循环调度、DPI 感知与系统剪贴板 | **可从静态 IAT 剥离（GUI 期动态加载）** | **Wave B/C 保留，Wave D 下沉**：在 GDI 退出历史舞台后，剩余窗口生命周期与消息循环符号仅存在于 GUI 模式；将其封装进无状态 sys\_dynload("user32.dll") 动态解析表，仅在确认进入桌面 GUI 分支后加载 |

### **3.2 bcrypt.dll 与 kernelbase.dll 的隐式渗漏根治细节**

对于 bcrypt.dll，工程中无加密场景却频繁出现该依赖，几乎必然来自静态 FFmpeg 的自动探测链路。FFmpeg configure 在 Windows 构建环境默认执行 check\_lib bcrypt "windows.h bcrypt.h" BCryptGenRandom \-lbcrypt24；一旦开启，libavutil/random\_seed.c 便通过 \#if HAVE\_BCRYPT 静态绑定 BCryptGenRandom14。在构建静态 FFmpeg 时显式关闭该项并确认 HAVE\_BCRYPT \== 0 即可切断 FFmpeg 侧引入26。在主工程侧，若仍存在 std::random\_device 或自定义随机生成逻辑引发的依赖，应使用 dumpbin /imports 逐一核实；非安全用途（如临时文件名碰撞规避、测试序列生成）改用标准 std::mt19937\_64 即可，必须调用密码学随机源处统一走 sys\_dynload。  
对于 kernelbase.dll，需要高度警惕 MSVC /MT 静态 STL 引入 C++20 等待原语的副作用。公开的 MSVC STL 源码与链接记录证实，libcpmt.lib(atomic\_wait.obj) 在 atomic\_wait.cpp 中直接调用 WaitOnAddress 与 WakeByAddressSingle28，而该组函数需要链接 Synchronization.lib13，在部分 Windows SDK / API Set 链接配置下会直接表现为静态导入 KERNELBASE.dll（或 api-ms-win-core-synch-\* 转发）。因此，在追求极端 PE 导入表净空的单二进制环境下，不可盲目使用 std::atomic::wait 或 std::counting\_semaphore 触发 Synchronization.lib，而应先以 dumpbin /imports 实证定位 KERNELBASE.dll 符号清单，再定点替换为不引入该静态库的并发原语或动态解析。

### **3.3 单 EXE 架构下的渐进式 IAT 门禁标准**

废除 rtx\_headless\_cli.exe 后，cmake/AuditImports.cmake 废除 GUI 与 HEADLESS 的双口径检查，对唯一宿主产物 CineRTX.exe 实施随演进波次递增严格度的硬门禁：

> 1. **Wave 0 / Wave B 门禁**：静态导入物理 DLL 严格 **\<= 3**，白名单限定 {kernel32.dll, user32.dll, gdi32.dll}；bcrypt.dll 与 kernelbase.dll 零容忍清零。  
> 2. **Wave D 门禁**：D2D1/DWrite 全面替代 GDI 后强制剥离 gdi32.dll，静态 IAT 降至 **\<= 2**；随后通过 sys\_dynload 收编窗口消息函数，最终实现 **Static IAT \== 1（仅静态链接 kernel32.dll）**。在此终极形态下，CineRTX.exe 既满足单二进制交付铁律，又在运行命令行无头任务或自动化测试时做到进程地址空间零加载 user32.dll 与 win32u.sys，完美实现无头与桌面双态零污染统一。

## **四、极限Modern C++23和26重构与自定义类存废裁决**

### **4.1 统一工具链基线与 C++23/26 特性采纳边界**

当前 build.bat:68 硬编码走 cmake/toolchain-zig.cmake，而项目文档与发布预设主推 windows-msvc-ninja4，这种双工具链分裂必须终止。全项目 C++ 编译工具链唯一锚定 **MSVC \+ Ninja \+ /MT \+ /W4 /WX**，清除 build.bat 中的 Zig C++ 交叉预设。至于 cmake/RtxFfmpegStatic.cmake 中的 Zig C 编译器分支1，须先在宿主机验证 MSVC/clang-cl 能否独立无误编出所需纯 C FFmpeg 静态归档；若宿主机已具备稳定单一工具链编译能力，则同步移除该分支以实现彻底的工具链合一，避免在未实测宿主环境前盲删导致第三方 C 库断编。  
在语言标准版本方面，根据 Microsoft 官方 Visual C++ 版本演进记录：Visual Studio 2022 17.12 至 17.14 已支持 C++23 区间格式化、std::print 示例、static operator()、static operator\[\] 与 if consteval（需 /std:c++latest 或 /std:c++23preview）11；Visual Studio 2026（v18.0 / MSVC Build Tools v14.50–v14.52）已实现完整 C++23 生产就绪（/std:c++23）及 auto(x) 等语言特性12；ISO C++26 标准本身已于 2026 年 3 月完成定稿，但相关库与工具链仍在合入追赶中36。同时，公开归档的早期 msvc-170 合规对照文档中缺少对 std::expected、std::mdspan、std::move\_only\_function、std::flat\_map、std::inplace\_vector、std::function\_ref、std::copyable\_function 的逐版本支持明细表33。因此，工程采纳遵循一条务实且不留兼容宏的原则：**基于宿主机实际安装的最高 MSVC 版本（若为 14.52+ 则可稳定启用 /std:c++23，需要 C++26 特性则统一使用 /std:c++latest），先对 std::expected、std::move\_only\_function、std::jthread、std::vformat 等特性及实验性 C++26 容器做一次宿主机最小编译探针核验，通过即全仓无条件使用，严禁在源码中堆砌 \#ifdef 版本兼容双分支**。

### **4.2 标准库替代清剿矩阵：极限消除冗余自定义类**

0beda87 当前在 include/ 与 src/ 中保有 34/35 个具名 struct/class1。以极致的 Modern C++ 洁癖审视，其中大量资源包装类、空壳策略类与自造轮子均属不可接受的冗余。下表列出所有基于既有源码文档可直接识别的可替代对象与标准库重构方案：

| 被拔除或否决的对象 | 既有冗余本质 | Modern C++20/23/26 标准库替代方案 | 架构与洁癖收益 |
| :---- | :---- | :---- | :---- |
| UiWidgetNode（原 38\_ 第 35 号配额）1 | 试图引入 OOP 保留模式控件节点树，撞穿 35/35 配额并制造状态同步包袱1 | 采用纯数据表驱动或即时绘制模式：控件配置为静态只读表 \+ std::span，交互状态存放于扁平数组，绘制与命中检测全改为无状态自由函数 | 省去 1 个自定义类（+0 增量），消除整棵 UI 树的同步复杂度 |
| ShotGroupSegmenter (hw\_transcode\_internal.h:242)4 | 仅在初始化调用一次 resetShotSegmenter 后全程未调用的幽灵死类4 | 物理删除类定义；改用无状态 GPU/CPU 自由函数计算 8x8 分块 SAD 与亮度直方图/方差，状态仅为一个局部紧凑数值阵列 | 直接减少 1 个具名类型，消除死代码并填补 NVOFA 切镜空白4 |
| 各类自定义 RAII 句柄包装类（FFmpeg 上下文、CUDA 内存/流、Win32 文件、HMODULE、COM 指针） | 重复编写析构函数与移动语义五件套（Rule-of-5） | 全面采用 C++20 非求值上下文 Stateless Lambda 删除器配合 std::unique\_ptr\<T, decltype(\[\](T\* p) noexcept { ... })\> | 彻底消灭资源包装类，零运行期开销强制实现 Rule-of-Zero |
| 自定义 Rational / 分数时间基结构（docs/TODO \#1）4 | 为修复 NTSC 1001 帧率时间基漂移而重复造轮子 | 直接使用 \<chrono\> 编译期有理数时钟：std::chrono::duration\<int64\_t, std::ratio\<1001, 24000\>\> / std::ratio\<1001, 60000\> 配合 duration\_cast 与标准分数参数 | 零新增自定义类型，标准库原生保证亚微秒级零累积误差 |
| 自定义错误返回值包装 / 状态结构体 | 破坏统一错误处理语义，增加调用方心智负担 | 统一替换为 C++23 std::expected\<T, std::string\> 或 std::expected\<T, uint32\_t\>，以 .and\_then() / .transform() 串联失败传递 | 消除错误包装类，以强类型迫使调用方显式处理失败路径 |
| WasapiAudioOutput 与潜在 NullAudioSink 测试桩 | 缺少静音输出通道导致单测直敲物理声卡发出 440Hz 蜂鸣4 | 使用 std::variant\<std::monostate, WasapiHandle\>：单测注入 std::monostate（纯时间推进不打开硬件声卡），正式播放持有 RAII WasapiHandle | 不新增任何虚基类或 Mock 类，根治单测 440Hz 蜂鸣4 |
| CpuVideoEncoder 与 Pipeline 算子继承体系4 | 虚函数表分发与抽象接口类违背 KISS 原则 | 算子退化为 enum class \+ 无状态自由函数/函数指针表（或 std::move\_only\_function）；SVT-AV1 退化为 std::unique\_ptr 上下文句柄与自由编码函数 | 彻底拔除虚继承与多态样板代码 |
| 自定义线程封装与手动退出标志类 | 重复封装线程生命周期与条件等待 | 全面采用 C++20 std::jthread \+ std::stop\_token / std::stop\_source \+ std::condition\_variable\_any | 析构自动请求停止并汇合，彻底消除漏 join 或竞态退出漏洞 |
| RifeAspectBucket / 测试局部重复驱动封装3 | 曾被误当作布尔值取反且测试侧重复封装底层驱动调用3 | RifeAspectBucket 严格收敛为强类型 enum class；测试侧动态函数加载封装压缩至单测文件内部局部匿名态，不进入 include/ 或 src/ 生产目录 | 保持 rtx\_tests.exe 纯 C ABI 隔离且不占用任何核心类型预算3 |

### **4.3 保留自定义类的显式必要性白名单**

由于现有归档文档并未逐一枚举 0beda87 全部 34 个具名类型的源码定义1，本方案不凭空臆测一个具体的剩余数量上限，而是要求在 Wave 0 由协调负责人导出 AuditTypes.cmake 扫描出的完整 34 项清单，逐个执行上述标准库替代审讯。**任何最终获准保留在 include/ 或 src/ 中的自定义 struct/class，必须同时满足“不可用标准库零成本表达”且在头文件注释中显式归属以下三类白名单之一，否则一律清退**：

> 1. **跨动态库 C ABI 契约 POD（extern "C"）**：例如 RtxNgxBridgeConfig、RtxRifeInitConfig 及不透明指针标签 RtxNgxBridge5。存在必要性：std::string、std::variant、std::tuple 均不具备跨编译单元/跨 CRT 边界（CineRTX.exe 与专有 rtx\_ngx\_bridge.dll3）的稳定 C ABI 内存布局，必须使用带 struct\_size 校验的标准布局结构体；同时必须拔除类似 RtxNgxBridgeConfig::reserved\[0\] \= 1u 的隐晦占位魔法开关5，所有真实生效字段必须显式命名，无用填充数组彻底清除。  
> 2. **GPU 硬件寄存器对齐常量块（HLSL cbuffer / CUDA 核参数）**：例如 alignas(16) 的色调映射参数聚合体。存在必要性：D3D12 根常量缓冲区与 CUDA 核函数接口强制要求严格的 16 字节硬件对齐布局，标准库通用容器无法表达该硬件偏移契约。  
> 3. **跨模块共享的 Rule-of-Zero 扁平领域状态聚合体（超过 3 个异构命名字段）**：例如 TranscodeProfile、EncoderJob、PlaybackSessionState、PreviewViewport 纯资源/状态句柄包、NgxRemasterBridge 纯函数指针表。存在必要性：当队列、视口、WebUI 与调度器需要共享带有明确语义名称的多个异构状态时，强行堆砌 std::tuple\<...\> 会使调用端退化为丧失语义的 std::get(state)，严重增加使用者心智负担并违背 KISS 原则；将其限制为**零自定义构造/析构/拷贝函数、零虚函数、全部资源成员均为标准库类型或** std::unique\_ptr **的纯聚合体**，是唯一符合 Modern C++ 工程理性的保留例外。

### **4.4 奥卡姆剃刀项目卫生清剿清单**

依据 01\_ 对历史仓库的只读取证4，以下全部隐藏开关、冗余分支、双源漂移与全局可变状态必须在 Wave 0 物理清零：

> 1. **清除所有死环境变量、幽灵开关与无用字段**：物理删除 src/shim/loader/nv\_encoder.cpp:241, 277 中悬空的 uhq\_env \= nullptr 与 bframes\_env \= nullptr 死分支4；删除 CMakeLists.txt 的 CINERTX\_ENABLE\_GPL\_CODECS 编译开关1；审计并拔除 PlaybackOptions 与 TranscodeProfile 中所有未接入实际逻辑的预留字段4。  
> 2. **清除全局可变状态与跨层悬空单例**：物理删除 src/core/ngx\_remaster.cpp:105 的 activePreviewMonitor() 全局原子单例指针，改为随任务上下文显式传入视图句柄4；物理删除 src/gui/workstation\_canvas.h:79-88 未接入管线的静态全局 active\_vsr\_target()，改为播放器直读视口实际尺寸4。  
> 3. **清除双源帮助与双套硬编码字典**：物理删除 src/cli/main.cpp:75-116 的硬编码 printHelp()，唯一以 param\_schema.cpp::formatCliHelp() 输出4；物理删除 src/gui/workstation\_canvas.cpp:206-241 的私藏 chromeText() 表与废弃 OptCodecH264 词条，收敛 Win32 与 WebUI (webui/app.js:21-330) 至单一词条源4。  
> 4. **清除隐性算子覆写与空壳假状态**：物理删除 src/core/hw\_transcode\_stages.cpp:92-95 在放大场景悄悄无视用户选项强切 fg-first 的隐性篡改代码4；物理删除 workstation\_canvas.cpp:1021-1033 在 playbackSession \== nullptr 时仍翻转按钮状态的空壳假播放逻辑4；物理删除 rtx\_gui.cpp:312-323 点击开始转码时以右侧检查器覆盖全队列待办任务独立参数的破坏性循环4。  
> 5. **消除告警掩蔽与重复构建规则**：移除 src/core/wasapi\_audio\_output.cpp:74 的 \#pragma warning(disable : 4324\)，通过将对齐敏感成员独立堆分配或对齐容器重构从根源消除填充告警4；合并 CMakeLists.txt:200-244 重复的配置期/构建期 PTX 规则4；严禁 rtx\_tests.exe 绕过库边界直接塞入核心内部 .cpp4，且任何对 rife\_pack.cu 的修改必须同步校验 rife\_pack.ptx 比特级等价性（如 testp.finite.f32 与 full\_range \* 255.0f 补齐）方准提交3。

## **五、开源合规架构与固定MP4输出兼容性矩阵**

### **5.1 纯 LGPL FFmpeg 与** SVT-AV1 **消除 GPL 冲突**

74\_ 擅自删除 CINERTX\_ENABLE\_GPL\_CODECS 并开启 \--enable-gpl 构成了直接的许可证违约风险1。CineRTX 主程序许可证为 GPL-3.0-or-later WITH Plugin-Exception，其设计初衷正是通过额外许可条款允许主程序动态边界调用闭源的 rtx\_ngx\_bridge.dll（封装 TensorRT nvinfer\_10 与 NGX SDK）3。然而，一旦在 FFmpeg 构建中传入 \--enable-gpl（如捆绑默认 GPLv2+/GPLv2 的 libx265 或 GPL 滤镜），受 GPL 覆盖的第三方代码将整体适用普通 GPL 条款，而 FFmpeg 上游及 x265 版权方（MulticoreWare 与社区贡献者）并未对 CineRTX 授予 Plugin-Exception 豁免8。将无额外豁免权限的 GPL 代码链入与专有动态库互通的主程序中，在法律上极易触发 GPL 与专有组件隔离条款冲突8。  
遵循奥卡姆剃刀原则，最优解不是在构建系统中保留一个令人困惑的 CINERTX\_ENABLE\_GPL\_CODECS 开关，而是**彻底砍除** libx265 **与** \--enable-gpl **分支**：

> 1. **FFmpeg 锁死为纯 LGPL 构建**：配置严禁开启 \--enable-gpl 与 \--enable-nonfree（不编译 libx264、libx265、yadif、postproc 等 GPL 部件）8，在纯 LGPL 边界内按白名单扩展解复用器（增补 asf、mpegps、webm、ivf、rm 等）与软解码器以弥补 NVDEC 不支持的老旧编码4。  
> 2. **CPU 软编码统一收敛为** SVT-AV1：SVT-AV1（v0.9 及之后版本）采用 BSD-3-Clause-Clear **\+** Alliance for Open Media Patent License 1.09，与 GPL-3.0-or-later 完全兼容且不触发 FFmpeg \--enable-gpl10。需要 HEVC 编码时统一走硬件 NVENC，需要高压缩比 CPU 10-bit CRF 软编码时独占使用 SVT-AV1（复用 GpuToCpuStaging 作为唯一显式 D2H 编码下传出口）4。同时须注意开源许可审核资料指出的细节：BSD-3-Clause-Clear 拥有标准 SPDX 标识，而 Alliance for Open Media Patent License 1.0 尚未列入标准 SPDX 清单45，因此在接入 SvtAv1Enc.lib 时须同步向 AuditSpdx 规则与第三方合规声明补充其完整法务文本登记。

### **5.2 物理锁死** .mp4 **单一输出容器的流处理矩阵**

依据既有产品边界，所有转码与拼接合并任务的输出封装物理锁定为 .mp44。为防止在渲染结束写封装头时因不兼容音轨、字幕或附件流引发失败（例如 MKV 内常见的 .ttf 字体附件会导致封装报错中止）43，输入媒体进入管线前必须先经过无状态函数 resolve\_mp4\_stream\_plan() 结合 libavformat 的 avformat\_query\_codec() 做确定性预裁决（以下兼容性规则综合自 FFmpeg 官方文档与专业编解码容器实证指南4）：

| 输入流类型 | 具体编码情形 | 固定 .mp4 处理策略 | 工程原理与用户契约 |
| :---- | :---- | :---- | :---- |
| 视频输出流 | H.264 / HEVC / AV1 | **直接封装（HEVC 强制附加** hvc1 **标签）** | 三者均被 MP4 原生支持；HEVC 需显式写入 hvc1 标签以确保跨生态播放器兼容性43 |
| MP4 原生兼容音轨 | AAC / Opus / FLAC（以及经 avformat\_query\_codec 校验通过的 MP3/AC-3 等） | **独立导出默认流拷贝；MergeConcat 模式统一重编码对齐** | 现代 MP4 封装器支持 AAC、Opus 与 FLAC43；但异构拼接多段输入时，若声道/采样率/编码不同须统一重采样编码防时间戳撕裂 |
| MP4 不兼容音轨 | PCM (pcm\_s16le 等) 及 avformat\_query\_codec 拒收编码（如 DTS / TrueHD / Vorbis 等） | **确定性软解并重采样转码为 AAC（或按无损偏好转 FLAC）** | MP4 封装器对 PCM 等格式缺少标准 Tag 注册，直接 \-c copy 必在写头阶段失败43 |
| 文本字幕流 | SubRip (srt) / ASS / SSA / WebVTT | **显式转码为** mov\_text **并提示样式裁剪** | MP4 通用文本字幕标准为 mov\_text（3GPP Timed Text）；转换可无损保留时间轴与纯文本，但会丢弃 ASS 特效字体、颜色与定位43 |
| 图像字幕与字体附件 | PGS / VobSub / DVB 及 MKV 内嵌 ttf/otf 字体附件 | **默认剥离排除并明确告警；仅当用户主动勾选时硬烧录进画面** | 图像字幕无法无损转文本且 MP4 无原生 Tag，而 MKV 字体附件映射入 MP4 会直接触发找不到 Tag 报错43 |

## **六、全栈核心系统演进方案与Phase 13四波次施工路线**

### **6.1 核心媒体链路与界面体系的极限演进设计**

> 1. **RIFE 画幅泛化与通用切镜总线（消除** 16:9 **牢笼与 NVOFA 果冻）**：严格遵守 \[DEC-011\] 禁令，绝不在单 .engine 内搞多 Optimization Profile（Phase 11.4 已实证其导致 8.89 GB 显存飙升与 \< 1fps 坍塌）4。改用按画幅分桶的物理独立单 Profile 引擎（如涵盖横屏、竖屏、超宽与方屏桶），超出推理上限时基于面积自适应下采样 infer\_scale 并做对称镜像居中填充4。同时将原本仅接在 RIFE 路径的 8x8 分块 SAD 检测4 升级为置于解码后的无状态公共原语 analyze\_temporal\_boundary()，融合亮度方差与直方图严格区分硬切、渐隐全黑与高速运动，同步向 RIFE、NVOFA（修复 consumeNvofSceneCut() 消费链1）、VSR（重置时域历史）与 NVENC（强制 IDR 关键帧）广播状态，彻底消灭 ShotGroupSegmenter 死代码与光流切镜果冻4。  
> 2. **D3D12 \+ DirectComposition 统一视口与 60fps AI 实时播放器**：在修复 job\_queue.cpp 预览回调注入的基础上，将 preview\_latest\_slot\_ 从初始化 0 改为单调递增帧代数计数器，彻底杜绝首帧未就绪前的空槽位借阅4；移除 960x540 与固定 100.0f 亮度硬编码，使 PreviewViewport 在首帧按实际交换链宽高重算 Letterbox/Pillarbox 等比采样矩阵4；复选框取消勾选预览时从 CUDA Pack Kernel、共享环写入到 30Hz Compute Dispatch 实施全链路真旁路降功耗4。播放器链路打通 NVDEC \-\> 切镜检测 \-\> FRUC(RIFE/NVOFA) \-\> 跟随视口VSR \-\> TrueHDR+3D LUT \-\> D3D12 ToneMap \-\> DComp，使用 std::chrono::duration\<int64\_t, std::ratio\<1001, ...\>\> 精密对齐 23.976/24/25 fps 到 60fps 的插帧步长并支持 Seek/步进瞬时重置时域缓存；同时将 WebUI 明确界定为“远程控制台 \+ 轻量代理预览面板”，彻底放弃让浏览器 HTML5 \<video\> 直接硬解 10-bit HDR MKV 的伪需求4。  
> 3. **契约透明的横向 DAG 管线与 D2D1/DWrite 极净 UI**：摒弃导致 UiWidgetNode 超限的节点类树，以 sys\_dynload 动态加载 d2d1.dll \+ dwrite.dll \+ dcomp.dll（完全不触发 comctl32/uxtheme/d3d12/dxgi/ole32 静态导入禁令4），依托 IDWriteTextLayout 实现中英文混排自动折行撑开高度、弹性滚动检查器、队列内动态增删与独立多输出/MergeConcat 切换、以及类 PR 横排可拖拽卡片4。i18n 全仓收敛为编译期 ID \+ std::vformat 位置参数插值单一数据源，语言切换实时触发重测算与回流4。DAG 算子全部携带显式色域/精度契约标签（SDR 与 BT.2020 PQ），当用户拖拽产生非法拓扑（例如先经过 TrueHDR 升至 BT.2020 PQ 再送入仅支持 SDR 的下游算子）时，UI 直接给出拓扑锁提示或明确显示自动插入的逆色调映射转换节点，彻底消除隐藏的强制覆写逻辑4。

### **6.2 Phase 13 四波次原子施工与** docs/TODO **收编总表**

| 演进波次 | 原子施工范围（均基于 0beda87 逐波增量推进） | 门禁指标要求 | 放行验收标准 |
| :---- | :---- | :---- | :---- |
| **Wave 0：止损清剿与单 EXE 立宪** | 回退 0beda871；拔除 rtx\_headless\_cli.exe 并合入安全混合入口6；移除 \--enable-gpl、CINERTX\_ENABLE\_GPL\_CODECS 与 \--disable-bcrypt1；借助 dumpbin /imports 清零 kernelbase.dll 与 bcrypt.dll；导出 34 类清单并清退可由 STL 替代的自定义类、死开关、全局单例与 \#pragma 43244；以 std::variant\<std::monostate, ...\> 静音化测试期 WASAPI4 | MSVC /W4 /WX 零警告；AuditTypes 严禁增加（净降）；Static IAT \<= 3（零 bcrypt/kernelbase）；AuditPrivacy \== 0；AuditSpdx \== PASS | cmd.exe 重定向与双击启动均无异常6；rtx\_tests.exe 157/157 PASS 且物理声卡零 440Hz 鸣响1 |
| **Wave B：全容器、SVT-AV1、MergeConcat 与预览/切镜闭环** | 在 0beda87 上复现 74\_ 的 areConcatInputsHomogeneous() 与 MergeConcat 异构路由及单测1；恢复 job\_queue.cpp 预览回调注入并修复槽位代数同步与 Letterbox1；落地公共 analyze\_temporal\_boundary() 同步驱动 RIFE 与 NVOFA1；接入 SvtAv1Enc.lib（10-bit CRF）、扩展 LGPL 解复用器、.mp4 兼容预裁决矩阵、\<chrono\> 1001 时基及任务独立隔离（消除 onStartTranscode 覆写）4 | Static IAT \<= 3；零新增 GPL 污染；Core SLOC 保持低水位膨胀约束 | 异构片段 MergeConcat 输出无音画漂移 .mp4；NVOFA 切镜果冻清零；PGS/PCM 等非原生流依矩阵确定性转码或告警剥离；预览开关勾选/暂停零延迟生效1 |
| **Wave C：60fps AI 实时播放器、任意比例 RIFE 与 WebUI 定界** | 实施分立画幅桶 RIFE 单 Profile 引擎与自适应缩放填边4；打通 16.67 ms 60fps 全 GPU 增强播放管线与 Seek/步进/变速对齐4；同步校核 rife\_pack.cu 与 rife\_pack.ptx 寄存器安全等价性5；完成 WebUI 远程控制 \+ 轻量代理预览定位切割4 | Static IAT \<= 3；Core SLOC \<= 23,0004 | 21:9 / 4:3 / 1:1 输入经 RIFE 无越界报错与紫屏3；24-\>60fps 播放音画漂移收敛至单帧内；RTX 5060 实测 120-rife 稳定闭环3 |
| **Wave D：D2D1/DWrite 极净 UI、单源 i18n 与契约式 DAG** | 动态加载 d2d1.dll \+ dwrite.dll \+ dcomp.dll 替换全部老旧 Win32 控件并移除 gdi32.dll4；上线单一 std::vformat i18n 源与拖拽式契约检验 DAG 卡片4；将剩余 user32.dll 窗口 API 移入 GUI 分支按需 sys\_dynload | **最终目标** Static IAT \== 1**（仅** kernel32.dll**）**；零 UiWidgetNode 类膨胀 | 动态切换语言零文本截断；非法 DAG 顺序显式阻断或自动插入转换核；命令行无头运行进程零加载 user32.dll / win32u.sys |

针对 01\_ 议题 13 备案的 docs/TODO/README.md 既有 7 项远期规划4，在 Wave 0 同步按以下终审裁决修订归档，严禁在当前源码中为远期事项预先留设抽象接口或死开关：

| 原 TODO 编号与事项 | Phase 13 终审裁决 | 依据与处理边界 |
| :---- | :---- | :---- |
| 第 7 项：CPU 软编码 \+ CRF 恒定质量 | **全量吸收（仅保留** SVT-AV1**）** | 纳入 Wave B；利用 BSD 许可系的 SVT-AV1 彻底根除 x265 与 \--enable-gpl4 |
| 第 1 项：NTSC 1001 时基因子扩展 | **全量吸收（以** \<chrono\> **实现）** | 纳入 Wave B；以 std::ratio\<1001, ...\> 无额外类代价解决 23.976/29.97/59.94 fps 合并与 60fps 播放同步精度4 |
| 第 2 项：FFmpeg 8.x 演进 | **延后至 Phase 14** | Phase 13 在已验证静态 FFmpeg 基线上按需增补纯 LGPL 解复用器，不并行承担主版本升级风险4 |
| 第 3 项：Nsight Systems 线级证据 | **转为 Wave C 性能验真工具** | 作为 16.67 ms 帧预算调优与 CUDA 流气泡排查的宿主实测手段，不作为功能模块编码4 |
| 第 4 项：HDR10 物理徽标 | **并入 Wave D D2D1 卡片自绘** | 随 Direct2D 矢量卡片管线顺带绘制，不单独立项4 |
| 第 5 项：2 小时 Soak 长稳测试 | **收编为 Wave C/D 发布前强制门禁** | 验证长时间插帧、音频时钟漂移与连续异构合并的内存/显存零泄漏4 |
| 第 6 项：Linux 无头形态 | **移出 Phase 13（远期独立规划）** | 本阶段全部极致瘦身均深度绑定 Windows PE IAT 与 D3D12/DComp 零回读通路，严禁现阶段为此预留跨平台抽象层与冗余虚接口，确保奥卡姆剃刀贯彻到底4 |

#### **引用的著作**

> 1. 75\_CineRTX Phase 13 74号返工验收阻塞与防覆写协同治理委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 2. [[REDACTED_ID]]([REDACTED_ID])  
> 3. 61\_CineRTX RIFE 重构(60号)验收阻塞与C ABI 单测及PTX 修复委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 4. 01\_CineRTX Phase 13 顶层架构重塑与全栈能力演进领域专家咨询委托书.md, [[REDACTED_ID]]([REDACTED_ID])  
> 5. 63\_CineRTX Phase 13 RIFE重构与PTX指令级合龙多波次多阶段施工Superpower规约.md, [[REDACTED_ID]]([REDACTED_ID])  
> 6. WriteFile on redirected stdout in Windows GUI app returns, [https\://stackoverflow.com/questions/64106748/writefile-on-redirected-stdout-in-windows-gui-app-returns-error-no-data](https://stackoverflow.com/questions/64106748/writefile-on-redirected-stdout-in-windows-gui-app-returns-error-no-data)  
> 7. \[Tracking\] Windows Desktop GUI: console windows (cmd/conhost/git, [https\://github.com/NousResearch/hermes-agent/issues/54220](https://github.com/NousResearch/hermes-agent/issues/54220)  
> 8. FFmpeg License and Legal Considerations, [https\://www\.ffmpeg.org/legal.html](https://www.ffmpeg.org/legal.html)  
> 9. AOMediaCodec/SVT-AV1: The canonical URL for this ... \- GitHub, [https\://github.com/aomediacodec/svt-av1](https://github.com/aomediacodec/svt-av1)  
> 10. meshlive-android/THIRD\_PARTY\_NOTICES.md at main \- GitHub, [https\://github.com/MeshLive/meshlive-android/blob/main/THIRD\_PARTY\_NOTICES.md](https://github.com/MeshLive/meshlive-android/blob/main/THIRD_PARTY_NOTICES.md)  
> 11. What's new for C++ in Visual Studio \- Microsoft Learn, [https\://learn.microsoft.com/en-us/cpp/overview/what-s-new-for-visual-cpp-in-visual-studio?view=msvc-170](https://learn.microsoft.com/en-us/cpp/overview/what-s-new-for-visual-cpp-in-visual-studio?view=msvc-170)  
> 12. What's new for MSVC Build Tools \- Microsoft Learn, [https\://learn.microsoft.com/en-us/cpp/overview/what-s-new-for-msvc?view=msvc-170](https://learn.microsoft.com/en-us/cpp/overview/what-s-new-for-msvc?view=msvc-170)  
> 13. Thread: \[Mingw-w64-public\] winpthreads: misc changes \- SourceForge, [https\://sourceforge.net/p/mingw-w64/mailman/mingw-w64-public/thread/20261113141258.oeqebxb2r2dwgupp%40pali/](https://sourceforge.net/p/mingw-w64/mailman/mingw-w64-public/thread/20261113141258.oeqebxb2r2dwgupp%40pali/)  
> 14. File: random\_seed.c \| Debian Sources, [https\://sources.debian.org/src/ffmpeg/7%3A8.1.2-2/libavutil/random\_seed.c](https://sources.debian.org/src/ffmpeg/7%3A8.1.2-2/libavutil/random_seed.c)  
> 15. console windows (cmd/conhost/git/gh/powershell) flash on ... \- GitHub, [https\://github.com/NousResearch/hermes-agent/issues/54220?timeline\_page=1?timeline\_page=1](https://github.com/NousResearch/hermes-agent/issues/54220?timeline_page=1?timeline_page%3D1)  
> 16. CPP / C++ Notes \- Windows API Programming Win32, [https\://caiorss.github.io/C-Cpp-Notes/WindowsAPI-cpp.html](https://caiorss.github.io/C-Cpp-Notes/WindowsAPI-cpp.html)  
> 17. notes/win32.zh.cn.md at master · wangwenx190/notes \- GitHub, [https\://github.com/wangwenx190/notes/blob/master/win32.zh.cn.md](https://github.com/wangwenx190/notes/blob/master/win32.zh.cn.md)  
> 18. AGENTS.md \- BSOD-MEMZ/CppSekai \- GitHub, [https\://github.com/BSOD-MEMZ/CppSekai/blob/main/AGENTS.md](https://github.com/BSOD-MEMZ/CppSekai/blob/main/AGENTS.md)  
> 19. GitHub \- wesmar/kvcKiller: Advanced AV/EDR Killer, [https\://github.com/wesmar/kvcKiller](https://github.com/wesmar/kvcKiller)  
> 20. GitHub \- wesmar/CmdT: The world's smallest TrustedInstaller launcher, [https\://github.com/wesmar/cmdt](https://github.com/wesmar/cmdt)  
> 21. Logging to stderr and corresponding character encoding, [https\://learn.microsoft.com/en-au/answers/questions/5922651/logging-to-stderr-and-corresponding-character-enco](https://learn.microsoft.com/en-au/answers/questions/5922651/logging-to-stderr-and-corresponding-character-enco)  
> 22. Redirecting output from an application linked with /SUBSYSTEM, [https\://github.com/PowerShell/PowerShell/discussions/25875](https://github.com/PowerShell/PowerShell/discussions/25875)  
> 23. std.debug.warn hangs when no stderr is attached on windows \#4196, [https\://github.com/ziglang/zig/issues/4196](https://github.com/ziglang/zig/issues/4196)  
> 24. configure \- jellyfin-ffmpeg \- GitHub, [https\://github.com/jellyfin/jellyfin-ffmpeg/blob/jellyfin/configure](https://github.com/jellyfin/jellyfin-ffmpeg/blob/jellyfin/configure)  
> 25. configure \- chromium/third\_party/ffmpeg \- Git at Google, [https\://chromium.googlesource.com/chromium/third\_party/ffmpeg/+/21d322f670919495bd39917a70d71d85ac3d109c/configure](https://chromium.googlesource.com/chromium/third_party/ffmpeg/+/21d322f670919495bd39917a70d71d85ac3d109c/configure)  
> 26. Backporting ffmpeg 5.1.2 to Microsoft Windows XP / XP x64, [http\://wp.xin.at/archives/5851](http://wp.xin.at/archives/5851)  
> 27. osuPoint \- Certual \- GitHub, [https\://github.com/Certual/osuPoint](https://github.com/Certual/osuPoint)  
> 28. STL/stl/src/atomic\_wait.cpp at main · microsoft/STL \- GitHub, [https\://github.com/microsoft/STL/blob/main/stl/src/atomic\_wait.cpp](https://github.com/microsoft/STL/blob/main/stl/src/atomic_wait.cpp)  
> 29. atomic waits do not link in ARM64 \#1504 \- microsoft/STL \- GitHub, [https\://github.com/microsoft/STL/issues/1504](https://github.com/microsoft/STL/issues/1504)  
> 30. Memory Validator \- Change History \- Software Verify, [https\://www\.softwareverify.com/product/memory-validator-change-history/](https://www.softwareverify.com/product/memory-validator-change-history/)  
> 31. Reversing Disassembly Reconstruction Accelerated \[2nd revised, [https\://dokumen.pub/reversing-disassembly-reconstruction-accelerated-2nd-revised-version-9781912636693.html](https://dokumen.pub/reversing-disassembly-reconstruction-accelerated-2nd-revised-version-9781912636693.html)  
> 32. 南海/FFmpeg-avs2-avs3 \- Gitee, [https\://gitee.com/zhengtianbo/FFmpeg-avs2-avs3/blob/master/configure](https://gitee.com/zhengtianbo/FFmpeg-avs2-avs3/blob/master/configure)  
> 33. Microsoft C/C++ language conformance by Visual Studio version, [https\://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance?view=msvc-170](https://learn.microsoft.com/en-us/cpp/overview/visual-cpp-language-conformance?view=msvc-170)  
> 34. What's New for C++ Developers in Visual Studio 2026 version 18.0, [https\://devblogs.microsoft.com/cppblog/whats-new-for-cpp-developers-in-visual-studio-2026-version-18-0/](https://devblogs.microsoft.com/cppblog/whats-new-for-cpp-developers-in-visual-studio-2026-version-18-0/)  
> 35. Visual Studio 2026 release notes \- Microsoft Learn, [https\://learn.microsoft.com/en-us/visualstudio/releases/2026/release-notes](https://learn.microsoft.com/en-us/visualstudio/releases/2026/release-notes)  
> 36. Pure Virtual C++ 2026: MSVC, are we there yet? Status of C ... \- Reddit, [https\://www\.reddit.com/r/cpp/comments/1v2bmeu/pure\_virtual\_c\_2026\_msvc\_are\_we\_there\_yet\_status/](https://www.reddit.com/r/cpp/comments/1v2bmeu/pure_virtual_c_2026_msvc_are_we_there_yet_status/)  
> 37. HEVC Encoder: x265.org, [https\://x265.org/](https://x265.org/)  
> 38. x265/COPYING at master · videolan/x265 \- GitHub, [https\://github.com/videolan/x265/blob/master/COPYING](https://github.com/videolan/x265/blob/master/COPYING)  
> 39. Umbra/THIRD-PARTY-NOTICES.md at release/umbra-0.5 \- GitHub, [https\://github.com/PreceptorOfMagic/Umbra/blob/release/umbra-0.5/THIRD-PARTY-NOTICES.md](https://github.com/PreceptorOfMagic/Umbra/blob/release/umbra-0.5/THIRD-PARTY-NOTICES.md)  
> 40. Open-source notices \- SnapID Mark, [https\://snapidmark.com/en/open-source/](https://snapidmark.com/en/open-source/)  
> 41. ffmpeg-tutorials/compile\_on\_windows.md at master \- GitHub, [https\://github.com/ffiirree/ffmpeg-tutorials/blob/master/compile\_on\_windows.md](https://github.com/ffiirree/ffmpeg-tutorials/blob/master/compile_on_windows.md)  
> 42. \[FFmpeg-devel\] \[PR\] 264 (PR \#22079), [https\://lists.ffmpeg.org/lore/ffmpeg-devel/177160281310.25.14875183411717503262@29965ddac10e/T/](https://lists.ffmpeg.org/lore/ffmpeg-devel/177160281310.25.14875183411717503262@29965ddac10e/T/)  
> 43. FFmpeg container error? It's the subtitle track: \-c:s mov\_text, [https\://www\.ffmpeg-micro.com/blog/ffmpeg-container-error-it-s-the-subtitle-track-c-s-mov-text](https://www.ffmpeg-micro.com/blog/ffmpeg-container-error-it-s-the-subtitle-track-c-s-mov-text)  
> 44. FFmpeg Codecs Documentation, [https\://ffmpeg.org/ffmpeg-codecs.html](https://ffmpeg.org/ffmpeg-codecs.html)  
> 45. license review: BSD-3-Clause-Clear coupled with Alliance for Open, [https\://gitlab.com/fedora/legal/fedora-license-data/-/issues/383](https://gitlab.com/fedora/legal/fedora-license-data/-/issues/383)  
> 46. ffmpeg media converter \- Ubuntu Manpages, [https\://manpages.ubuntu.com/manpages/noble/man1/ffmpeg-all.1.html](https://manpages.ubuntu.com/manpages/noble/man1/ffmpeg-all.1.html)  
> 47. FFmpeg Formats Documentation, [https\://ffmpeg.org/ffmpeg-formats.html](https://ffmpeg.org/ffmpeg-formats.html)  
> 48. Use FFmpeg to add text subtitles \[closed\] \- Stack Overflow, [https\://stackoverflow.com/questions/8672809/use-ffmpeg-to-add-text-subtitles](https://stackoverflow.com/questions/8672809/use-ffmpeg-to-add-text-subtitles)