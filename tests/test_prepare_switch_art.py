import tempfile
import unittest
from pathlib import Path

from PIL import Image
from tools.prepare_switch_art import prepare


class SwitchArtTests(unittest.TestCase):
    def test_wide_splash_fills_canvas_width_without_stretching(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            artwork = root / 'wide.png'
            Image.new('RGB', (320, 150), (255, 0, 0)).save(artwork)
            prepare(artwork, artwork, root)
            pixels = (root / 'game_splash.bgra').read_bytes()
            self.assertEqual(len(pixels), 1280 * 720 * 4)
            canvas = Image.frombytes('RGBA', (1280, 720), pixels, 'raw', 'BGRA')
            self.assertEqual(canvas.getpixel((0, 60)), (255, 0, 0, 255))
            self.assertEqual(canvas.getpixel((1279, 659)), (255, 0, 0, 255))
            self.assertEqual(canvas.getpixel((640, 59)), (0, 0, 0, 255))
            self.assertEqual(canvas.getpixel((640, 660)), (0, 0, 0, 255))
