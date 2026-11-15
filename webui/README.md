# CineRTX WebUI

浏览器端的专业转码控制台。与 `CineRTX.exe`(Win32 原生 GUI / CLI 双模单 exe)并存的第三种前端形态,面向远程/无桌面场景。

## 文件边界

**WebUI 本体 = 恰好 4 个文件**(本目录下的其他文件都是后端适配器或文档/测试,不属于 WebUI 本体):

| 文件 | 角色 |
| --- | --- |
| `index.html` | 唯一界面文档;每个控件用 `data-bind` 声明状态路径、`data-i18n` 声明词条 |
| `app.js` | 唯一脚本:响应式 store + 双向绑定引擎 + i18n 表(与 `include/rtx/i18n.h` 113 项词条 SSOT 对齐)+ SSE 客户端 |
| `app.css` | 唯一样式:暗色专业工作站主题(色板对齐原生 GUI 的碳黑 `#121212` 与 NVIDIA Signature Green `#76B900`) |
| `state.json` | **前端状态持久化层** = 单一 JSON 状态文档(SSOT),UI 与它 1:1 双向绑定 |

**后端侧适配器(非 WebUI 本体)**:`server.py` — 零依赖 stdlib JSON 桥。前端与后端的全部通信是它承载的 JSON;argv 翻译等引擎细节只存在于这一层,前端对引擎零知识。

## 运行

```bash
# 真实引擎(自动按 state.json 的 engine.path / CINERTX_ENGINE / exe 同级目录解析)
python webui/server.py --port 8787
# 显式指定引擎与状态文档
python webui/server.py --engine D:/dist/CineRTX.exe --state webui/state.json
# 无 RTX 硬件的机器上端到端联调(夹具引擎,模拟 CLI 输出契约)
python webui/server.py --engine webui/test/mock_cinertx.bat
```

浏览器打开 `http://127.0.0.1:8787/`。

## JSON 通信契约(前端 ↔ 桥)

| 路由 | 方向 | 载荷 |
| --- | --- | --- |
| `GET /api/state` | 拉取 | 完整状态文档(`ui/profile/engine` + 服务器所有的 `runtime`) |
| `PUT /api/state` | 持久化 | `{ui, profile, engine}`(服务端校验范围/枚举后原子写盘;`runtime` 由服务器独占,客户端写入被忽略) |
| `POST /api/probe` | 动作 | 执行 `--probe --json`,原生 JSON 原样入库 `runtime.probe` |
| `POST /api/job` | 动作 | `{action:"start"\|"cancel"}`;start 可选携带 `profile` 覆盖 |
| `GET /api/events` | 订阅 | SSE;每个事件都是一个 JSON 文档:`hello` / `runtime`(进度、报告、日志,≤4Hz)/ `state-file`(外部改盘推送) |
| `GET /api/fs?path=` | 浏览 | 通用 JSON 目录列表(路径选择弹窗) |
| `GET /api/health` | 探活 | 引擎解析结果/版本、作业状态 |

## 状态文档与所有权

```
state.json
├── version            文档版本
├── ui                 浏览器所有(语言/自动命名/日志显隐)
├── engine             浏览器所有(引擎路径/工作目录)
├── profile            浏览器所有(转码参数矩阵,对齐 HwTranscodeOptions)
└── runtime            服务器所有(探针结果、作业生命周期、遥测、日志尾)
```

**双向绑定的三条通道**(全部以 JSON 为介质):

1. **DOM → state → 磁盘**:控件 `input/change` → `setPath()` → 250ms 防抖 `PUT /api/state`;
2. **磁盘 → state → DOM**:`server.py` 监视 `state.json`(过滤自身写入,防回写竞态)→ SSE `state-file` → 客户端应用 → 控件重渲染;
3. **引擎 → state → DOM**:`runtime` 事件(≤4Hz 节流发布、≥1Hz 落盘)→ 合并进 `state.runtime` → 进度条/遥测/报告/日志自动更新。

作业执行时前端只发 `{action:"start"}`,服务端从 `state.json` 读 `profile` 翻译为 `CineRTX.exe` 参数(唯一引擎知识点:`JobRunner.build_argv`,server.py:402),无 `--json` 启动以获得逐帧进度,实时解析 `[Transcode]` 进度行与 `printHwReport` 报告块,全部以 JSON 事件回推。

## 设计说明与已知边界

- **VSR 勾选框不提供**:CLI 契约(`src/cli/main.cpp` 参数表)没有关闭 VSR 的开关(产品路径恒过 NGX 桥),WebUI 只暴露 `vsrQuality 1..4`,与 CLI 能力严格对齐。
- **Ctrl+L 切语言为尽力而为**:Chromium 系把 Ctrl+L 预留给地址栏,部分内核会拦截;点右上角 `EN/中文` 按钮始终有效(语言选择持久化在 `ui.lang`)。
- **多标签页**:每个标签页独立持有状态副本,自己的编辑即时落盘;另一个标签页需重载或等待外部改盘事件才会看到(`PUT` 故意不回声,防止快速连续编辑被在途快照回写覆盖——验收报告中有该竞态的实证与修复记录)。
- **媒体流与三态工作站**:支持 `[1] Encoder`(转码矩阵)、`[2] Player`(播放审片与分屏对比)与 `[3] NLE`(非线编时间轴)三态切换;媒体流式预览通过 `/api/media` HTTP 206 断点续传流式端点直接向原生 `<video>` 供给，保持状态通道与像素流清晰解耦。
- `test/mock_cinertx.py` 是测试夹具(模拟 CLI 控制台契约),不是产品组件。
