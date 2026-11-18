import tempfile
import unittest
from pathlib import Path

from tools.package_switch import package_switch


class SwitchPackageTests(unittest.TestCase):
    def test_packages_only_console_files_in_sd_layout(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            data = root / 'pack' / 'DATA'
            data.mkdir(parents=True)
            (data / 'DD.IDX').write_bytes(b'catalog')
            build = root / 'build'
            build.mkdir()
            nro = build / 'DemolitionRacer.nro'
            nro.write_bytes(bytes(16) + b'NRO0')
            (build / 'DemolitionRacer.elf').write_bytes(b'debug-only')
            settings = root / 'settings.yaml'
            settings.write_bytes(b'LOG: DEBUG\n')
            output = root / 'dist'
            package_switch(nro, data, settings, output)
            expected = {'DemolitionRacer.nro', 'settings.yaml', 'DATA/DD.IDX'}
            folder = output / 'switch' / 'DemolitionRacer'
            self.assertEqual({path.relative_to(folder).as_posix() for path in folder.rglob('*') if path.is_file()}, expected)
            self.assertEqual((folder / 'settings.yaml').read_bytes(), settings.read_bytes())
            self.assertEqual((folder / 'DemolitionRacer.nro').read_bytes(), nro.read_bytes())
            self.assertEqual((folder / 'DATA' / 'DD.IDX').read_bytes(), b'catalog')
            with self.assertRaises(FileExistsError):
                package_switch(nro, data, settings, output)

    def test_rejects_non_nro_before_creating_output(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            invalid_nro = root / 'invalid.nro'
            invalid_nro.write_bytes(b'not an nro')
            with self.assertRaises(ValueError):
                package_switch(invalid_nro, root / 'DATA', root / 'settings.yaml', root / 'dist')
            self.assertFalse((root / 'dist').exists())
