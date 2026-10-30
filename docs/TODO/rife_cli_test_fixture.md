# `cli_rife_command` 覆盖缺口：进程内引擎构建缺少可解析的 ONNX 夹具

> **文档标识**：`TODO-TEST-RIFE-CLI-FIXTURE-001`
> **所属目录**：`docs/TODO/rife_cli_test_fixture.md`
> **起草日期**：2026-10-29
> **状态**：已知缺口 / 本轮明确接受，暂不修复
> **关联**：`CMakeLists.txt`（`cli_rife_command` 用例）、`src/cli/main.cpp`（`buildRife`）、`src/shim/backends/tensorrt/rife_engine.cpp`

---

## 1. 问题

`cli_rife_command` 原本覆盖两件事：TensorRT 版本锁（`10.13.3.9`）与 `--build-rife` 的进程内构建 + `.engine.sha256` 旁车写入。

TensorRT 10.13.3.9 改为配置期自动供给后，`RTX_HAVE_TENSORRT` 在 Windows 构建中恒为开启，进程内构建分支成为默认路径；而该用例的输入 `model.onnx` 是配置期写入构建目录的文本占位文件（`file(WRITE ... "CineRTX rife gate fixture\n")`），必然解析失败：

```
# TensorRT baseline: 10.13.3.9 (10.14+ regresses RIFE v4.25 activation memory)
error: INVALID_ARGUMENT: cannot parse the ONNX graph (model.onnx)
```

`set_tests_properties(... PASS_REGULAR_EXPRESSION "10\.13\.3\.9")` 一旦命中，CTest 忽略进程退出码，因此用例仍然 PASS——但它现在**只验证版本锁**，不再验证构建成功路径与旁车写入。

## 2. 本轮处置

- 为保证版本锁在成功与失败两条路径上都可见，`buildRife` 已把基线声明移到构建尝试之前（`src/cli/main.cpp`）。
- 明确接受该覆盖削弱，**不新增 ONNX 夹具**：真实 RIFE v4.25 模型体积与权重许可都不适合进仓库，`tools/check_spdx.ps1` 也把 `*.onnx` 列为禁止入库后缀。

## 3. 候选修复（未排期）

1. **`--build-rife --dry-run`**：只做参数校验、ONNX 摘要与 trtexec 命令生成，不触碰 TensorRT；CTest 断言旁车写入与命令内容。改动最小，但不再覆盖真实构建。
2. **独立测试夹具生成器**：用 TensorRT 的 `INetworkDefinition` 现场构造一个 1×7×H×W 的最小图并序列化为 ONNX，测试时先跑生成器再跑 `--build-rife`。可覆盖真实构建，但需要引入 ONNX 序列化依赖（或手写 protobuf），成本最高。
3. **CI 专用夹具**：在带 GPU 的流水线里用 `trtexec --onnx=<下载的模型>` 预置引擎，测试改为校验旁车与 `readEngineSidecar`；仓库仍不存权重。

## 4. 触发条件

出现下列任一情况时应重新评估：RIFE 执行链路真正接入产品主循环、`--build-rife` 成为发布流程的一部分、或 TensorRT 版本闸门再次调整。
