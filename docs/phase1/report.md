先核对服务器上的实际写入内容，再做一次源码一致性回传比对。
## 一、服务器上新建/修改的文件

全部位于 `C:\rtx`，**未在服务器上改动任何仓库源码**。

**A. 工具链（为构建临时部署，非交付物）**

| 路径 | 说明 |
| --- | --- |
| `C:\rtx\zig.zip` → `C:\rtx\zig-windows-x86_64-0.13.0\` | Zig 0.13（服务器无 MSVC） |
| `C:\rtx\cmake.zip` → `C:\rtx\cmake-3.29.6-windows-x86_64\` | CMake |
| `C:\rtx\ninja.zip` → `C:\rtx\bin\ninja.exe` | Ninja |
| `C:\rtx\ffmpeg.zip` → `C:\rtx\ffmpeg-n8.1-latest-win64-gpl-shared-8.1\` | FFmpeg 8.1 开发包 + 运行时 |

**B. 仓库副本**

| 路径 | 说明 |
| --- | --- |
| `C:\rtx\repo.tgz` → `C:\rtx\repo\` | 首次传输副本（含 `build\zig`、`build\zig-ffmpeg`） |
| `C:\rtx\repo2.tgz` → `C:\rtx\repo2\` | 最终源码副本（含 `build\zig-ffmpeg`） |
| `C:\rtx\repo2-back.tgz` | 回传本机做哈希比对的导出包 |

**C. 构建产物（`C:\rtx\repo2\build\zig-ffmpeg\`）**

`rtx-transcode.exe`、`rtx-transcode-base.exe`、`rtx_tests.exe`、`rtx_shim.dll`、`rtx_ngx_bridge.dll`，以及 7 个 FFmpeg DLL（`avcodec-62`、`avformat-62`、`avutil-60`、`swscale-9`、`swresample-6`、`avdevice-62`、`avfilter-11`）。

**D. 测试素材与输出**

| 文件 | 来源 |
| --- | --- |
| `sample.mp4` | 从本机上传的原始素材（H.264 720×1280） |
| `sample10.mp4` | 服务器端用 `ffmpeg + hevc_nvenc + p010le` 生成（Main10） |
| `raw.nv12` / `raw.p010` | 裸流验收的输入/输出（33,177,600 / 66,355,200 字节） |
| `out_hevc.mp4`、`out10.mp4`、`final8.mp4`、`final10.mp4` | 各轮转码输出 |
| `jelly10.mkv` | 16 字节，下载失败（HTTP 522）的残留文件 |

**E. 验收包装脚本**

`C:\rtx\gate1.cmd`、`C:\rtx\gate1_10.cmd` —— 仅用于在 cmd 下正确传递引号调用 `verify_gate1.ps1`。

## 二、源码一致性结论

回传 `repo2` 后做了全量 SHA-256 比对（排除 `build/`、`out/`、`[REDACTED_CACHE]/`）：

```
local files  : 100
server files : 100
only local   : 0
only server  : 0
content diff : 0
```

服务器副本与本机**逐字节一致**，服务器上没有产生任何独立修改。

## 三、交给专家评审的仓库

**以本机 `[REDACTED_WORKSPACE]` 为准。** 服务器上的 `repo`、`repo2` 只是构建/测试副本，可随时丢弃，不应作为评审基线。

本机实际变更共 14 个文件：

- **新增（3）**：`src/shim/loader/nv_trace.h`、`tools/verify_gate1.ps1`、`docs/gate1.md`
- **修改（11）**：`CMakeLists.txt`、`README.md`、`docs/architecture.md`、`docs/abi.md`、`docs/testing.md`、`src/cli/main.cpp`、`src/core/hw_transcode.cpp`、`src/core/nvidia_shim.cpp`、`src/io/ffmpeg_io.cpp`、`src/shim/loader/nv_decoder.cpp`、`src/shim/loader/nv_encoder.cpp`

## 四、提交前需处理的一项仓库卫生问题

仓库根目录存在一个名为 `-` 的遗留文件（2417 字节，时间戳 00:38，早于本次返工），内容是 CMake 编译器探测的预处理器转储（`# 1 "C:/PUBLIC/toolchain/pylibs/cmake/...CXX-DetectStdlib.h"`）。它不属于工程源码，但会被打进任何 `tar` 包，评审时容易被误判为异常文件。建议删除——需要我直接清理吗？