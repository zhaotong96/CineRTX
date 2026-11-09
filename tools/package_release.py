#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CineRTX 绿色自包含免安装发布包构建脚本 (Standalone Release Packager)

自动化收集 CineRTX.exe、rtx_ngx_bridge.dll、RTX Video SDK 与 TensorRT/Optical Flow 运行时库，
生成 SHA256 校验清单，产出开箱即用的绿色发布包。
"""

import argparse
import hashlib
import os
import shutil
import sys
import zipfile

if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")
if hasattr(sys.stderr, "reconfigure"):
    sys.stderr.reconfigure(encoding="utf-8")

REQUIRED_BINARIES = [
    "CineRTX.exe",
    "rtx_ngx_bridge.dll",
    "nvngx_vsr.dll",
    "nvngx_truehdr.dll",
    "NvOFFRUC.dll",
    "cudart64_110.dll",
    "nvinfer_10.dll",
    "nvinfer_builder_resource_10.dll",
    "nvinfer_plugin_10.dll",
    "nvonnxparser_10.dll",
]

PLUGIN_BINARIES = [
    "rtx_ngx_bridge.dll",
    "nvngx_vsr.dll",
    "nvngx_truehdr.dll",
]

def sha256_file(path: str) -> str:
    h = hashlib.sha256()
    with open(path, "rb") as f:
        while chunk := f.read(1024 * 1024):
            h.update(chunk)
    return h.hexdigest()

def package_release(build_dir: str, output_dir: str, make_zip: bool = True) -> int:
    print("=" * 65)
    print("      CineRTX 绿色自包含免安装发布包构建流水线 (Wave 4)")
    print("=" * 65)

    if not os.path.exists(build_dir):
        print(f"[错误]: 构建目录不存在: {build_dir}")
        return 1

    # 检查主二进制
    main_exe = os.path.join(build_dir, "CineRTX.exe")
    if not os.path.exists(main_exe):
        print(f"[错误]: 未在构建目录中找到 CineRTX.exe: {main_exe}")
        return 1

    os.makedirs(output_dir, exist_ok=True)
    plugin_dir = os.path.join(output_dir, "plugins", "nvidia_rtx")
    os.makedirs(plugin_dir, exist_ok=True)

    print(f"[源目录]: {os.path.abspath(build_dir)}")
    print(f"[目标目录]: {os.path.abspath(output_dir)}")
    print("\n[1. 复制核心程序与依赖运行期库]:")

    copied_files = []
    missing_files = []

    for name in REQUIRED_BINARIES:
        src = os.path.join(build_dir, name)
        dst = os.path.join(output_dir, name)
        if os.path.exists(src):
            shutil.copy2(src, dst)
            size_mb = os.path.getsize(dst) / (1024 * 1024)
            print(f"  [√] 部署: {name:<32} ({size_mb:>8.2f} MB)")
            copied_files.append((name, dst))
        else:
            print(f"  [X] 缺失: {name}")
            missing_files.append(name)

    # 复制插件目录备选镜像 (兼容 plugins/nvidia_rtx 探测路径)
    for name in PLUGIN_BINARIES:
        src = os.path.join(build_dir, name)
        if os.path.exists(src):
            dst = os.path.join(plugin_dir, name)
            shutil.copy2(src, dst)

    if missing_files:
        print(f"\n[错误]: 发布包缺少关键依赖文件: {missing_files}")
        return 1

    # 生成 SHA-256 校验和清单
    print("\n[2. 计算并签发 SHA-256 完整性校验清单]:")
    checksums_path = os.path.join(output_dir, "SHA256SUMS.txt")
    with open(checksums_path, "w", encoding="utf-8") as f_sums:
        for name, file_path in copied_files:
            digest = sha256_file(file_path)
            f_sums.write(f"{digest}  {name}\n")
            print(f"  {digest[:16]}...  {name}")

    # 生成使用说明文档 README.txt
    readme_path = os.path.join(output_dir, "README.txt")
    with open(readme_path, "w", encoding="utf-8") as f_readme:
        f_readme.write(
            "CineRTX 纯 GPU 专业转码与非编引擎 (免安装绿色版)\n"
            "====================================================\n\n"
            "本包为完全自包含独立绿色发布包，内置 NVIDIA RTX Video SDK (VSR + TrueHDR)、\n"
            "Optical Flow FRUC 与 TensorRT 运行时，无需安装任何额外运行库。\n\n"
            "运行要求:\n"
            "  * 操作系统: Windows 10 / 11 64-bit\n"
            "  * 显卡硬件: NVIDIA GeForce RTX 30/40/50 系列或 RTX 专业 GPU\n"
            "  * 显卡驱动: NVIDIA Game Ready / Studio 驱动 550.00 以上\n\n"
            "常用命令示例:\n"
            "  1. 硬件探测与自检:\n"
            "     CineRTX.exe --probe\n\n"
            "  2. SDR 4K 原生直通转码 (消除压扁与偏灰):\n"
            "     CineRTX.exe --input input.mp4 --output output_4k_sdr.mp4 --sdr\n\n"
            "  3. TrueHDR 逆色调映射 4K HDR10 转码:\n"
            "     CineRTX.exe --input input.mp4 --output output_4k_hdr10.mp4 --truehdr\n\n"
            "  4. 启动图形化非编界面 (GUI):\n"
            "     CineRTX.exe --gui\n"
        )
    print(f"  [√] 部署说明文档: README.txt")

    if make_zip:
        zip_path = f"{output_dir}.zip"
        print(f"\n[3. 打包压缩绿色归档包]:")
        print(f"  正在生成: {zip_path} ...")
        with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
            for root, _, files in os.walk(output_dir):
                for file in files:
                    full_p = os.path.join(root, file)
                    rel_p = os.path.relpath(full_p, os.path.dirname(output_dir))
                    zf.write(full_p, rel_p)
        zip_size_mb = os.path.getsize(zip_path) / (1024 * 1024)
        print(f"  [√] 压缩完成: {zip_path} ({zip_size_mb:.2f} MB)")

    print("\n" + "=" * 65)
    print("   [成功]: CineRTX 绿色自包含发布包构建闭环完成！")
    print("=" * 65)
    return 0

def main():
    parser = argparse.ArgumentParser(description="CineRTX 绿色发布包构建工具")
    parser.add_argument("--build-dir", "-b", default=r"build\cmake\windows-msvc-ninja", help="CMake 构建输出目录")
    parser.add_argument("--output-dir", "-o", default=r"dist\CineRTX-v0.2.0-win64", help="发布包输出目标目录")
    parser.add_argument("--no-zip", action="store_true", help="不生成 zip 压缩文件")
    args = parser.parse_args()

    return package_release(args.build_dir, args.output_dir, make_zip=not args.no_zip)

if __name__ == "__main__":
    sys.exit(main())
