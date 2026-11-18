"""Build the Switch DATA directory from a private SLUS-00969 raw disc dump."""

import argparse
import hashlib
import re
import struct
import tempfile
from pathlib import Path


SECTOR_SYNC = b'\x00' + b'\xff' * 10 + b'\x00'
SECTOR_SIZE = 2352
PAYLOAD_SIZE = 2048
CATALOG_SIZE = 10 * PAYLOAD_SIZE
DISC_SHA256 = '7227af2b19221d10f9d8707402909f8f166668bb5868b1b0ce59c95d472349bc'
# LBA and ISO directory size on the supported image. XA/STR need whole sectors,
# including original mode, subheader, channel IDs, position, and padding.
FILE_EXTENTS = {
    'DD.DAT': (272, 32325632),
    'SLUS_009.69': (24, 507904),
    'MOVPLAY.EXE': (16056, 135168),
    'INTRO.STR': (16122, 16908288),
    'INFLOGO.STR': (24378, 1146880),
    'TATOU.STR': (24938, 2293760),
    'PIT.STR': (26058, 4194304),
    'WIN.STR': (28106, 2424832),
    'LOSE.STR': (29290, 2359296),
    'PRES.XA': (30442, 92651520),
    'GAME.XA': (75682, 78249984),
    'CREDIT.STR': (113890, 21676032),
}


def extract_extent(image, output, first_sector, logical_size, *, raw):
    if first_sector < 0 or logical_size < 0:
        raise ValueError('Invalid extent')
    image.seek(first_sector * SECTOR_SIZE)
    remaining_bytes = logical_size
    for sector_number in range(first_sector, first_sector + (logical_size + PAYLOAD_SIZE - 1) // PAYLOAD_SIZE):
        sector = image.read(SECTOR_SIZE)
        if (len(sector) != SECTOR_SIZE or sector[:12] != SECTOR_SYNC or sector[15] != 2
                or sector[16:20] != sector[20:24]):
            raise ValueError(f'Invalid Mode 2 sector at LBA {sector_number}')
        if raw:
            output.write(sector)
        else:
            if sector[18] & 0x20:
                raise ValueError(f'Expected Form 1 data at LBA {sector_number}')
            output.write(sector[24:24 + min(PAYLOAD_SIZE, remaining_bytes)])
        remaining_bytes -= PAYLOAD_SIZE


def read_archive_catalog(catalog, archive_size):
    if len(catalog) != CATALOG_SIZE:
        raise ValueError('Truncated DD.DAT index')
    entries = []
    seen = set()
    for raw_name, size, sector_offset in struct.iter_unpack('<32sII', catalog):
        if raw_name[0] == 0:
            return entries
        name_bytes, terminator, _ = raw_name.partition(b'\0')
        if not terminator or not re.fullmatch(rb'[A-Za-z0-9_-][A-Za-z0-9_.-]*(?:\\[A-Za-z0-9_-][A-Za-z0-9_.-]*)*', name_bytes):
            raise ValueError(f'Unsafe DD.DAT entry: {raw_name!r}')
        name = name_bytes.decode('ascii').replace('\\', '/')
        if name.casefold() in seen:
            raise ValueError(f'Duplicate DD.DAT entry: {name}')
        seen.add(name.casefold())
        offset = CATALOG_SIZE + sector_offset * PAYLOAD_SIZE
        if offset + size > archive_size:
            raise ValueError(f'DD.DAT entry outside archive: {name}')
        entries.append((name, offset, size))
    raise ValueError('DD.DAT index has no terminator')


def prepare_assets(image_path, output_path):
    if output_path.exists():
        raise FileExistsError(f'Output already exists: {output_path}. Choose a new directory.')
    with image_path.open('rb') as image:
        digest = hashlib.file_digest(image, 'sha256').hexdigest()
        if digest != DISC_SHA256:
            raise ValueError(f'Unsupported disc SHA-256: {digest}; expected {DISC_SHA256}')
        output_path.parent.mkdir(parents=True, exist_ok=True)
        with tempfile.TemporaryDirectory(prefix='.prepare-', dir=output_path.parent) as staging:
            staging_path = Path(staging)
            data_path = staging_path / 'DATA'
            data_path.mkdir()
            for name, (first_sector, logical_size) in FILE_EXTENTS.items():
                destination = (staging_path if name == 'DD.DAT' else data_path) / name
                with destination.open('xb') as output:
                    extract_extent(image, output, first_sector, logical_size, raw=name.endswith(('.STR', '.XA')))
            archive_path = staging_path / 'DD.DAT'
            with archive_path.open('rb') as archive:
                catalog = archive.read(CATALOG_SIZE)
                entries = read_archive_catalog(catalog, archive_path.stat().st_size)
                (data_path / 'DD.IDX').write_bytes(catalog)
                for name, offset, size in entries:
                    destination = data_path / name
                    destination.parent.mkdir(parents=True, exist_ok=True)
                    archive.seek(offset)
                    payload = archive.read(size)
                    if len(payload) != size:
                        raise ValueError(f'Truncated DD.DAT entry: {name}')
                    with destination.open('xb') as output:
                        output.write(payload)
            # Stage on the same filesystem. The destination is created exclusively.
            output_path.mkdir()
            try:
                data_path.rename(output_path / 'DATA')
            except OSError:
                output_path.rmdir()
                raise
    return len(entries)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('image', type=Path, help='Single-track MODE2/2352 BIN')
    parser.add_argument('--out', type=Path, default=Path('pack'), help='New output directory (default: pack)')
    arguments = parser.parse_args()
    try:
        entry_count = prepare_assets(arguments.image, arguments.out)
    except (OSError, ValueError) as error:
        parser.exit(1, f'{parser.prog}: {error}\n')
    print(f'Prepared {entry_count} archive entries in {arguments.out / "DATA"}')


if __name__ == '__main__':
    main()
