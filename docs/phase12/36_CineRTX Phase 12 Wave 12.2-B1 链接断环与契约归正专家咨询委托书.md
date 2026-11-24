# CineRTX Phase 12 Wave 12.2-B1 链接断环与契约归正专家咨询委托书

- **文档编号**：`CINERTX-P12-DELEGATION-036`  
- **归档路径**：`docs/phase12/36_CineRTX Phase 12 Wave 12.2-B1 链接断环与契约归正专家咨询委托书.md`  
- **呈报主体**：CineRTX 项目总协调负责人（Coordinator）  
- **接收主体**：多媒体系统工程与 GPU 异构计算首席领域专家组（Extreme Modern C++ Purist & Architecture Board）  
- **前置依据**：  
  - [`33_CineRTX Phase 12 Wave 12.2-A 全仓代码事实验证裁决书暨Wave12.2-B1正式作战令.md`](./33_CineRTX%20Phase%2012%20Wave%2012.2-A%20全仓代码事实验证裁决书暨Wave12.2-B1正式作战令.md)（`CINERTX-P12-FINAL-SEAL-033`）  
  - [`34_CineRTX Phase 12 Wave 12.2-B1 第一战场施工单_ngx_bridge内部类型清剿与现代标准库替换.md`](./34_CineRTX%20Phase%2012%20Wave%2012.2-B1%20第一战场施工单_ngx_bridge内部类型清剿与现代标准库替换.md)（`CINERTX-P12-WORKORDER-034`）  
  - [`35_CineRTX Phase 12 Wave 12.2-B1 施工汇报.md`](./35_CineRTX%20Phase%2012%20Wave%2012.2-B1%20施工汇报.md)（`CINERTX-P12-REPORT-035`）  
- **呈报日期**：2026-11-23  
- **委托主题**：呈报 Wave 12.2-B1 第一战场类型清剿阶段性战果，汇报 Windows 宿主机 MSVC 链接中断（LNK2019）、门禁契约分类违规与 SLOC 演化实况，提请专家组裁决系统性定点纠偏方案。

---

## 一、 咨询背景与执行现状

依据 33 号终审裁决与作战令，外部执行工程师对第一战场 `src/shim/ngx_bridge/` 实施了外科手术式重构，并提交了 35 号施工汇报。协调负责人通过 Windows 宿主机物理环境对变更文件进行了逐行审计与门禁复验，确认取得重大架构突破，但暴露了两项致命阻断与一项指标裁量问题：

### 1.1 阶段性重大突破（已确证事实）
1. **类型清剿卓越达标**：`src/shim/ngx_bridge/` 具名类型由 30 真实骤降至 **6 个**（净削减 24 个，清剿率 100%），全仓具名类型总数从 128 压减至 **104 个**（提前超额锁定 $\le 105$ 目标）；
2. **许可证隔离彻底加固**：彻底拔除 `rife_engine.cpp` 对 GPLv3 头文件 `include/rtx/shot_segmenter.h` 的越界包含，`AuditSpdx` 门禁实测 **GPL=112, MIT=39** 纯净绿灯；
3. **现代 C++ 规范全面落地**：全量推行 `std::tuple` 配合结构化绑定解包，彻底消灭 `std::get<0>` 魔法索引与伪 typedef；Pimpl 扁平化，CUDA Driver API 收敛为统一命名空间自由函数；
4. **代码卫生清零**：全仓旧式格式化（`snprintf` / `_snwprintf` / `ostringstream`）保持 0 命中，全仓 0 个 `.bak` 垃圾文件。

### 1.2 阻断性硬伤与问题暴露
1. **Windows MSVC 单测链接断环（Blocker 1）**：在 Linux 下由于未编入相关源文件实编全绿，但在 Windows 宿主机下 `rtx_tests.exe` 编入 `src/shim/ngx_bridge/rife_pack_runtime.cpp`，因找不到 `rtx_ngx::nv_cuda::*` 符号，暴发 **12 处 LNK2019 无法解析的外部符号**；
2. **门禁契约分类非法（Blocker 2）**：`cmake -P cmake/AuditTypes.cmake` 拦截报错，`rife_builder.cpp:34` 中 `RifeTrtLogger` 标注的 `cat='Polymorphic-Boundary'` 不在 CMake 允许的 5 类白名单内；
3. **Pure SLOC 分步演进裁量**：当前全仓 Pure SLOC 达成 30,948 行（`ngx_bridge` 单目录净瘦身 1,412 行，降幅 -21.1%），未达 Doc 34 预估的 $\le 29,500$ 行，需裁定后续递延路线。

---

## 二、 呈报专家咨询之核心议题

### 议题一：`rtx_tests.exe` 跨模块链接断环之架构仲裁

#### 1. 物理报错事实（宿主机真实物证）
```text
rife_pack_runtime.cpp.obj : error LNK2019: unresolved external symbol "bool __cdecl rtx_ngx::nv_cuda::driverAvailable(void)" referenced in function "bool __cdecl `anonymous namespace'::ensureLoaded(void)"
rife_pack_runtime.cpp.obj : error LNK2019: unresolved external symbol "bool __cdecl rtx_ngx::nv_cuda::moduleApiLoaded(void)" ...
rife_pack_runtime.cpp.obj : error LNK2019: unresolved external symbol "int __cdecl rtx_ngx::nv_cuda::init(unsigned int)" ...
rife_pack_runtime.cpp.obj : error LNK2019: unresolved external symbol "int __cdecl rtx_ngx::nv_cuda::deviceGet(int *,int)" ...
rife_pack_runtime.cpp.obj : error LNK2019: unresolved external symbol "int __cdecl rtx_ngx::nv_cuda::ctxGetCurrent(void * *)" ...
rife_pack_runtime.cpp.obj : error LNK2019: unresolved external symbol "int __cdecl rtx_ngx::nv_cuda::ctxSetCurrent(void *)" ...
rife_pack_runtime.cpp.obj : error LNK2019: unresolved external symbol "int __cdecl rtx_ngx::nv_cuda::primaryCtxRetain(void * *,int)" ...
rife_pack_runtime.cpp.obj : error LNK2019: unresolved external symbol "int __cdecl rtx_ngx::nv_cuda::moduleLoadData(void * *,void const *)" ...
rife_pack_runtime.cpp.obj : error LNK2019: unresolved external symbol "int __cdecl rtx_ngx::nv_cuda::moduleGetFunction(void * *,void *,char const *)" ...
rife_pack_runtime.cpp.obj : error LNK2019: unresolved external symbol "int __cdecl rtx_ngx::nv_cuda::launchKernel(void *,unsigned int,unsigned int,unsigned int,unsigned int,unsigned int,unsigned int,unsigned int,void *,void * *,void * *)" ...
rife_pack_runtime.cpp.obj : error LNK2019: unresolved external symbol "class std::basic_string<...> __cdecl rtx_ngx::nv_cuda::formatError(char const *,int)" ...
rife_pack_runtime.cpp.obj : error LNK2019: unresolved external symbol "int __cdecl rtx_ngx::nv_cuda::memsetD32Async(unsigned __int64,unsigned int,unsigned __int64,void *)" ...
rtx_tests.exe : fatal error LNK1120: 12 unresolved externals
```

#### 2. 底层架构因果剖析
- 在 `CMakeLists.txt` 中：
  ```cmake
  if(WIN32)
      target_sources(rtx_tests PRIVATE src/shim/ngx_bridge/rife_pack_runtime.cpp)
  endif()
  ```
- `rife_pack_runtime.cpp` 作为源码直接编译入 `rtx_tests.exe`。此前该文件自包含局部驱动加载器；重构后改为依赖统一的 `rtx_ngx::nv_cuda::*` 自由函数（位于 `nv_func_tables.cpp`）；
- 但 `nv_func_tables.h` 中的自由函数未标记 `RTX_SHIM_API`（无 `__declspec(dllexport)`），导致生成的 `rtx_ngx_bridge.lib` 导出表中无此类符号，且 `rtx_tests` 源码集中未包含 `nv_func_tables.cpp`。

#### 3. 候选解决方案比选
- **方案 1（DLL 符号显式导出）**：在 `src/shim/ngx_bridge/nv_func_tables.h` 中为 `namespace nv_cuda` 的函数声明补充 `RTX_SHIM_API` 宏修饰，使符号自 `rtx_ngx_bridge.dll` 导出并在导入库中可见。
  - *优点*：不修改构建脚本，遵循“bridge 内部符号由 DLL 承载”的设计；
  - *疑点*：是否符合仅导出极简 C-ABI 的最初设计哲学？
- **方案 2（构建脚本补全编译源）**：在 `CMakeLists.txt` 中 `if(WIN32)` 分支的 `rtx_tests` 源码列表中追加 `src/shim/ngx_bridge/nv_func_tables.cpp`。
  - *优点*：完全不向 DLL 外部泄露内部 C++ 命名空间符号；
  - *疑点*：`rtx_tests` 直接混编内部 bridge 文件，代码卫生度待裁定。
- **方案 3（测试链路纯洁解耦）**：`rtx_tests` 不直接编译 `rife_pack_runtime.cpp`，改为通过已导出的 bridge C-ABI 接口进行测试。

---

### 议题二：`RifeTrtLogger` 门禁契约分类归正

#### 1. 物理报错事实
```text
CMake Warning at cmake/AuditTypes.cmake:173:
  [AuditTypes] src/shim/ngx_bridge/rife_builder.cpp:34: type 'RifeTrtLogger' lacks valid @rtx-type-justification (cat='Polymorphic-Boundary', why=TRUE, inv=TRUE)
CMake Error at cmake/AuditTypes.cmake:176:
  [AuditTypes] 58 violation(s) detected (valid justified types: 46/35).
```

#### 2. 原因与裁决诉求
- `AuditTypes.cmake` 刚性限制仅 5 类白名单：`C-ABI;Hardware-Layout;NonTrivial-StateMachine;Std-Customization;Domain-Model`；
- 执行工程师擅自发明了 `Polymorphic-Boundary` 类别；
- 提请专家裁定：是否将 `RifeTrtLogger` 归正为 `Std-Customization`（适配 TensorRT SDK 的外部虚基类定制点），或在架构上将其收敛至无状态函数指针？

---

### 议题三：全仓 Pure SLOC 演进路线与分步考核裁量

#### 1. 事实数据
- Doc 34 预估目标：全仓 $\le 29,500$ 行；
- 实测数据：全仓 30,948 行（`ngx_bridge` 由 6,661 行降至 5,249 行，净瘦身 1,412 行，降幅 -21.1%）；
- 执行工程师指出：剩余代码为 PTX 汇编与必需的硬件 API 序列，若强行在单目录内压减 2,860 行将违反“禁止破坏语义、禁止压成单行”的底线。

#### 2. 裁决诉求
- 提请专家裁定：是否认可 35 号汇报的诚实披露，核准将剩余 SLOC 瘦身配额（约 1,448 行）顺延至 Wave 12.2-B2（`src/io/` 与播放会话）及 Wave 12.2-B3（核心层与 UI）集中释放？

---

## 三、 提请专家核发成果清单

为确保第一战场 Wave 12.2-B1 顺利结案并平稳过渡至 Wave 12.2-B2，恳请首席领域专家组签发 **《37 号终审裁决书》** 并赐予以下具体成果：

1. **链接断环纠偏标准代码范本**：裁定议题一的最优工程方案，并提供精确的代码或 CMake 修复范本；
2. **契约注释规范定型**：明确 `RifeTrtLogger` 的合规类别与正规契约注释文本；
3. **阶段性封板放行条件**：裁定修复上述两项阻断后，是否在宿主机 MSVC `/W4 /WX` 85/85 零警告与 147/147 PASS 达成后即可直通签署 Wave 12.2-B1 封板令。
