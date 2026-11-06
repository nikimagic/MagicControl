"""Offline safety checks for historical release-staging cleanup."""
import csv
import hashlib
import io
from pathlib import Path
import shutil
import stat
import sys
import tarfile
import tempfile
import unittest
import zipfile


ROOT = Path(__file__).resolve().parents[2]
sys.path.insert(0, str(ROOT / 'Tools'))
from ReleaseStagingCleanup import is_link, prune  # noqa: E402


class ReleaseStagingCleanupTest(unittest.TestCase):
    def setUp(self):
        self.scratch = ROOT / 'build' / 'release-prune-fixtures'
        self.scratch.mkdir(parents=True, exist_ok=True)
        if is_link(self.scratch) or self.scratch.resolve().parent != (ROOT / 'build').resolve():
            raise RuntimeError('fixture parent is a symlink')
        self.base = Path(tempfile.mkdtemp(prefix='fixture-', dir=self.scratch))
        self.releases = self.base / 'GitHub_Releases'
        self.releases.mkdir()
        self.version = self.base / 'product-version.txt'
        self.version.write_text('0.1.12', encoding='utf-8')
        self.report = self.base / 'report.csv'
        self.sums = {}

    def tearDown(self):
        resolved = self.base.resolve(strict=True)
        if resolved.parent != self.scratch.resolve(strict=True) or not resolved.name.startswith('fixture-') or is_link(self.base):
            raise RuntimeError('unsafe fixture cleanup target')
        shutil.rmtree(resolved)
        try:
            self.scratch.rmdir()  # Only succeeds when no other fixture is active.
        except OSError:
            pass

    def release(self, version, platform='windows', files=None, bad_tar_mode=False, disguised_link=False):
        files = files or {'MagicControl.exe': b'application', 'settings/app-settings.ini': b'defaults'}
        folder = self.releases / f'MagicControl-0.1.{version}-{platform}-x64-portable'
        folder.mkdir()
        for name, content in files.items():
            path = folder / name
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(content)
        archive = folder.with_name(folder.name + ('.zip' if platform == 'windows' else '.tar.gz'))
        if platform == 'windows':
            with zipfile.ZipFile(archive, 'w') as packed:
                for name, content in files.items():
                    packed.writestr('MagicControl/' + name, content)
                if disguised_link:
                    entry = zipfile.ZipInfo('MagicControl/disguised-link/')
                    entry.create_system = 3
                    entry.external_attr = (stat.S_IFLNK | 0o777) << 16
                    packed.writestr(entry, b'target')
        else:
            with tarfile.open(archive, 'w:gz') as packed:
                for name, content in files.items():
                    info = tarfile.TarInfo('MagicControl/' + name)
                    info.size = len(content)
                    info.mode = 0o755 if Path(name).name in {'MagicControl', 'start.sh', 'nvflash'} and not bad_tar_mode else 0o644
                    packed.addfile(info, io.BytesIO(content))
        self.sums[archive.name] = hashlib.sha256(archive.read_bytes()).hexdigest()
        return folder, archive

    def write_sums(self):
        (self.releases / 'SHA256SUMS.txt').write_text(
            ''.join(f'{value}  {name}\n' for name, value in sorted(self.sums.items())), encoding='utf-8')

    def results(self):
        with self.report.open(encoding='utf-8-sig', newline='') as stream:
            return {row['folder']: row for row in csv.DictReader(stream)}

    def test_preview_and_prune_preserve_archives_and_refuse_personal_or_changed_data(self):
        verified, archive = self.release(1)
        changed, _ = self.release(2)
        (changed / 'MagicControl.exe').write_bytes(b'user-changed')
        protected, _ = self.release(3, files={'MagicControl.exe': b'app', 'logs_GM6/log.jsonl': b'personal log'})
        bad_hash, bad_archive = self.release(4)
        self.sums[bad_archive.name] = '0' * 64
        linux, linux_archive = self.release(5, 'linux', {'MagicControl': b'linux binary'})
        changed_case, _ = self.release(6, files={'MixedName.bin': b'same bytes, different path'})
        (changed_case / 'MixedName.bin').rename(changed_case / 'rename-temporary')
        (changed_case / 'rename-temporary').rename(changed_case / 'mixedname.bin')
        extra_directory, _ = self.release(7)
        (extra_directory / 'empty-user-folder').mkdir()
        bad_mode, _ = self.release(9, 'linux', {'MagicControl': b'linux binary'}, bad_tar_mode=True)
        disguised_link, _ = self.release(10, disguised_link=True)
        template, _ = self.release(0)
        current, _ = self.release(12)
        self.write_sums()
        original_archive = hashlib.sha256(archive.read_bytes()).hexdigest()
        original_linux_archive = hashlib.sha256(linux_archive.read_bytes()).hexdigest()

        prune(self.releases, self.version, self.report, preview=True)
        preview = self.results()
        self.assertEqual(preview[verified.name]['result'], 'verified')
        self.assertEqual(preview[linux.name]['result'], 'verified')
        self.assertEqual(preview[changed.name]['result'], 'skipped')
        self.assertEqual(preview[protected.name]['result'], 'skipped')
        self.assertEqual(preview[bad_hash.name]['result'], 'skipped')
        self.assertEqual(preview[changed_case.name]['result'], 'skipped')
        self.assertEqual(preview[extra_directory.name]['result'], 'skipped')
        self.assertEqual(preview[bad_mode.name]['result'], 'skipped')
        self.assertEqual(preview[disguised_link.name]['result'], 'skipped')
        self.assertTrue(verified.exists())
        preview_report = self.report.read_bytes()

        prune(self.releases, self.version, self.report, preview=False)
        self.assertTrue(self.report.read_bytes().startswith(preview_report), 'existing cleanup audit must be retained')
        with self.report.open(encoding='utf-8-sig', newline='') as stream:
            stages = [row['result'] for row in csv.DictReader(stream) if row['folder'] == verified.name]
        self.assertEqual(stages, ['verified', 'removing', 'removed'])
        self.assertFalse(verified.exists())
        self.assertFalse(linux.exists())
        self.assertTrue((protected / 'logs_GM6' / 'log.jsonl').is_file())
        self.assertEqual((changed / 'MagicControl.exe').read_bytes(), b'user-changed')
        self.assertTrue(bad_hash.is_dir())
        self.assertEqual((changed_case / 'mixedname.bin').read_bytes(), b'same bytes, different path')
        self.assertTrue((extra_directory / 'empty-user-folder').is_dir())
        self.assertTrue(bad_mode.is_dir())
        self.assertTrue(disguised_link.is_dir())
        self.assertTrue(template.is_dir())
        self.assertTrue(current.is_dir())
        self.assertEqual(hashlib.sha256(archive.read_bytes()).hexdigest(), original_archive)
        self.assertEqual(hashlib.sha256(linux_archive.read_bytes()).hexdigest(), original_linux_archive)


if __name__ == '__main__':
    unittest.main()
