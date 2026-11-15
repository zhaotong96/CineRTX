# CineRTX Phase 9：SuperPower 施工单 (Track 9C)
## 模型构建治理、污染清零与 FP32 坐标算子链精度锁定

- **任务编号**：`SP-P9-TRACK-9C`
- **执行责任**：TensorRT 模型构建与算子精度工程专家
- **涉及模块与文件**：
  - `src/shim/ngx_bridge/rife_builder.cpp`
  - `src/shim/ngx_bridge/rife_engine.cpp`
  - `src/shim/ngx_bridge/bridge_exports.cpp`
- **依赖前置**：无（可独立并行推进）

---

## 一、缺陷机理与施工目标

### 1.1 缺陷定位
1. **假引擎写盘污染磁盘**：当工作区未放置外部 ONNX 时，`rife_engine.cpp:206` 自动调用构建器，将合成的仅 15KB~95KB 的极简占位网络写入 `models/rife_v4.25.engine`。这导致磁盘被假文件污染，后续即便下载了正版权重或换用真实环境，系统依然将该 95KB 文件视为合法 RIFE 模型并跳过重新构建。
2. **占位网络对 $\alpha$ 零响应**：内置占位网络（`buildBuiltinTemporalNetwork`）硬编码 `0.5*I0 + 0.5*I1`，第 6 通道 $\alpha$ 权重全部赋 `0`，任意时刻插值均输出固定画面。
3. **光流采样网格 FP16 下溢坍缩**：依据 TensorRT Issue #4721 与 vs-mlrt Issue #66，在真实 RIFE v4.25 模型转 TensorRT FP16 模式时，坐标生成流（`Cast`、`Div`、`Reciprocal`、`Mul`、`Sub`、`Add`、`Slice`）若被下放至半精度，极易发生网格截断，导致插值光流归零或伪影。
4. **桥接层重复二次推理冗余**：`bridge_exports.cpp:1335` 在硬件光流 Warp 已完成插值并写入 `out_nv12` 之后，又执行了一次 `impl->rife_engine->interpolate(out_nv12, out_nv12, ...)`，逻辑混乱且产生死循环式图像退化。

### 1.2 施工目标
- 扩展 ONNX 模型查找路径，严禁将占位合成网络命名或写入为 `models/rife_v4.25.engine`；若磁盘存在小于 512KB 的脏占位文件，自动识别并拒绝加载；
- 改造合成网络使其具备基础的 $\alpha$ 相位线性混合响应能力，保证单测下的动态演进；
- 在 `rife_builder.cpp` 构建真实 ONNX 时，对坐标网格链与元素级算子做完整的 `FP32` 强制锁定；
- 拔除 `bridge_exports.cpp` 中无意义的二次推理代码。

---

## 二、逐行施工与代码变更清单

### 2.1 改造 `src/shim/ngx_bridge/rife_engine.cpp`（防污染机制）
1. **拦截脏占位文件与扩展 ONNX 候选名单**：
   ```cpp
   // 检查磁盘既有 engine 是否有效（真实 RIFE 引擎通常 >= 10MB，占位文件仅数十 KB）
   if (std::filesystem::exists(candidate, ec)) {
       const auto fsize = std::filesystem::file_size(candidate, ec);
       if (fsize < 512u * 1024u) {
           // 脏占位文件：拒绝当作真实引擎，强制重新搜寻 ONNX 或内存构建
           resolved_engine.clear();
       } else {
           resolved_engine = candidate;
           found_existing = true;
       }
   }
   ```
2. **禁止将 `__builtin__` 写入正式路径**：
   ```cpp
   if (onnx_source == "__builtin__") {
       // 仅在设置了显式测试环境变量时允许内存加载，绝不允许写盘命名为 rife_v4.25.engine
       resolved_engine = base_dirs.front() / "models" / "rife_builtin_synth.engine";
   }
   ```

### 2.2 改造 `src/shim/ngx_bridge/rife_builder.cpp`（FP32 坐标网格锁精度）
依据 `vs-mlrt` 官方成熟经验，在 `rtx_ngx_bridge_build_rife_engine` 中全面锁定非卷积层的精度：

```cpp
config->setFlag(nvinfer1::BuilderFlag::kFP16);
config->setFlag(nvinfer1::BuilderFlag::kOBEY_PRECISION_CONSTRAINTS);

const int32_t nbLayers = network->getNbLayers();
for (int32_t i = 0; i < nbLayers; ++i) {
    nvinfer1::ILayer* layer = network->getLayer(i);
    if (!layer) continue;
    const auto type = layer->getType();
    const std::string name = layer->getName() ? layer->getName() : "";

    // 锁定 GridSample 算子以及所有与网格坐标变换相关的 ElementWise / Unary / Cast 算子为 FP32
    const bool is_coord_sensitive =
        (type == nvinfer1::LayerType::kGRID_SAMPLE) ||
        (type == nvinfer1::LayerType::kREDUCE) ||
        (type == nvinfer1::LayerType::kCAST) ||
        (type == nvinfer1::LayerType::kSLICE) ||
        (type == nvinfer1::LayerType::kUNARY) ||
        (type == nvinfer1::LayerType::kELEMENTWISE);

    if (is_coord_sensitive) {
        layer->setPrecision(nvinfer1::DataType::kFLOAT);
        for (int32_t outIdx = 0; outIdx < layer->getNbOutputs(); ++outIdx) {
            if (auto* out = layer->getOutput(outIdx)) {
                if (out->getType() == nvinfer1::DataType::kFLOAT ||
                    out->getType() == nvinfer1::DataType::kHALF) {
                    layer->setOutputType(outIdx, nvinfer1::DataType::kFLOAT);
                }
            }
        }
    }
}
```

### 2.3 清理 `src/shim/ngx_bridge/bridge_exports.cpp`
定位第 1334–1338 行：
```cpp
// 删除以下画蛇添足的重复推理调用：
// (void)impl->rife_engine->interpolate(
//     out_nv12, out_nv12, pitch, alpha, out_nv12, full_range, nullptr,
//     impl->interop->pack_stream());
```
光流扭曲 `launchNvofWarpNv12` 已经将依据 $\alpha$ 混合好的图像写入 `out_nv12`，无需二次空转推理。

---

## 三、验收标准与自测门禁 (DoD)

1. **防污染生效**：删除现有 `rife_v4.25.engine`，在缺少 ONNX 时运行，磁盘上不得生成小于 512KB 的脏 `rife_v4.25.engine`；
2. **算子精度受控**：执行 `--build-rife-engine` 编译 ONNX 时，控制台打印锁定的 FP32 敏感层数量，且构建生成的 `.engine` 能正常运行；
3. **静态审计通过**：`bridge_exports.cpp` 二次冗余推理 100% 拔除。
