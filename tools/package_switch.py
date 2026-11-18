"""Stage a private, SD-card-ready Demolition Racer Switch distribution."""

import argparse
import shutil
import tempfile
from pathlib import Path


def package_switch(nro, data, settings, output):
    if output.exists():
        raise FileExistsError(f'Output already exists: {output}. Use a new directory or move the old package.')
    with nro.open('rb') as executable:
        executable.seek(16)
        if executable.read(4) != b'NRO0':
            raise ValueError(f'Not a Switch NRO: {nro}')
    if not (data / 'DD.IDX').is_file():
        raise ValueError(f'No prepared asset catalog in {data}; use tools/prepare_assets.py first')
    if not settings.is_file():
        raise FileNotFoundError(settings)
    if output.resolve().is_relative_to(data.resolve()):
        raise ValueError('The output directory must not be inside the input DATA directory')
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.switch-package-', dir=output.parent) as staging:
        package = Path(staging) / 'switch'
        game = package / 'DemolitionRacer'
        game.mkdir(parents=True)
        shutil.copyfile(nro, game / 'DemolitionRacer.nro')
        shutil.copyfile(settings, game / 'settings.yaml')
        shutil.copytree(data, game / 'DATA')
        output.mkdir()
        try:
            package.rename(output / 'switch')
        except OSError:
            output.rmdir()
            raise
    return output / 'switch' / 'DemolitionRacer'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--nro', type=Path, default=Path('switch/build/DemolitionRacer.nro'))
    parser.add_argument('--data', type=Path, default=Path('pack/DATA'))
    parser.add_argument('--settings', type=Path, default=Path('switch/settings.yaml'))
    parser.add_argument('--out', type=Path, default=Path('dist'))
    arguments = parser.parse_args()
    try:
        package = package_switch(arguments.nro, arguments.data, arguments.settings, arguments.out)
    except (OSError, ValueError) as error:
        parser.exit(1, f'{parser.prog}: {error}\n')
    print(f'Package ready: {package}')


if __name__ == '__main__':
    main()
