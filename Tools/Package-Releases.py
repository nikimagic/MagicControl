"""Package successful GUI builds; private mctl stays in working portable only.

Run from Windows with Python 3 and WSL Ubuntu-26.04 available for Linux strip/ABI.
The CMake portable stagers own the required binary dependency list.
Operator checklist: docs/APP-UPDATES.md, section "Инструкция упаковщику".
"""
from pathlib import Path
import argparse
import datetime
import hashlib
import json
import os
import re
import subprocess
import tarfile
import zipfile
from ReleaseChecksums import apply as apply_checksums, plan as plan_checksums
from ReleasePackageAssets import canonical_resources, stage_canonical_text

ROOT = Path(__file__).resolve().parents[1]
RELEASES = ROOT / 'GitHub_Releases'
CMAKE_WINDOWS = Path('C:/Program Files/Microsoft Visual Studio/18/Community/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe')


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def linux_path(path):
    if os.name != 'nt':
        return str(path)
    return subprocess.check_output(['wsl.exe', '-d', 'Ubuntu-26.04', '--exec', 'wslpath', '-u', path.as_posix()], text=True).strip()


def linux_run(args):
    command = ['wsl.exe', '-d', 'Ubuntu-26.04', '--exec'] + args if os.name == 'nt' else args
    return subprocess.check_output(command, text=True)


def verified_build():
    session = json.loads((ROOT / 'build/product-build.json').read_text(encoding='utf-8-sig'))
    if session.get('status') != 'complete' or not session.get('application_inputs'):
        raise RuntimeError('Finish a common build with captured application inputs before packaging')
    spec = json.loads((ROOT / 'cmake/ApplicationInputs.json').read_text())
    paths = {ROOT / name for name in spec['files']}
    for tree in spec['trees']:
        paths.update(p for p in (ROOT / tree).rglob('*') if p.is_file())
    current = {p.relative_to(ROOT).as_posix(): sha(p) for p in paths}
    if current != session['application_inputs']:
        raise RuntimeError('Application inputs differ from the successful build; rebuild BOTH platforms')
    # A build may be committed after compilation. Claim the new exact revision
    # only when every captured input is tracked and clean against that HEAD.
    # Otherwise retain the build's original base revision/dirty provenance.
    roots = spec['trees'] + spec['files']
    tracked = subprocess.check_output(['git', 'ls-files', '-z', '--', *roots], cwd=ROOT).decode('utf-8').split('\0')
    dirty = subprocess.check_output(['git', 'status', '--porcelain', '--untracked-files=all', '--', *roots], cwd=ROOT)
    if not dirty.strip() and set(filter(None, tracked)) == set(current):
        session['source_revision'] = subprocess.check_output(['git', 'rev-parse', 'HEAD'], cwd=ROOT, text=True).strip()
        session['source_dirty'] = False
    else:
        # Ignored binary inputs are not represented by the source-only HEAD.
        session['source_dirty'] = True
    return session


def package(platform, portable_root=None):
    session = verified_build()
    # Separate staging leaves a running user's EXE and settings untouched.
    portable = (portable_root or ROOT / 'MagicControl') / platform
    version = (portable / 'build-version.txt').read_text(encoding='utf-8-sig').strip()
    if not re.fullmatch(r'0\.1\.\d+', version):
        raise RuntimeError('Invalid portable version: ' + version)
    generated = ROOT / 'build' / platform / 'generated/MagicControl/Version.h.version'
    if generated.read_text().strip() != version:
        raise RuntimeError('Portable is older than the last build attempt; stage a successful build first')
    binary_name = 'MagicControl.exe' if platform == 'windows' else 'MagicControl'
    built = ROOT / ('build/windows/bin/Release/MagicControl.exe' if platform == 'windows' else 'build/linux/bin/MagicControl')
    if sha(built) != sha(portable / binary_name):
        raise RuntimeError('Portable GUI differs from the successful build')
    folder = RELEASES / f'MagicControl-{version}-{platform}-x64-portable'
    archive = folder.with_name(folder.name + ('.zip' if platform == 'windows' else '.tar.gz'))
    if folder.exists() or folder.is_symlink() or archive.exists() or archive.is_symlink():
        raise RuntimeError(f'{folder} or {archive} already exists; existing releases are never overwritten')
    # Fail before staging when a required canonical license/default is absent.
    canonical_resources(ROOT, platform)
    folder.mkdir()
    # User-visible, empty inbox shipped in BOTH platform archives. Never copy
    # live update staging, rollback data or hidden predecessor directories.
    update_inbox = folder / 'magiccontrol-update'
    update_inbox.mkdir()
    # Explicit canonical defaults and licenses only; never take live settings.
    stage_canonical_text(ROOT, platform, folder)
    if platform == 'linux':
        # Preserve a valid shebang if a Windows checkout converted line endings.
        script = folder / 'start.sh'
        script.write_bytes(script.read_bytes().replace(b'\r\n', b'\n'))
    if platform == 'windows':
        subprocess.run([str(CMAKE_WINDOWS), '-DCONFIG=Release', f'-DEXECUTABLE={built}',
                        f'-DDESTINATION={folder}', '-DSTATIC_CRT=TRUE',
                        f'-DVERSION_FILE={generated}',
                        f'-DPAWNIO_RUNTIME={ROOT / "src/platform/windows/runtime/PawnIO"}',
                        '-P', str(ROOT / 'cmake/StagePortableWindows.cmake')], check=True)
    else:
        # The canonical stager owns fonts, notices and the pinned NVFlash payload
        # too. Copying just the old GUI dependency list loses newer readers.
        linux_run(['cmake', '-DCONFIG=Release', f'-DGUI_EXECUTABLE={linux_path(built)}',
                   f'-DDESTINATION={linux_path(folder)}',
                   f'-DVERSION_FILE={linux_path(generated)}',
                   f'-DLAYER_LIBRARY={linux_path(portable / "libgtk4-layer-shell.so.0")}',
                   '-P', linux_path(ROOT / 'cmake/StagePortableLinux.cmake')])
        abi = []
        for name in [binary_name]:
            # Only our copied product binaries; never strip the pinned vendor ELF.
            linux_run(['strip', '--strip-unneeded', linux_path(folder / name)])
            abi.append('=== ' + name + ' ===\n' + linux_run(['readelf', '--version-info', linux_path(folder / name)]))
        (folder / 'linux-abi.txt').write_text('\n'.join(abi), encoding='utf-8')
    readme_path = folder / 'README.txt'
    readme = readme_path.read_text(encoding='utf-8-sig').replace('MagicControl 0.1.0', 'MagicControl ' + version)
    if platform == 'windows':
        readme += '\nДрайверы и модули встроены в EXE; внешние папки drivers и PawnIO не нужны.\n'
    readme += '\nВерсия в подвале открывает проверку и установку обновлений с GitHub.\n'
    readme_path.write_text(readme, encoding='utf-8')
    revision = session['source_revision']
    source_dirty = session['source_dirty']
    info = dict(product='MagicControl', version=version, publisher='NikiMagic',
                packaged_date=datetime.datetime.now(datetime.timezone.utc).date().isoformat(),
                platform=platform, architecture='x86_64', distribution='portable binary preview',
                magiccontrol_source_included=False, source_revision=revision, source_revision_is_base=source_dirty,
                source_binary_sha256=sha(portable / binary_name),
                cli_included=False,
                includes_uncommitted_source_changes=source_dirty, linux_symbols_stripped=(platform == 'linux'))
    (folder / 'build-info.json').write_text(json.dumps(info, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    entries = [dict(path=p.relative_to(folder).as_posix(), bytes=p.stat().st_size, sha256=sha(p))
               for p in sorted(folder.rglob('*')) if p.is_file()]
    manifest = dict(product='MagicControl', version=version, platform=platform, publisher='NikiMagic',
                    magiccontrol_source_included=False, files=entries)
    (folder / 'package-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
    if platform == 'windows':
        with zipfile.ZipFile(archive, 'w', zipfile.ZIP_DEFLATED, compresslevel=9) as packed:
            packed.write(folder, 'MagicControl/')
            packed.write(update_inbox, 'MagicControl/magiccontrol-update/')
            for path in sorted(folder.rglob('*')):
                if path.is_file():
                    packed.write(path, (Path('MagicControl') / path.relative_to(folder)).as_posix())
    else:
        def attributes(info):
            info.uid = info.gid = 0
            info.uname = info.gname = ''
            info.mode = 0o755 if info.isdir() or Path(info.name).name in ('MagicControl', 'start.sh', 'nvflash') else 0o644
            return info
        with tarfile.open(archive, 'w:gz') as packed:
            packed.add(folder, arcname='MagicControl', filter=attributes)
    print(f'{platform}: {version}: {archive.name}')
    expected = {(Path('MagicControl') / p.relative_to(folder)).as_posix(): p for p in folder.rglob('*') if p.is_file()}
    if any(Path(name).name in ('mctl', 'mctl.exe', 'rom_id', 'platform_id', 'bios-inventory.json', 'device-settings.ini', 'battery-enforcement-platform.txt')
           or any(part.casefold() == 'logs' or part.casefold().startswith('logs_') for part in Path(name).parts[:-1])
           or (platform == 'windows' and Path(name).parts[1].casefold() in ('drivers', 'pawnio'))
           or name.endswith('.pdb') for name in expected):
        raise RuntimeError('Unexpected runtime/developer file in release')
    if platform == 'windows':
        with zipfile.ZipFile(archive) as packed:
            inbox_name = 'MagicControl/magiccontrol-update/'
            if set(packed.namelist()) != set(expected) | {'MagicControl/', inbox_name} or not packed.getinfo(inbox_name).is_dir() or not packed.getinfo('MagicControl/').is_dir():
                raise RuntimeError('ZIP file list differs from package')
            if any(name.startswith(inbox_name) and name != inbox_name for name in packed.namelist()):
                raise RuntimeError('ZIP update inbox is not empty')
            for name, path in expected.items():
                if packed.read(name) != path.read_bytes():
                    raise RuntimeError('ZIP content differs: ' + name)
    else:
        with tarfile.open(archive) as packed:
            inbox_name = 'MagicControl/magiccontrol-update'
            if not packed.getmember(inbox_name).isdir() or any(m.name.startswith(inbox_name + '/') for m in packed.getmembers()):
                raise RuntimeError('TAR update inbox missing or not empty')
            entries = [m for m in packed.getmembers() if m.isfile()]
            if {m.name for m in entries} != set(expected):
                raise RuntimeError('TAR file list differs from package')
            for member in entries:
                if packed.extractfile(member).read() != expected[member.name].read_bytes():
                    raise RuntimeError('TAR content differs: ' + member.name)
    return archive


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--platform', choices=['windows', 'linux', 'all'], default='all')
    parser.add_argument('--portable-root', type=Path,
                        help='Alternate staging root containing windows/ and/or linux/')
    operations = parser.add_mutually_exclusive_group()
    operations.add_argument('--prune-verified-staging', choices=['preview', 'apply'],
                            help='Remove older staging folders only after byte-for-byte archive verification')
    operations.add_argument('--reconcile-checksums', choices=['preview', 'apply'],
                            help='Verify retained release archives and audit missing checksum entries')
    args = parser.parse_args()
    if args.prune_verified_staging:
        from ReleaseStagingCleanup import prune
        prune(RELEASES, ROOT / 'build/product-version.txt',
              ROOT / 'build/release-staging-cleanup.csv',
              preview=args.prune_verified_staging == 'preview')
        raise SystemExit(0)
    audit_dir=ROOT / 'research' / 'release-checksum-audit'
    if args.reconcile_checksums:
        prepared=plan_checksums(RELEASES)
        print(f"Verified archives: {len(prepared['current'])}; missing historical entries: {len(prepared['missing'])}")
        if args.reconcile_checksums == 'apply':
            evidence=apply_checksums(prepared,audit_dir)
            print('Checksum reconciliation:', evidence if evidence else 'unchanged')
        raise SystemExit(0)
    verified_build()  # No partial release directory before provenance checks.
    selected = ['windows', 'linux'] if args.platform == 'all' else [args.platform]
    product_version = ROOT / 'build/product-version.txt'
    if product_version.exists():
        # Preparation is still an explicit user action. Do not publish one half
        # of an unfinished common build or label different binaries as one release.
        expected = product_version.read_text(encoding='utf-8-sig').strip()
        session = json.loads((ROOT / 'build/product-build.json').read_text(encoding='utf-8-sig'))
        if session.get('status') != 'complete' or session.get('version') != expected:
            raise RuntimeError('Finish the common Windows/Linux build before preparing release archives')
        portable_root = args.portable_root or ROOT / 'MagicControl'
        for platform in selected:
            portable = portable_root / platform
            binary = portable / ('MagicControl.exe' if platform == 'windows' else 'MagicControl')
            if (portable / 'build-version.txt').read_text(encoding='utf-8-sig').strip() != expected:
                raise RuntimeError('Portable version differs from the common product version: ' + platform)
            if sha(binary).lower() != session['platforms'][platform]['sha256'].lower():
                raise RuntimeError('Portable differs from the verified common build: ' + platform)
    if 'linux' in selected:
        linux_run(['/usr/bin/true'])  # Fail before creating any output when WSL is unavailable.
    for platform in selected:
        canonical_resources(ROOT,platform)
    plan_checksums(RELEASES)  # Reject unexpected/modified archives before staging.
    archives = [package(platform, args.portable_root) for platform in selected]
    # Existing files must match their recorded hashes. Missing historical
    # entries remain in the append-only audit; only just-created archives may
    # be added without a prior hash. No archive is deleted or downloaded.
    prepared=plan_checksums(RELEASES, [archive.name for archive in archives])
    apply_checksums(prepared,audit_dir)
