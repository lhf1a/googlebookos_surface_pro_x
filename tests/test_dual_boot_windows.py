"""Static checks for the Windows dual-boot scripts (PS 5.1 / C# 4 compatibility)."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
PS1 = ROOT / 'tools/Install-GooglebookDualBoot.ps1'
CMD = ROOT / 'tools/Install-GooglebookDualBoot.cmd'
PS1_TEST = ROOT / 'tests/Test-GooglebookDualBoot.ps1'


class WindowsDualBootScripts(unittest.TestCase):
    def test_ps1_has_utf8_bom_and_crlf(self):
        for path in (PS1, PS1_TEST):
            raw = path.read_bytes()
            self.assertTrue(raw.startswith(b'\xef\xbb\xbf'), f'{path.name} must have UTF-8 BOM for PS 5.1')
            self.assertNotIn(b'\n', raw.replace(b'\r\n', b''), f'{path.name} must use CRLF')
            text = raw.decode('utf-8-sig')
            self.assertIn('#requires -Version 5.1', text)

    def test_cmd_is_pure_ascii_crlf_and_invokes_ps1(self):
        raw = CMD.read_bytes()
        raw.decode('ascii')
        self.assertNotIn(b'\n', raw.replace(b'\r\n', b''))
        text = raw.decode('ascii')
        self.assertIn('Install-GooglebookDualBoot.ps1', text)
        self.assertIn('-ExecutionPolicy Bypass', text)
        self.assertIn('RunAs', text)

    def test_ps1_avoids_ps7_and_modern_csharp_syntax(self):
        text = PS1.read_text(encoding='utf-8-sig')
        self.assertNotIn('ConvertFrom-Json -AsHashtable', text)
        self.assertNotIn(' ?? ', text)
        match = re.search(r"\$nativeCode\s*=\s*@'\r?\n(.*?)\r?\n'@", text, re.S)
        self.assertIsNotNone(match)
        csharp = match.group(1)
        self.assertNotIn('$"', csharp)
        self.assertNotIn('nameof(', csharp)
        self.assertNotIn('out var ', csharp)
        self.assertIn('LAYOUT_HEADER_BYTES = 48', csharp)
        self.assertIn('PARTITION_ENTRY_BYTES = 144', csharp)

    def test_dual_boot_grub_and_uefi_requirements_present(self):
        text = PS1.read_text(encoding='utf-8-sig')
        for token in (
            'IOCTL_DISK_GET_DRIVE_LAYOUT_EX = 0x00070050',
            'IOCTL_DISK_SET_DRIVE_LAYOUT_EX = 0x0007C054',
            'IOCTL_DISK_UPDATE_PROPERTIES = 0x00070140',
            'FSCTL_LOCK_VOLUME',
            'FSCTL_DISMOUNT_VOLUME',
            'bcdedit.exe',
            '{fwbootmgr}',
            '/EFI/Microsoft/Boot/bootmgfw.efi',
            '/EFI/SQ2Nat/start.efi',
            '/EFI/SQ2Res/start.efi',
            'SPX-IMAGE.txt',
            'UTF8Encoding]::new($false)',
        ):
            self.assertIn(token, text)


if __name__ == '__main__':
    unittest.main()
