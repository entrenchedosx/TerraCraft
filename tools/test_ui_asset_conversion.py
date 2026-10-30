"""Synthetic converter regression tests; no Minecraft assets are required.

Run from the workspace: python tools/test_ui_asset_conversion.py
Fixtures stay under ignored build/foundation/, then are removed.
"""
import json
import struct
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from PIL import Image
import convert_mcassets as converter


class MenuConversionTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(converter.REPO) / "build" / "foundation"
        scratch.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(prefix="ui-assets-", dir=scratch)
        self.root = Path(self.temp.name)
        self.textures = self.root / "textures"
        self.downloaded = self.root / "downloaded"
        self.output = self.root / "generated"
        self.output.mkdir()
        self.locations = patch.multiple(converter, TEX=str(self.textures),
                                        DOWNLOADED_MENU=str(self.downloaded), GEN_MENU=str(self.output))
        self.locations.start()

    def tearDown(self):
        self.locations.stop()
        self.temp.cleanup()

    def widget(self, stem="button", size=(200, 20), border=3):
        path = self.textures / "gui" / "sprites" / "widget" / (stem + ".png")
        path.parent.mkdir(parents=True, exist_ok=True)
        image = Image.new("RGBA", size, (70, 80, 90, 200))
        image.putpixel((0, 0), (10, 20, 30, 40))
        image.save(path)
        Path(str(path) + ".mcmeta").write_text(json.dumps({"gui": {"scaling": {
            "type": "nine_slice", "width": 200, "height": 20, "border": border}}}), encoding="utf-8")
        return path

    def faces(self, directory=None, size=64, offset=0):
        directory = directory or self.textures / "gui" / "title" / "background"
        directory.mkdir(parents=True, exist_ok=True)
        for i in range(6):
            Image.new("RGBA", (size, size), (offset + i, 20, 30, 255)).save(directory / ("panorama_%d.png" % i))
        return directory

    def test_native_widget_dimensions_and_alpha(self):
        for stem, border in converter.MENU_WIDGETS.items():
            self.widget(stem, border=border)
        self.assertEqual(converter.convert_menu_widgets(), (3, [], []))
        data = (self.output / "button.bmp").read_bytes()
        self.assertEqual(struct.unpack_from("<II", data, 18), (200, 20))
        self.assertEqual(struct.unpack_from("<H", data, 28)[0], 32)
        # Top-left pixel is stored in the last bottom-up row, in BGRA order.
        self.assertEqual(data[54 + 19 * 200 * 4:54 + 19 * 200 * 4 + 4], bytes((30, 20, 10, 40)))

    def test_bad_widget_size_is_diagnosed(self):
        self.widget(size=(100, 20))
        count, missing, invalid = converter.convert_menu_widgets()
        self.assertEqual(count, 0)
        self.assertEqual(len(missing), 2)
        self.assertIn("expected native 200x20", invalid[0])
        self.assertFalse((self.output / "button.bmp").exists())

    def test_bad_metadata_is_diagnosed(self):
        self.widget(border=8)
        count, _, invalid = converter.convert_menu_widgets()
        self.assertEqual(count, 0)
        self.assertIn("nine-slice metadata", invalid[0])

    def test_missing_optional_sources(self):
        self.assertEqual(converter.convert_menu_widgets()[0], 0)
        self.assertEqual(len(converter.convert_menu_widgets()[1]), 3)
        self.assertEqual(converter.convert_menu_native_faces()[0], 0)
        self.assertEqual(len(converter.convert_menu_native_faces()[1]), 6)

    def test_native_faces_keep_dimensions_and_order(self):
        self.faces(size=128)
        self.assertEqual(converter.convert_menu_native_faces(), (6, [], []))
        for i in range(6):
            with Image.open(self.output / ("panorama_%d.bmp" % i)) as image:
                self.assertEqual(image.size, (128, 128))
                self.assertEqual(image.getpixel((20, 20))[:3], (i, 20, 30))

    def test_complete_downloaded_set_wins_without_mixing(self):
        self.faces(offset=10)
        self.downloaded.mkdir()
        Image.new("RGB", (64, 64), (100, 0, 0)).save(self.downloaded / "panorama_0.png")
        self.assertEqual(converter.convert_menu_native_faces(), (6, [], []))
        with Image.open(self.output / "panorama_0.bmp") as image:
            self.assertEqual(image.getpixel((0, 0))[:3], (10, 20, 30))
        self.faces(self.downloaded, offset=100)
        self.assertEqual(converter.convert_menu_native_faces(), (6, [], []))
        with Image.open(self.output / "panorama_0.bmp") as image:
            self.assertEqual(image.getpixel((0, 0))[:3], (100, 20, 30))

    def test_invalid_face_set_writes_no_new_faces(self):
        directory = self.faces()
        Image.new("RGB", (64, 63)).save(directory / "panorama_3.png")
        count, missing, invalid = converter.convert_menu_native_faces()
        self.assertEqual((count, missing), (0, []))
        self.assertEqual(len(invalid), 1)
        self.assertFalse(list(self.output.glob("panorama_*.bmp")))

    def test_legacy_panorama_still_available(self):
        self.faces()
        self.assertTrue(converter.convert_menu_panorama())
        with Image.open(self.output / "menu_panorama.bmp") as image:
            self.assertEqual(image.size, (1024, 512))


if __name__ == "__main__":
    unittest.main(verbosity=2)
