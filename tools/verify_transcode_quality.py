#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""CineRTX 转码质量与合规性全自动验证诊断工具 (Transcode Quality & Compliance Verifier)

用于端到端验证转码后视频是否为“大体上正确的视频文件”，针对以下四大画质病灶进行定量检测：
1. 偏灰 (Black Level Lift & Milky Fog)
2. 过曝 (Highlight Clipping & Overexposure)
3. 不正确的绿色 (Green Tint & Color Shift)
4. 彩色条带 (False Contouring & Color Banding)
"""

import json
import os
import subprocess
import sys

# 保证在 Windows 控制台下输出 UTF-8 不崩溃
if hasattr(sys.stdout, "reconfigure"):
    sys.stdout.reconfigure(encoding="utf-8")

def find_test_files():
    candidates = [
        (r".vs\gemini_generated_video_6eaef4a7.mp4", "gemini_generated_video_6eaef4a7.mp4"),
        (r".vs\gemini_generated_video_7eb7a09e.mp4", "gemini_generated_video_7eb7a09e.mp4"),
        ("gemini_generated_video_6eaef4a7.mp4", "gemini_generated_video_6eaef4a7.mp4"),
    ]
    for path, name in candidates:
        if os.path.exists(path):
            return path
    return None

def run_ffprobe(video_path):
    cmd = [
        "ffprobe", "-v", "quiet", "-print_format", "json",
        "-show_format", "-show_streams", video_path
    ]
    proc = subprocess.run(cmd, capture_output=True, text=True)
    if proc.returncode != 0:
        return None
    try:
        return json.loads(proc.stdout)
    except Exception:
        return None

def extract_rgb24_frame(video_path, timestamp, output_raw, tonemap_hdr=False):
    if tonemap_hdr:
        cmd = [
            "ffmpeg", "-y", "-ss", str(timestamp), "-i", video_path,
            "-vf", "zscale=t=linear:npl=100,tonemap=tonemap=hable:desat=0,zscale=p=709:t=709:m=709,format=yuv420p",
            "-vframes", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", output_raw
        ]
    else:
        cmd = [
            "ffmpeg", "-y", "-ss", str(timestamp), "-i", video_path,
            "-vframes", "1", "-f", "rawvideo", "-pix_fmt", "rgb24", output_raw
        ]
    proc = subprocess.run(cmd, capture_output=True)
    return proc.returncode == 0

def analyze_raw_frame(raw_path, width, height):
    with open(raw_path, "rb") as f:
        data = f.read()

    total_pixels = width * height
    if len(data) < total_pixels * 3:
        return None

    r = data[0::3]
    g = data[1::3]
    b = data[2::3]

    mean_r = sum(r) / total_pixels
    mean_g = sum(g) / total_pixels
    mean_b = sum(b) / total_pixels

    min_r, max_r = min(r), max(r)
    min_g, max_g = min(g), max(g)
    min_b, max_b = min(b), max(b)

    # 过曝检测: RGB 通道均饱和 >= 245 的像素比例
    overexposed = sum(1 for i in range(total_pixels) if r[i] >= 245 and g[i] >= 245 and b[i] >= 245)
    overexposed_pct = overexposed / total_pixels * 100.0

    # 偏色检测: 绿色通道相对 (R+B)/2 的能量比值
    green_ratio = mean_g / (0.5 * (mean_r + mean_b) + 1e-5)

    # 条带断层检测: 统计微小渐变梯度的断层密度
    banding_steps = 0
    stride = width * 3
    sample_rows = min(80, height)
    for r_idx in range(0, height, max(1, height // sample_rows)):
        offset = r_idx * stride
        for col in range(1, width):
            i1 = offset + (col - 1) * 3
            i2 = offset + col * 3
            dr = abs(data[i2] - data[i1])
            dg = abs(data[i2+1] - data[i1+1])
            db = abs(data[i2+2] - data[i1+2])
            if (dr == 1 or dg == 1 or db == 1) and (dr + dg + db <= 2):
                banding_steps += 1

    return {
        "width": width, "height": height, "pixels": total_pixels,
        "mean": (mean_r, mean_g, mean_b),
        "min": (min_r, min_g, min_b),
        "max": (max_r, max_g, max_b),
        "overexposed_pct": overexposed_pct,
        "green_ratio": green_ratio,
        "banding_indicator": banding_steps
    }

def main():
    import argparse
    parser = argparse.ArgumentParser(description="CineRTX 视频画质与工业合规性全自动验证诊断工具")
    parser.add_argument("--input", "-i", default=None, help="输入视频文件")
    parser.add_argument("--output", "-o", default=r"scratch\test_out.mp4", help="输出视频文件")
    parser.add_argument("--force", "-f", action="store_true", help="强制重新转码")
    args = parser.parse_args()

    print("=" * 60)
    print("   CineRTX 视频画质与工业合规性全自动验证诊断工具")
    print("=" * 60)

    input_file = args.input if args.input else find_test_files()
    if not input_file:
        print("[错误]: 未找到测试源视频文件 (.vs/ 或根目录)！")
        return 1

    output_file = args.output
    if args.force or not os.path.exists(output_file):
        print(f"[提示]: 调起 CineRTX 执行基准转码...")
        exe_path = r"build\cmake\windows-msvc-ninja\CineRTX.exe"
        if not os.path.exists(exe_path):
            print(f"[错误]: CineRTX.exe 未构建完成 ({exe_path})！")
            return 1
        cmd = [exe_path, "--input", input_file, "--output", output_file]
        subprocess.run(cmd, check=True)

    in_probe = run_ffprobe(input_file)
    out_probe = run_ffprobe(output_file)

    if not in_probe or not out_probe:
        print("[错误]: ffprobe 无法解析输入或输出容器！")
        return 1

    in_streams = in_probe.get("streams", [])
    out_streams = out_probe.get("streams", [])

    in_v = next((s for s in in_streams if s.get("codec_type") == "video"), {})
    out_v = next((s for s in out_streams if s.get("codec_type") == "video"), {})
    in_a = next((s for s in in_streams if s.get("codec_type") == "audio"), None)
    out_a = next((s for s in out_streams if s.get("codec_type") == "audio"), None)

    print(f"\n[1. 基础物理容器与流合规性检验]:")
    print(f"  输入源: {input_file}")
    print(f"    - 视频: {in_v.get('codec_name')}, {in_v.get('width')}x{in_v.get('height')}, {in_v.get('pix_fmt')}")
    print(f"    - 色彩: Pri={in_v.get('color_primaries', 'unknown')}, TRC={in_v.get('color_transfer', 'unknown')}, Matrix={in_v.get('color_space', 'unknown')}")
    print(f"    - 音频: {'存在 (' + in_a.get('codec_name') + ')' if in_a else '无音频轨'}")

    print(f"  输出源: {output_file}")
    print(f"    - 视频: {out_v.get('codec_name')}, {out_v.get('width')}x{out_v.get('height')}, {out_v.get('pix_fmt')}")
    print(f"    - 色彩: Pri={out_v.get('color_primaries', 'unknown')}, TRC={out_v.get('color_transfer', 'unknown')}, Matrix={out_v.get('color_space', 'unknown')}")
    print(f"    - 音频: {'存在 (' + out_a.get('codec_name') + ')' if out_a else '【缺失/完全静音】(已登记待办)'}")

    # 多采样点分析 (25%, 50%, 75% 时刻)
    duration = float(out_probe.get("format", {}).get("duration", 5.0))
    timestamps = [duration * 0.25, duration * 0.5, duration * 0.75]

    os.makedirs("scratch", exist_ok=True)
    raw_in = r"scratch\diag_in.raw"
    raw_out = r"scratch\diag_out.raw"

    print(f"\n[2. 逐像素色彩、动态范围与画质病灶深度分析]:")
    for idx, ts in enumerate(timestamps, 1):
        extract_rgb24_frame(input_file, ts, raw_in)
        extract_rgb24_frame(output_file, ts, raw_out)

        stat_in = analyze_raw_frame(raw_in, int(in_v["width"]), int(in_v["height"]))
        stat_out = analyze_raw_frame(raw_out, int(out_v["width"]), int(out_v["height"]))

        if not stat_in or not stat_out:
            continue

        print(f"\n  --- 采样时刻 t = {ts:.2f}s ---")
        print(f"  【源片帧】: 暗部极小值={stat_in['min']}, 高光极大值={stat_in['max']}")
        print(f"              均值RGB=({stat_in['mean'][0]:.1f}, {stat_in['mean'][1]:.1f}, {stat_in['mean'][2]:.1f}), 绿偏比={stat_in['green_ratio']:.3f}, 过曝={stat_in['overexposed_pct']:.2f}%")
        print(f"  【输出帧】: 暗部极小值={stat_out['min']}, 高光极大值={stat_out['max']}")
        print(f"              均值RGB=({stat_out['mean'][0]:.1f}, {stat_out['mean'][1]:.1f}, {stat_out['mean'][2]:.1f}), 绿偏比={stat_out['green_ratio']:.3f}, 过曝={stat_out['overexposed_pct']:.2f}%")
        print(f"              阶调条带阶梯指标={stat_out['banding_indicator']} (源片={stat_in['banding_indicator']})")

    # 抽取并合成一幅可视化的对比 PNG
    mid_ts = duration * 0.5
    png_in = r"scratch\side_in.png"
    png_out = r"scratch\side_out.png"
    png_side = r"scratch\transcode_comparison_side_by_side.png"

    subprocess.run(["ffmpeg", "-y", "-ss", str(mid_ts), "-i", input_file, "-vframes", "1", png_in], capture_output=True)
    if out_v.get("color_transfer") == "smpte2084":
        # HDR10 输出：使用 zscale + hable 色调映射将高动态范围自然映射至标准显示色彩空间预览
        subprocess.run([
            "ffmpeg", "-y", "-ss", str(mid_ts), "-i", output_file,
            "-vf", "zscale=t=linear:npl=100,tonemap=tonemap=hable:desat=0,zscale=p=709:t=709:m=709,format=yuv420p",
            "-vframes", "1", png_out
        ], capture_output=True)
    else:
        subprocess.run(["ffmpeg", "-y", "-ss", str(mid_ts), "-i", output_file, "-vframes", "1", png_out], capture_output=True)

    # 利用 ffmpeg hstack 合成左右对比图
    hstack_cmd = [
        "ffmpeg", "-y",
        "-i", png_in, "-i", png_out,
        "-filter_complex", "[0:v]scale=-1:1080[v0];[1:v]scale=-1:1080[v1];[v0][v1]hstack=inputs=2[out]",
        "-map", "[out]", png_side
    ]
    subprocess.run(hstack_cmd, capture_output=True)

    print(f"\n[3. 视觉诊断图像生成]:")
    print(f"  已自动合成并排对比图: file:///{os.path.abspath(png_side).replace(chr(92), '/')}")

    print(f"\n[4. 综合健康度与四大病灶诊断结论]:")
    # 综合评估
    health_issues = []
    # 偏灰评估
    if stat_out['min'][0] > 25 and stat_out['min'][1] > 25 and stat_out['min'][2] > 25:
        health_issues.append("【偏灰发白】: 输出暗部极小值严重悬浮 (>25)，黑电平未沉底，全片有白雾感！")
    # 过曝评估
    if stat_out['overexposed_pct'] > stat_in['overexposed_pct'] * 2 and stat_out['overexposed_pct'] > 1.5:
        health_issues.append("【高光过曝】: 输出过曝削顶比例显著高于源片，存在死白过曝！")
    # 色偏评估
    if abs(stat_out['green_ratio'] - stat_in['green_ratio']) > 0.06:
        health_issues.append("【不正确的绿色】: 绿色能量比严重漂移，色域/色彩矩阵映射存在倒挂！")
    # 条带评估
    if stat_out['banding_indicator'] > stat_in['banding_indicator'] * 3:
        health_issues.append("【彩色条带】: 阶调断层密集度超过源片 3 倍以上，量化拉伸失真！")

    if health_issues:
        for idx, h in enumerate(health_issues, 1):
            print(f"  {idx}. {h}")
        print(f"\n  >> 总体结论: 视频存在显著非自然失真，需要修正色彩映射回路与色调映射。")
    else:
        print("  >> 总体结论: 视频大体上色彩正常、动态范围合规、无显著病态失真。")

    print("=" * 60)
    return 0

if __name__ == "__main__":
    sys.exit(main())
