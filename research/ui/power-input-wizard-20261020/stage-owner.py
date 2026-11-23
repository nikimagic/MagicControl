"""Create and inspect a clean local owner folder; no app, tests or GitHub action."""
from pathlib import Path
import hashlib
import json
import shutil
import subprocess
import uuid

ROOT = Path(__file__).resolve().parents[3]
state = json.loads((ROOT / 'build/product-build.json').read_text(encoding='utf-8-sig'))
assert state['status'] == 'complete'
assert all(state['platforms'][p]['status'] == 'verified' for p in ('windows', 'linux'))
version = state['version']
OUT = ROOT / 'out/owner-delivery' / ('run-' + uuid.uuid4().hex)
OUT.mkdir(parents=True, exist_ok=False)
dest = OUT / f'MagicControl-{version}-windows-x64-portable'
assert not dest.exists(), f'Refusing to overwrite {dest}'
cmake = Path(r'C:\Program Files\Microsoft Visual Studio\18\Community\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe')
binary = ROOT / 'build/windows/bin/Release/MagicControl.exe'
subprocess.run([str(cmake), '-DCONFIG=Release', f'-DEXECUTABLE={binary.as_posix()}',
                f'-DDESTINATION={dest.as_posix()}',
                f'-DPAWNIO_RUNTIME={(ROOT / "src/platform/windows/runtime/PawnIO").as_posix()}',
                '-DSTATIC_CRT=TRUE', f'-DVERSION_FILE={(ROOT / "build/product-version.txt").as_posix()}',
                '-P', str(ROOT / 'cmake/StagePortableWindows.cmake')], check=True)
shutil.copytree(ROOT / 'GitHub_Releases/MagicControl-0.1.0-windows-x64-portable/licenses', dest / 'licenses', dirs_exist_ok=True)
shutil.copytree(ROOT / 'packaging/windows/licenses', dest / 'licenses', dirs_exist_ok=True)
shutil.copy2(ROOT / 'GitHub_Releases/LICENSE.txt', dest / 'LICENSE.txt')
for name in ('THIRD-PARTY-NOTICES.txt', 'SOURCE_OFFER.txt'):
    shutil.copy2(ROOT / 'packaging/windows' / name, dest / name)
(dest / 'README.txt').write_text(f'''MagicControl {version} — сбор состояний питания GM6IR0C / HYDROC-16 G2

Откройте INFO → «Снять дамп» → «Проверка БП».
Программа последовательно предложит:
1. Отключить круглый БП и питание USB-C.
2. Подключить круглый БП при неполной батарее и дождаться реальной зарядки.
3. Дождаться 100% с подключённым круглым БП и прекращения зарядки батареи.

Каждый принятый этап собирает минимум минуту показаний. Следуйте подсказке
и не меняйте питание во время сбора. На этапе зарядки нужен положительный
ток/мощность, на последнем — полный заряд и почти нулевая мощность батареи.
Для последнего этапа при необходимости самостоятельно выберите верхний100%.

«Отложить» сохраняет уже собранные этапы. Чтобы продолжить, снова выберите
«Снять дамп» → «Проверка БП», не закрывая программу и не меняя профиль.
После перезапуска начинается новая серия; старые данные остаются на диске.
Пришлите всю папку logs_<плата>, включая power-input-* и неудачные попытки.

Сценарий читает данные. Он не переключает вентиляторы, пороги или режимы EC,
не разряжает батарею автоматически. Действующие фоновые контроллеры продолжают
работать. Подключение именно круглого штекера обеспечивает владелец:
одного признака внешнего питания Windows недостаточно для определения порта.

ВАЖНО: ограничение GM6 пока не исправлено. Лог0.1.98 подтвердил запись85/80,
но зарядка продолжалась выше85%. Новый сценарий нужен для поиска причины.
Кнопки100/95/85 показывают настройку, а не доказанную физическую отсечку.
Температура батареи из неподтверждённого преобразования EC не подставляется.
Установка драйверов и запуск аппаратных проб автоматически не выполняются.

Это чистая локальная portable-папка для владельца, не публикация GitHub.
''', encoding='utf-8')
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
info = {'product': 'MagicControl', 'version': version, 'platform': 'windows',
        'architecture': 'x86_64', 'purpose': 'Guided read-only power-input evidence collection',
        'source_revision': state['source_revision'], 'source_revision_is_base': True,
        'source_binary_sha256': sha(binary), 'source_fingerprint': state['source_fingerprint'],
        'includes_uncommitted_source_changes': state['source_dirty'],
        'distribution': 'Clean local portable folder; not a GitHub release'}
(dest / 'build-info.json').write_text(json.dumps(info, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
rows = [{'path': p.relative_to(dest).as_posix(), 'bytes': p.stat().st_size, 'sha256': sha(p)}
        for p in sorted(dest.rglob('*')) if p.is_file() and p.name != 'package-manifest.json']
manifest = {'product': 'MagicControl', 'version': version, 'platform': 'windows', 'files': rows}
(dest / 'package-manifest.json').write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
for row in rows:
    p = dest / row['path']
    assert p.stat().st_size == row['bytes'] and sha(p) == row['sha256']
assert sha(dest / 'MagicControl.exe') == sha(binary)
assert (dest / 'build-version.txt').read_text(encoding='utf-8-sig').strip() == version
assert sha(dest / 'app-settings.ini') == sha(ROOT / 'packaging/app-settings.ini')
assert sha(dest / 'fan-curves.csv') == sha(ROOT / 'packaging/fan-curves.csv')
for p in dest.rglob('*'):
    assert not p.name.startswith('logs') and p.suffix not in ('.pdb', '.jsonl', '.zip')
    assert p.name not in ('device-settings.ini', 'keyboard-cycle.ini', 'bios-inventory.json', 'mctl.exe')
report = {'version': version, 'folder': str(dest), 'manifest_verified_files': len(rows),
          'gui_sha256': sha(binary), 'clean_settings': True,
          'scope': 'Static file/manifest comparison; no app, tests or hardware execution'}
(OUT / f'owner-folder-{version}.json').write_text(json.dumps(report, ensure_ascii=False, indent=2) + '\n', encoding='utf-8')
print(json.dumps(report, ensure_ascii=False))
