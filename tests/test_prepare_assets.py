import io
import struct
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

import tools.prepare_assets as asset_tool
from tools.prepare_assets import extract_extent, read_archive_catalog, SECTOR_SYNC


def make_sector(payload, submode=8):
    return SECTOR_SYNC + b'\x00\x02\x00\x02' + bytes((1, 0, submode, 0)) * 2 + payload.ljust(2328, b'\0')


class AssetPreparationTests(unittest.TestCase):
    def test_raw_xa_retains_mode2_header_and_subchannel(self):
        sector = make_sector(b'XA sample', 0x64)
        result = io.BytesIO()
        extract_extent(io.BytesIO(sector), result, 0, 2048, raw=True)
        self.assertEqual(result.getvalue(), sector)

    def test_form1_copies_only_requested_payload_bytes(self):
        image = io.BytesIO(make_sector(b'A' * 2048) + make_sector(b'BCD'))
        result = io.BytesIO()
        extract_extent(image, result, 0, 2050, raw=False)
        self.assertEqual(result.getvalue(), b'A' * 2048 + b'BC')

    def test_truncated_raw_sector_fails(self):
        with self.assertRaises(ValueError):
            extract_extent(io.BytesIO(make_sector(b'')[:-1]), io.BytesIO(), 0, 2048, raw=True)

    def test_archive_offsets_start_after_the_original_index(self):
        catalog = struct.pack('<32sII', b'FRONTEND\\ART.TIM', 123, 2).ljust(20480, b'\0')
        entries = read_archive_catalog(catalog, 24576 + 123)
        self.assertEqual(entries, [('FRONTEND/ART.TIM', 24576, 123)])

    def test_archive_rejects_out_of_bounds_entries(self):
        catalog = struct.pack('<32sII', b'ART.TIM', 100, 0).ljust(20480, b'\0')
        with self.assertRaises(ValueError):
            read_archive_catalog(catalog, 20480 + 99)

    def test_archive_rejects_traversal(self):
        catalog = struct.pack('<32sII', b'FRONTEND\\..\\ART.TIM', 1, 0).ljust(20480, b'\0')
        with self.assertRaises(ValueError):
            read_archive_catalog(catalog, 20481)

    def test_archive_rejects_case_collisions_on_fat(self):
        catalog = (struct.pack('<32sII', b'ART.TIM', 1, 0) + struct.pack('<32sII', b'art.tim', 1, 1)).ljust(20480, b'\0')
        with self.assertRaises(ValueError):
            read_archive_catalog(catalog, 24576)


class SwitchDataPreparationTests(unittest.TestCase):
    def test_explicit_raw_image_without_suffix_reaches_hash_validation(self):
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            disc = project / 'disc-image'
            disc.write_bytes(b'unsupported dump')
            script = Path(__file__).resolve().parents[1] / 'tools' / 'prepare_assets.py'
            result = subprocess.run([sys.executable, str(script), str(disc)],
                                    cwd=project, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn('Unsupported disc SHA-256', result.stderr)
            self.assertFalse((project / 'pack').exists())

    def test_cue_resolves_quoted_bin_relative_to_cue(self):
        with tempfile.TemporaryDirectory() as directory:
            cue_path = Path(directory) / 'disc.cue'
            cue_path.write_text('REM comment\nfile "Game Disc.bin" binary\n track 01 mode2/2352\n index 01 00:00:00\n',
                                encoding='utf-8-sig')
            self.assertEqual(asset_tool.resolve_cue_image(cue_path), cue_path.parent / 'Game Disc.bin')

    def test_cue_rejects_unsupported_layouts_and_nonlocal_bin_paths(self):
        with tempfile.TemporaryDirectory() as directory:
            cue_path = Path(directory) / 'disc.cue'
            for filename, layout in (
                ('disc.bin', 'TRACK 01 MODE1/2352\nINDEX 01 00:00:00'),
                ('disc.bin', 'TRACK 01 MODE2/2352\nINDEX 01 00:02:00'),
                ('disc.bin', 'TRACK 01 MODE2/2352\nINDEX 01 00:00:00\nTRACK 02 AUDIO'),
                ('../disc.bin', 'TRACK 01 MODE2/2352\nINDEX 01 00:00:00'),
                ('C:disc.bin', 'TRACK 01 MODE2/2352\nINDEX 01 00:00:00'),
            ):
                with self.subTest(filename=filename, layout=layout):
                    cue_path.write_text(f'FILE "{filename}" BINARY\n{layout}', encoding='utf-8')
                    with self.assertRaisesRegex(ValueError, 'CUE'):
                        asset_tool.resolve_cue_image(cue_path)

    def test_direct_output_refuses_existing_data_even_when_empty(self):
        with tempfile.TemporaryDirectory() as directory:
            output_path = Path(directory)
            (output_path / 'DATA').mkdir()
            with self.assertRaisesRegex(FileExistsError, 'DATA'):
                asset_tool.prepare_switch_data(output_path / 'missing.bin', output_path)
            self.assertEqual(list((output_path / 'DATA').iterdir()), [])

    def test_no_argument_cli_uses_default_cue_and_checks_bin_hash(self):
        with tempfile.TemporaryDirectory() as directory:
            project = Path(directory)
            disc = project / 'DISC'
            disc.mkdir()
            (disc / 'Demolition Racer (USA).cue').write_text(
                'FILE "Demolition Racer (USA).bin" BINARY\nTRACK 01 MODE2/2352\nINDEX 01 00:00:00\n',
                encoding='utf-8')
            (disc / 'Demolition Racer (USA).bin').write_bytes(b'unsupported dump')
            script = Path(__file__).resolve().parents[1] / 'tools' / 'prepare_assets.py'
            result = subprocess.run([sys.executable, str(script)],
                                    cwd=project, capture_output=True, text=True)
            self.assertEqual(result.returncode, 1, result.stderr)
            self.assertIn('Unsupported disc SHA-256', result.stderr)
            self.assertFalse((project / 'dist').exists())
