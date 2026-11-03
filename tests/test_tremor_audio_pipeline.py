import unittest
import os

class TestTremorAudioPipeline(unittest.TestCase):
    def setUp(self):
        self.root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
        self.sdl_audio_c = os.path.join(self.root, "butterscotch", "src", "audio", "sdl1", "sdl_audio_system.c")
        self.tremor_c = os.path.join(self.root, "src", "audio", "audio_tremor.c")
        self.tremor_h = os.path.join(self.root, "src", "audio", "audio_tremor.h")
        self.loop_c = os.path.join(self.root, "butterscotch", "src", "loop.c")

        self.assertTrue(os.path.exists(self.sdl_audio_c))
        self.assertTrue(os.path.exists(self.tremor_c))
        self.assertTrue(os.path.exists(self.tremor_h))
        self.assertTrue(os.path.exists(self.loop_c))

        with open(self.sdl_audio_c, "r", encoding="utf-8", errors="ignore") as f:
            self.sdl_audio_src = f.read()
        with open(self.tremor_h, "r", encoding="utf-8", errors="ignore") as f:
            self.tremor_h_src = f.read()
        with open(self.loop_c, "r", encoding="utf-8", errors="ignore") as f:
            self.loop_src = f.read()

    def test_tremor_declarations(self):
        """Verifies audio_tremor.h declares ring buffer and streaming functions."""
        self.assertIn("AUDIO_RING_BUFFER_SIZE", self.tremor_h_src)
        self.assertIn("audio_stream_init", self.tremor_h_src)
        self.assertIn("audio_stream_read_pcm", self.tremor_h_src)
        self.assertIn("audio_stream_write_pcm", self.tremor_h_src)

    def test_sdl_audio_tremor_integration(self):
        """Verifies sdl_audio_system.c integrates Tremor fixed-point fast-path."""
        self.assertIn("USE_TREMOR_AUDIO", self.sdl_audio_src)
        self.assertIn("audio_stream_read_pcm", self.sdl_audio_src)

    def test_loop_tremor_lifecycle(self):
        """Verifies loop.c initializes Tremor audio stream on Miyoo."""
        self.assertIn("USE_TREMOR_AUDIO", self.loop_src)
        self.assertIn("audio_stream_init", self.loop_src)

if __name__ == "__main__":
    unittest.main()
