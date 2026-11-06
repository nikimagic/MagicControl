"""Safely prune reproducible historical release staging, retaining archives."""

from pathlib import Path, PurePosixPath
import csv
import hashlib
import os
import re
import shutil
import stat
import tarfile
import zipfile


NAME = re.compile(r"MagicControl-0\.1\.(\d+)-(windows|linux)-x64-portable\Z")
PROTECTED_DIR = re.compile(r"(?i)^(?:logs?(?:_.*)?|dumps?|bios-dumps|ec-dumps|vbios-dumps|backups?)$")
PROTECTED_FILE = re.compile(r"(?i)^(?:device-settings\.ini|keys-settings\.ini|battery_id\.txt|project_id|platform_id|rom_id)$|\.(?:jsonl|log|rom|fd|dmp)$")


def digest(stream):
    result = hashlib.sha256()
    while chunk := stream.read(1024 * 1024):
        result.update(chunk)
    return result.hexdigest()


def is_link(path):
    info = path.lstat()
    return stat.S_ISLNK(info.st_mode) or bool(getattr(info, 'st_file_attributes', 0) & 0x400)


def signature(path):
    info = path.stat()
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns


def inspect_tree(folder):
    if is_link(folder):
        raise ValueError('staging root is a symlink or reparse point')
    files, dirs, folded = {}, {}, set()
    def raise_walk_error(error):
        raise error
    for parent, child_dirs, child_files in os.walk(folder, followlinks=False, onerror=raise_walk_error):
        for name in child_dirs:
            path = Path(parent) / name
            if is_link(path) or PROTECTED_DIR.fullmatch(name):
                raise ValueError('linked or protected directory: ' + str(path))
            relative = path.relative_to(folder).as_posix()
            if relative.casefold() in folded:
                raise ValueError('case-colliding staging paths: ' + relative)
            folded.add(relative.casefold())
            dirs[relative] = signature(path)
        for name in child_files:
            path = Path(parent) / name
            if is_link(path) or PROTECTED_FILE.search(name):
                raise ValueError('linked or protected file: ' + str(path))
            relative = path.relative_to(folder).as_posix()
            if relative.casefold() in folded:
                raise ValueError('case-colliding staging paths: ' + relative)
            folded.add(relative.casefold())
            files[relative] = (path, signature(path))
    return files, dirs


def archive_relative(name, folder):
    parts = PurePosixPath(name).parts
    if not parts or parts[0] not in {'MagicControl', folder.name}:
        raise ValueError('unexpected archive root: ' + name)
    if name.startswith('/') or '\\' in name or any(p in {'.', '..'} for p in parts):
        raise ValueError('unsafe archive path: ' + name)
    return '/'.join(parts[1:])


def verify_staging(folder, archive, expected_hash):
    if is_link(archive):
        raise ValueError('archive is a symlink or reparse point')
    archive_signature = signature(archive)
    with archive.open('rb') as stream:
        if digest(stream) != expected_hash:
            raise ValueError('archive SHA-256 differs from SHA256SUMS')
    files, directories = inspect_tree(folder)
    seen, archive_dirs = set(), set()
    count_bytes = 0

    def remember_directories(relative, is_directory):
        parts = PurePosixPath(relative).parts
        limit = len(parts) if is_directory else len(parts) - 1
        for index in range(1, limit + 1):
            archive_dirs.add('/'.join(parts[:index]))

    def check(name, length, stream):
        nonlocal count_bytes
        relative = archive_relative(name, folder)
        if not relative or relative in seen or relative not in files:
            raise ValueError('extra or duplicate archive file: ' + name)
        path, before = files[relative]
        remember_directories(relative, False)
        if before[2] != length:
            raise ValueError('size differs: ' + relative)
        with path.open('rb') as local:
            if digest(local) != digest(stream):
                raise ValueError('content differs: ' + relative)
        seen.add(relative)
        count_bytes += length

    if archive.suffix == '.zip':
        with zipfile.ZipFile(archive) as packed:
            for entry in packed.infolist():
                relative = archive_relative(entry.filename, folder)
                kind = stat.S_IFMT(entry.external_attr >> 16)
                if kind not in (0, stat.S_IFREG, stat.S_IFDIR):
                    raise ValueError('archive contains non-regular entry: ' + entry.filename)
                if kind and kind != (stat.S_IFDIR if entry.is_dir() else stat.S_IFREG):
                    raise ValueError('archive type differs from name: ' + entry.filename)
                if entry.is_dir():
                    remember_directories(relative, True)
                    continue
                with packed.open(entry) as stream:
                    check(entry.filename, entry.file_size, stream)
    else:
        with tarfile.open(archive, mode='r|gz') as packed:
            for entry in packed:
                relative = archive_relative(entry.name, folder)
                expected_mode = 0o755 if entry.isdir() or PurePosixPath(entry.name).name in {'MagicControl', 'start.sh', 'nvflash'} else 0o644
                if entry.mode & 0o7777 != expected_mode:
                    raise ValueError('archive mode differs from release policy: ' + entry.name)
                if entry.isdir():
                    remember_directories(relative, True)
                    continue
                if not entry.isfile():
                    raise ValueError('archive contains non-file: ' + entry.name)
                stream = packed.extractfile(entry)
                if stream is None:
                    raise ValueError('archive member unavailable: ' + entry.name)
                with stream:
                    check(entry.name, entry.size, stream)
    if seen != set(files):
        raise ValueError('staging contains files not present in archive')
    if archive_dirs != set(directories):
        raise ValueError('staging and archive directory sets differ')
    files_after, dirs_after = inspect_tree(folder)
    if files_after != files or dirs_after != directories or signature(archive) != archive_signature:
        raise ValueError('staging or archive changed during verification')
    return count_bytes, len(files), files, directories, archive_signature


def prune(releases, product_version, report_path, *, preview):
    root = releases.resolve(strict=True)
    if is_link(releases):
        raise ValueError('release root must not be a symlink or reparse point')
    version_text = product_version.read_text(encoding='utf-8-sig').strip()
    version = re.fullmatch(r'0\.1\.(\d+)', version_text)
    if not version or int(version.group(1)) == 0:
        raise ValueError('current version is missing or invalid')
    current = int(version.group(1))
    sums = {}
    for line in (root / 'SHA256SUMS.txt').read_text(encoding='utf-8-sig').splitlines():
        match = re.fullmatch(r'([a-fA-F0-9]{64})  (.+)', line)
        if match:
            sums[match.group(2)] = match.group(1).lower()
    report_path.parent.mkdir(parents=True, exist_ok=True)
    if report_path.exists() and is_link(report_path):
        raise ValueError('cleanup report must not be a symlink or reparse point')

    def record(item):
        existing = report_path.exists() and report_path.stat().st_size > 0
        with report_path.open('a', encoding='utf-8' if existing else 'utf-8-sig', newline='') as output:
            writer = csv.DictWriter(output, fieldnames=('folder', 'archive', 'bytes', 'files', 'result', 'detail'))
            if not existing:
                writer.writeheader()
            writer.writerow(item)
            output.flush()
            os.fsync(output.fileno())

    entries = []
    for folder in sorted(root.iterdir()):
        match = NAME.fullmatch(folder.name)
        if not match or not folder.is_dir() or not 0 < int(match.group(1)) < current:
            continue
        archive = root / (folder.name + ('.zip' if match.group(2) == 'windows' else '.tar.gz'))
        item = {'folder': folder.name, 'archive': archive.name, 'bytes': 0, 'files': 0, 'result': 'skipped', 'detail': ''}
        try:
            if archive.name not in sums:
                raise ValueError('archive absent from SHA256SUMS')
            if folder.resolve(strict=True) != root / folder.name:
                raise ValueError('staging does not resolve directly under release root')
            item['bytes'], item['files'], verified_files, verified_dirs, archive_signature = verify_staging(folder, archive, sums[archive.name])
            if preview:
                item['result'] = 'verified'
            else:
                if product_version.read_text(encoding='utf-8-sig').strip() != version_text:
                    raise ValueError('current version changed during cleanup')
                # Resolve again immediately before recursive deletion and refuse
                # symlinks/junctions anywhere in the staging tree.
                if folder.resolve(strict=True) != root / folder.name or is_link(folder):
                    raise ValueError('staging path changed during cleanup')
                files_now, dirs_now = inspect_tree(folder)
                if files_now != verified_files or dirs_now != verified_dirs or signature(archive) != archive_signature:
                    raise ValueError('staging or archive changed after verification')
                item['result'] = 'removing'
                record(item)  # Durable evidence before changing the filesystem.
                shutil.rmtree(folder)
                item['result'] = 'removed'
        except (OSError, ValueError, KeyError, zipfile.BadZipFile, tarfile.TarError) as exc:
            if item['result'] == 'removing':
                item['result'] = 'failed_partial'
            item['detail'] = str(exc)
        entries.append(item)
        record(item)
        if len(entries) % 10 == 0:
            print(f"{len(entries)} staging folders checked", flush=True)
    state = 'verified' if preview else 'removed'
    selected = [entry for entry in entries if entry['result'] == state]
    print(f"{state}: {len(selected)}/{len(entries)} folders; {sum(e['bytes'] for e in selected) / 1024**3:.2f} GiB")
    print('Report:', report_path)
