"""Offline release fixtures: preserve notices and refuse changed archives."""
import hashlib
import json
from pathlib import Path
import re
import shutil
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Tools'))
from ReleaseChecksums import apply, is_link, plan  # noqa: E402
from ReleasePackageAssets import canonical_resources, stage_canonical_text  # noqa: E402


class ReleasePackageAssetsTest(unittest.TestCase):
    def setUp(self):
        out = ROOT / 'out'
        out.mkdir(exist_ok=True)
        if is_link(out) or out.resolve().parent != ROOT.resolve():
            raise RuntimeError('fixture output parent is linked or outside the project')
        self.scratch = out / 'release-package-fixtures'
        self.scratch.mkdir(exist_ok=True)
        if is_link(self.scratch) or self.scratch.resolve().parent != out.resolve():
            raise RuntimeError('fixture directory is linked or outside output')
        self.base = Path(tempfile.mkdtemp(prefix='fixture-', dir=self.scratch))

    def tearDown(self):
        out = ROOT.resolve() / 'out'
        expected = out / 'release-package-fixtures'
        if is_link(out) or is_link(self.scratch) or out.resolve(strict=True) != out or self.scratch.resolve(strict=True) != expected:
            raise RuntimeError('fixture cleanup parent is linked or outside output')
        resolved = self.base.resolve(strict=True)
        if is_link(self.base) or resolved.parent != expected or not resolved.name.startswith('fixture-'):
            raise RuntimeError('unsafe fixture cleanup target')
        shutil.rmtree(resolved)
        try:
            self.scratch.rmdir()
        except OSError:
            pass

    def test_both_platforms_copy_canonical_text_without_overwrite(self):
        for platform in ('windows', 'linux'):
            folder = self.base / platform
            folder.mkdir()
            resources = canonical_resources(ROOT, platform)
            stage_canonical_text(ROOT, platform, folder)
            for source, target in resources:
                self.assertEqual((folder / target).read_bytes(), source.read_bytes())
            marker = folder / resources[0][1]
            before = marker.read_bytes()
            with self.assertRaises(ValueError):
                stage_canonical_text(ROOT, platform, folder)
            self.assertEqual(marker.read_bytes(), before)
        with self.assertRaises(ValueError):
            canonical_resources(ROOT, 'unknown')

    def test_both_ui_resources_use_the_packaged_license(self):
        expected = (ROOT / 'packaging' / 'LICENSE.txt').resolve(strict=True)
        resources = ROOT / 'src' / 'gui' / 'assets' / 'magiccontrol-icons.gresource.xml'
        aliases = [node.text for node in ET.parse(resources).iter('file') if node.get('alias') == 'license.txt']
        self.assertEqual(len(aliases), 1)
        self.assertEqual((resources.parent / aliases[0]).resolve(strict=True), expected)

        rc = (ROOT / 'src' / 'gui' / 'windows' / 'MagicControl.rc.in').read_text(encoding='utf-8')
        paths = re.findall(r'IDR_PROJECT_LICENSE\s+RCDATA\s+"@PROJECT_SOURCE_DIR@/([^"\r\n]+)"', rc)
        self.assertEqual(len(paths), 1)
        self.assertEqual((ROOT / paths[0]).resolve(strict=True), expected)
        inputs = json.loads((ROOT / 'cmake' / 'ApplicationInputs.json').read_text(encoding='utf-8'))
        self.assertIn('packaging/LICENSE.txt', inputs['files'])

    def test_checksum_reconciliation_preserves_archives_and_rejects_changes(self):
        releases = self.base / 'GitHub_Releases'
        releases.mkdir()
        first = releases / 'MagicControl-0.1.1-windows-x64-portable.zip'
        second = releases / 'MagicControl-0.1.2-linux-x64-portable.tar.gz'
        missing = 'MagicControl-0.1.3-windows-x64-portable.zip'
        first.write_bytes(b'first fake archive')
        second.write_bytes(b'second fake archive')
        old = (f'{hashlib.sha256(first.read_bytes()).hexdigest()}  {first.name}\n'
               f'{"0" * 64}  {missing}\n').encode()
        sums = releases / 'SHA256SUMS.txt'
        sums.write_bytes(old)
        with self.assertRaises(ValueError):
            plan(releases)  # Unlisted archives cannot be silently adopted.
        prepared = plan(releases, allowed_new=[second.name])
        self.assertEqual(prepared['missing'], [missing])
        evidence = apply(prepared, self.base / 'audit')
        self.assertEqual(Path(evidence['before']).read_bytes(), old)
        self.assertNotIn(missing.encode(), sums.read_bytes())
        self.assertEqual(first.read_bytes(), b'first fake archive')
        self.assertEqual(second.read_bytes(), b'second fake archive')
        prepared = plan(releases)
        before = sums.read_bytes()
        first.write_bytes(b'changed fake archive')
        with self.assertRaises(ValueError):
            apply(prepared, self.base / 'audit')
        self.assertEqual(sums.read_bytes(), before)
        with self.assertRaises(ValueError):
            plan(releases)


if __name__ == '__main__':
    unittest.main()
