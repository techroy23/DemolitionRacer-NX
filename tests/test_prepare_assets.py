import io
import struct
import unittest

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
