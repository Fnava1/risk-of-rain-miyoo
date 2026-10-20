import os
import io
import pytest
from PIL import Image

# Import the module to be implemented
from tools.optimize_ror_assets import (
    quantize_texture_surface_16bit,
    calculate_texture_memory_mb,
    AudioOptimizationConfig,
    validate_texture_budget
)

def test_texture_quantization_reduces_size_by_half():
    """Verify converting an RGBA 2048x2048 image to 16-bit achieves exactly 50% byte size."""
    width, height = 2048, 2048
    # Create test RGBA8888 image (4 bytes per pixel = 16 MB)
    img = Image.new("RGBA", (width, height), color=(255, 128, 64, 255))
    
    # Quantize to 16-bit (2 bytes per pixel = 8 MB)
    quantized_bytes, fmt = quantize_texture_surface_16bit(img, mode="RGB565")
    
    original_size = width * height * 4
    quantized_size = len(quantized_bytes)
    
    assert fmt == "RGB565"
    assert quantized_size == width * height * 2
    assert quantized_size == original_size // 2

def test_texture_quantization_rgba4444_for_alpha():
    """Verify that RGBA4444 format is selected for images with translucent alpha."""
    width, height = 256, 256
    img = Image.new("RGBA", (width, height), color=(100, 150, 200, 128)) # translucent
    
    quantized_bytes, fmt = quantize_texture_surface_16bit(img, mode="RGBA4444")
    assert fmt == "RGBA4444"
    assert len(quantized_bytes) == width * height * 2

def test_texture_budget_validation():
    """Verify budget validator ensures texture pool stays <= 28 MB."""
    # 3 texture pages of 2048x2048 in 16-bit = 3 * 8 MB = 24 MB (valid)
    assert validate_texture_budget(num_pages=3, page_width=2048, page_height=2048, bpp=2, max_mb=28) is True
    
    # 5 texture pages of 2048x2048 in 16-bit = 5 * 8 MB = 40 MB (exceeds budget)
    assert validate_texture_budget(num_pages=5, page_width=2048, page_height=2048, bpp=2, max_mb=28) is False

def test_audio_optimization_config():
    """Verify audio downsampling parameters conform to Miyoo specs (22,050 Hz, Q2)."""
    cfg = AudioOptimizationConfig(target_sample_rate=22050, quality=2)
    cmd = cfg.build_ffmpeg_args("input.ogg", "output.ogg")
    
    assert "-ar" in cmd
    assert "22050" in cmd
    assert "-qscale:a" in cmd
    assert "2" in cmd
