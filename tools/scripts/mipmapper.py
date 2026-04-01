#!/usr/bin/env python3
"""
mipmapper.py — 批量为输入图片生成 mipmap chain

功能：
  - 支持单张图 / 整个目录批量处理
  - 支持多种 resampling filter
  - 支持输出为 png / 保持原格式
  - 可选：保存为未压缩 RGBA 的 .dds （需要 pillow 支持 + 文件扩展名 .dds）

用法示例：
  python mipmapper.py input.png out_dir --filter lanczos
  python mipmapper.py assets_dir output_dir --recursive --format png
  python mipmapper.py assets_dir output_dir --recursive --format orig
  python mipmapper.py input.png out_dir --format dds
"""

import os
import argparse
from PIL import Image

RESAMPLING_FILTERS = {
    "nearest": Image.NEAREST,
    "bilinear": Image.BILINEAR,
    "bicubic": Image.BICUBIC,
    "lanczos": Image.LANCZOS,
}

def generate_mipmap_for_image(img: Image.Image,
                              output_dir: str,
                              base_name: str,
                              out_format: str = "png",
                              filter_name: str = "lanczos"):
    """为单张 Image 对象生成 mipmap chain 并保存"""
    os.makedirs(output_dir, exist_ok=True)
    img = img.convert("RGBA")
    w, h = img.size
    level = 0

    # 决定文件扩展名
    ext = out_format.lower()
    if ext == "orig":
        ext = img.format.lower() if img.format else "png"
    elif ext == "dds":
        ext = "dds"
    else:
        ext = out_format

    # 保存原始层级
    out_path = os.path.join(output_dir, f"{base_name}_mip{level}.{ext}")
    img.save(out_path)
    print(f"[Level {level}] {w}x{h} → {out_path}")

    # 逐级缩小
    while w > 1 or h > 1:
        w = max(1, w // 2)
        h = max(1, h // 2)
        level += 1
        img = img.resize((w, h), RESAMPLING_FILTERS.get(filter_name, Image.LANCZOS))
        out_path = os.path.join(output_dir, f"{base_name}_mip{level}.{ext}")
        img.save(out_path)
        print(f"[Level {level}] {w}x{h} → {out_path}")

def process_file(input_path: str, output_dir: str,
                 out_format: str, filter_name: str):
    try:
        with Image.open(input_path) as img:
            base = os.path.splitext(os.path.basename(input_path))[0]
            # 每张图片放一个子目录
            dest = os.path.join(output_dir, base)
            generate_mipmap_for_image(img, dest, base,
                                      out_format=out_format,
                                      filter_name=filter_name)
    except Exception as e:
        print(f"Skipped {input_path}: {e}")

def process_dir(input_dir: str, output_dir: str,
                out_format: str, filter_name: str,
                recursive: bool = False):
    for root, dirs, files in os.walk(input_dir):
        for fn in files:
            if fn.lower().endswith((".png", ".jpg", ".jpeg", ".bmp", ".tga", ".gif")):
                input_path = os.path.join(root, fn)
                # preserve relative subfolder
                rel = os.path.relpath(root, input_dir)
                target_dir = os.path.join(output_dir, rel)
                process_file(input_path, target_dir, out_format, filter_name)
        if not recursive:
            break

def main():
    parser = argparse.ArgumentParser(description="Generate mipmaps for images")
    parser.add_argument("input", help="Input file or directory")
    parser.add_argument("output", help="Output directory")
    parser.add_argument("--format", "-f", default="png",
                        choices=["png", "orig", "dds"],
                        help="Output format / container. 'png' (default), 'orig' keeps original format, 'dds' outputs .dds (uncompressed RGBA if Pillow supports)")
    parser.add_argument("--filter", "-r", default="lanczos",
                        choices=list(RESAMPLING_FILTERS.keys()),
                        help="Resampling filter for downscaling")
    parser.add_argument("--recursive", "-R", action="store_true",
                        help="If input is directory, process subdirectories recursively")
    args = parser.parse_args()

    if os.path.isfile(args.input):
        process_file(args.input, args.output, args.format, args.filter)
    elif os.path.isdir(args.input):
        process_dir(args.input, args.output, args.format, args.filter, args.recursive)
    else:
        print("Input path is not file or directory")

if __name__ == "__main__":
    main()
