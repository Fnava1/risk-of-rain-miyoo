"""
Risk of Rain (2013) Asset Optimization Tool for Miyoo Mini Plus.
Quantizes textures from RGBA8888 (32-bit) to RGB565 / RGBA4444 (16-bit)
and prepares audio downsampling parameters for Tremor (libvorbisidec).
"""

import os
import sys
import struct
import argparse
from dataclasses import dataclass
from typing import Tuple, List
from PIL import Image

@dataclass
class AudioOptimizationConfig:
    target_sample_rate: int = 22050
    quality: int = 2
    channels: int = 2

    def build_ffmpeg_args(self, input_file: str, output_file: str) -> List[str]:
        """Generate ffmpeg command-line flags to resample OGG files for Miyoo."""
        return [
            "ffmpeg", "-y", "-i", input_file,
            "-vn",
            "-ar", str(self.target_sample_rate),
            "-ac", str(self.channels),
            "-c:a", "libvorbis",
            "-qscale:a", str(self.quality),
            output_file
        ]

def calculate_texture_memory_mb(num_pages: int, width: int = 2048, height: int = 2048, bpp: int = 2) -> float:
    """Calculate VRAM/RAM required by texture pages in megabytes."""
    total_bytes = num_pages * width * height * bpp
    return total_bytes / (1024.0 * 1024.0)

def validate_texture_budget(num_pages: int, page_width: int = 2048, page_height: int = 2048, bpp: int = 2, max_mb: float = 28.0) -> bool:
    """Ensure total active texture memory stays under the strict Miyoo threshold."""
    return calculate_texture_memory_mb(num_pages, page_width, page_height, bpp) <= max_mb

def quantize_texture_surface_16bit(image: Image.Image, mode: str = "RGB565") -> Tuple[bytes, str]:
    """
    Quantize an RGBA PIL Image to 16-bit packed raw pixels (little-endian uint16).
    """
    img = image.convert("RGBA")
    width, height = img.size
    pixels = img.load()
    
    out = bytearray(width * height * 2)
    idx = 0
    
    if mode == "RGB565":
        for y in range(height):
            for x in range(width):
                r, g, b, _ = pixels[x, y]
                val = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3)
                out[idx] = val & 0xFF
                out[idx + 1] = (val >> 8) & 0xFF
                idx += 2
        return bytes(out), "RGB565"
        
    elif mode == "RGBA4444":
        for y in range(height):
            for x in range(width):
                r, g, b, a = pixels[x, y]
                val = ((r >> 4) << 12) | ((g >> 4) << 8) | ((b >> 4) << 4) | (a >> 4)
                out[idx] = val & 0xFF
                out[idx + 1] = (val >> 8) & 0xFF
                idx += 2
        return bytes(out), "RGBA4444"
        
    else:
        raise ValueError(f"Unsupported 16-bit mode: {mode}")

def optimize_game_data_package(input_win_path: str, output_win_path: str, textures: List[str] = None, mode: str = "RGB565") -> bool:
    """
    Processes game data and quantizes textures into the target output container.
    """
    if not os.path.exists(input_win_path):
        print(f"[!] Error: {input_win_path} not found.")
        return False

    with open(input_win_path, "rb") as f_in:
        raw_header = f_in.read(1024)

    quantized_chunks = bytearray()
    if textures:
        for tex in textures:
            if os.path.exists(tex):
                img = Image.open(tex)
                q_bytes, _ = quantize_texture_surface_16bit(img, mode=mode)
                quantized_chunks.extend(q_bytes)

    with open(output_win_path, "wb") as f_out:
        f_out.write(raw_header)
        f_out.write(b"MMP_TEXTURE_PAYLOAD")
        f_out.write(quantized_chunks)

    print(f"[+] Output data package created at {output_win_path} ({os.path.getsize(output_win_path)} bytes).")
    return True

def main():
    parser = argparse.ArgumentParser(description="Risk of Rain 16-bit Asset Optimizer for Miyoo Mini Plus")
    parser.add_argument("--input-win", help="Path to input data.win", default="data.win")
    parser.add_argument("--output-win", help="Path to output data_mmp.win", default="data_mmp.win")
    parser.add_argument("--mode", choices=["RGB565", "RGBA4444"], default="RGB565")
    args = parser.parse_args()
    
    print(f"[*] Optimizing {args.input_win} -> {args.output_win} with mode {args.mode}")
    optimize_game_data_package(args.input_win, args.output_win, mode=args.mode)

if __name__ == "__main__":
    main()
