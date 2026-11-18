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
DEFAULT_CUE = Path('DISC/Demolition Racer (USA).cue')
DEFAULT_SWITCH_FOLDER = Path('dist/switch/DemolitionRacer')
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


def resolve_cue_image(cue_path):
    """Resolve the BIN for the supported single-track CUE layout."""
    lines = cue_path.read_text(encoding='utf-8-sig').splitlines()
    statements = [line.strip() for line in lines if line.strip() and line.split()[0].upper() != 'REM']
    if (len(statements) != 3 or statements[1].upper().split() != ['TRACK', '01', 'MODE2/2352']
            or statements[2].upper().split() != ['INDEX', '01', '00:00:00']):
        raise ValueError(f'Unsupported CUE layout in {cue_path}: expected one MODE2/2352 track starting at 00:00:00')
    file_statement = re.fullmatch(r'FILE\s+(?:"([^"]+)"|([^\s"]+))\s+BINARY', statements[0], re.IGNORECASE)
    if file_statement:
        filename = file_statement[1] or file_statement[2]
        if not re.search(r'[\\/:<>|?*\x00-\x1f]', filename) and Path(filename).suffix.lower() == '.bin':
            return cue_path.parent / filename
    raise ValueError(f'Invalid CUE FILE statement in {cue_path}: expected a local BIN filename and BINARY')


def prepare_assets(image_path, output_path, *, allow_existing_parent=False):
    destination_data = output_path / 'DATA'
    must_not_exist = destination_data if allow_existing_parent else output_path
    if must_not_exist.exists() or must_not_exist.is_symlink():
        raise FileExistsError(f'Output already exists: {must_not_exist}. Choose a new directory with --out.')
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
            if allow_existing_parent:
                publish_switch_data(data_path, destination_data)
            else:
                # Legacy packaging requires a new parent directory.
                output_path.mkdir()
                try:
                    data_path.rename(destination_data)
                except OSError:
                    output_path.rmdir()
                    raise
    return len(entries)


def publish_switch_data(staged_data, destination_data):
    destination_data.parent.mkdir(parents=True, exist_ok=True)
    children = list(staged_data.iterdir())
    # Reserve DATA exclusively, even on platforms where rename can replace an empty directory.
    destination_data.mkdir()
    moved_children = []
    try:
        for child in children:
            child.rename(destination_data / child.name)
            moved_children.append(child)
    except (OSError, KeyboardInterrupt):
        for child in reversed(moved_children):
            (destination_data / child.name).rename(child)
        destination_data.rmdir()
        raise


def prepare_switch_data(image_path, output_path):
    return prepare_assets(image_path, output_path, allow_existing_parent=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('disc', nargs='?', type=Path, default=DEFAULT_CUE,
                        help=f'CUE or raw single-track MODE2/2352 image (default: {DEFAULT_CUE})')
    parser.add_argument('--out', type=Path,
                        help=f'Parent of generated DATA (default: {DEFAULT_SWITCH_FOLDER} for CUE, pack for BIN)')
    arguments = parser.parse_args()
    try:
        if arguments.disc.suffix.lower() == '.cue':
            image_path = resolve_cue_image(arguments.disc)
            output_path = arguments.out or DEFAULT_SWITCH_FOLDER
            entry_count = prepare_switch_data(image_path, output_path)
        else:
            output_path = arguments.out or Path('pack')
            entry_count = prepare_assets(arguments.disc, output_path)
    except (OSError, ValueError) as error:
        parser.exit(1, f'{parser.prog}: {error}\n')
    print(f'Prepared {entry_count} archive entries in {output_path / "DATA"}')


if __name__ == '__main__':
    main()
