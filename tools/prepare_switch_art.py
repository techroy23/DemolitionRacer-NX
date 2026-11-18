"""Prepare the supplied launcher art without touching the original game assets."""

import argparse
from pathlib import Path

from PIL import Image, ImageOps


def prepare(icon_path, splash_path, output):
    output.mkdir(parents=True, exist_ok=True)
    with Image.open(icon_path) as source:
        # The square launcher tile prioritizes the title at the top of the poster.
        icon = ImageOps.fit(source.convert('RGB'), (256, 256), Image.Resampling.LANCZOS, centering=(0.5, 0.0))
        icon.save(output / 'game_icon.jpg', quality=90, optimize=True)
    with Image.open(splash_path) as source:
        canvas = Image.new('RGBA', (1280, 720), (0, 0, 0, 255))
        splash = ImageOps.contain(source.convert('RGB'), canvas.size, Image.Resampling.LANCZOS)
        canvas.paste(splash, ((canvas.width - splash.width) // 2, (canvas.height - splash.height) // 2))
        (output / 'game_splash.bgra').write_bytes(canvas.tobytes('raw', 'BGRA'))


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('icon', type=Path)
    parser.add_argument('splash', type=Path)
    parser.add_argument('output', type=Path)
    options = parser.parse_args()
    prepare(options.icon, options.splash, options.output)
