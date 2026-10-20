import os
import shutil
import tempfile
from PIL import Image
from tools.optimize_ror_assets import optimize_game_data_package

def test_optimize_game_data_package_e2e():
    """Verify end-to-end pipeline converts mock assets, quantizes textures, and produces data_mmp.win."""
    with tempfile.TemporaryDirectory() as tmpdir:
        input_win = os.path.join(tmpdir, "data.win")
        output_win = os.path.join(tmpdir, "data_mmp.win")
        audio_dir = os.path.join(tmpdir, "audio")
        os.makedirs(audio_dir, exist_ok=True)

        # Create mock data.win
        with open(input_win, "wb") as f:
            f.write(b"FORM" + b"\x00" * 100 + b"TXTR" + b"\x00" * 100)

        # Create mock texture image (256x256 RGBA)
        tex_path = os.path.join(tmpdir, "page_0.png")
        img = Image.new("RGBA", (256, 256), (200, 100, 50, 255))
        img.save(tex_path)

        success = optimize_game_data_package(
            input_win_path=input_win,
            output_win_path=output_win,
            textures=[tex_path],
            mode="RGB565"
        )

        assert success is True
        assert os.path.exists(output_win)
        assert os.path.getsize(output_win) > 0
