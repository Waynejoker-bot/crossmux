import hashlib
import json
import sys
import tempfile
import unittest
from pathlib import Path


SCRIPTS = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(SCRIPTS))

import package_igagentos_x3


class IGAgentOSPackageTest(unittest.TestCase):
    @staticmethod
    def write_firmware(path):
        image = bytearray(24)
        image[0] = 0xE9
        image[12:14] = (0x0005).to_bytes(2, 'little')
        image.extend(b'CROSSPOINT-BOARD-V1:x4;')
        image.extend(b'iGAgentOS\x000.1.0\x00')
        path.write_bytes(image)

    def test_packages_versioned_x3_firmware_with_sd_card_alias_and_manifest(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            firmware = root / 'firmware.bin'
            rollback = root / 'firmware-rollback-1.5.7-cn.bin'
            self.write_firmware(firmware)
            rollback.write_bytes(b'rollback')
            (root / 'platformio.ini').write_text(
                '[crosspoint]\nversion = 1.5.8\n'
                '[igagentos]\nname = iGAgentOS\nversion = 0.1.0\n'
            )

            output = package_igagentos_x3.package(
                root=root,
                firmware=firmware,
                output_root=root / 'packages',
                package_date='20260831',
                git_commit='a' * 40,
                development_origin='http://192.168.0.57:2148',
                rollback=rollback,
                rollback_label='CrossMux-v1.5.7-cn',
            )

            self.assertEqual(output.name, 'iGAgentOS-X3-v0.1.0-20260831')
            self.assertEqual((output / 'firmware.bin').read_bytes(), firmware.read_bytes())
            self.assertEqual(
                (output / 'iGAgentOS-X3-v0.1.0.bin').read_bytes(), firmware.read_bytes()
            )
            self.assertEqual(
                (output / 'igrowth-development.txt').read_text(),
                'http://192.168.0.57:2148\n',
            )
            self.assertTrue(
                (output / 'iGAgentOS-X3-rollback-CrossMux-v1.5.7-cn.bin').is_file()
            )

            manifest = json.loads(
                (output / 'iGAgentOS-X3-v0.1.0-manifest.json').read_text()
            )
            self.assertEqual(manifest['product'], {'name': 'iGAgentOS', 'version': '0.1.0'})
            self.assertEqual(manifest['target'], 'Xteink X3')
            self.assertEqual(manifest['baseFirmware'], {'name': 'CrossMux', 'version': '1.5.8'})
            self.assertEqual(manifest['gitCommit'], 'a' * 40)
            self.assertFalse(manifest['sourceDirty'])
            self.assertEqual(
                manifest['firmware']['sha256'], hashlib.sha256(firmware.read_bytes()).hexdigest()
            )

            checksums = (output / 'SHA256SUMS.txt').read_text()
            self.assertIn('  firmware.bin\n', checksums)
            self.assertIn('  iGAgentOS-X3-v0.1.0.bin\n', checksums)
            self.assertIn('  iGAgentOS-X3-v0.1.0-manifest.json\n', checksums)

    def test_rejects_firmware_without_matching_brand_and_version(self):
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            firmware = root / 'firmware.bin'
            self.write_firmware(firmware)
            image = firmware.read_bytes().replace(b'0.1.0', b'0.2.0')
            firmware.write_bytes(image)
            (root / 'platformio.ini').write_text(
                '[crosspoint]\nversion = 1.5.8\n'
                '[igagentos]\nname = iGAgentOS\nversion = 0.1.0\n'
            )

            with self.assertRaisesRegex(SystemExit, 'does not embed iGAgentOS 0.1.0'):
                package_igagentos_x3.package(
                    root=root,
                    firmware=firmware,
                    output_root=root / 'packages',
                    package_date='20260831',
                    git_commit='b' * 40,
                )


if __name__ == '__main__':
    unittest.main()
