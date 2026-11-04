# VBIOS reader lifetime0.1.195

Статическая проверка после0.1.194 выявила, что Linux fork-child наследует
сеанс/процессную группу SSH и может получить терминальный SIGINT вместе с
mctl. Теперь перед exec выполняется setsid, child не имеет controlling TTY,
stdin=/dev/null. Raw output FDs нормализуются выше0/1/2. Сохраняются только
стандартные потоки и capture flock FD с очищенным CLOEXEC. При гибели frontend
эта блокировка остаётся у vendor child до его выхода. Прошивальные команды,
подтверждения, modprobe/rmmod или сигналы reader не добавлялись.

Windows reader наследует semaphore handle через существующий whitelist;
родительские flags восстанавливаются сразу после CreateProcess. AFU/FPT при
пустой NVFlash lease наследуют прежние3 std handle. Pending waiter обеих ОС
через aliasing shared_ptr удерживает native lease и DeviceOperation gate до
выхода reader, включая timeout10мин. UI/геометрия/скролл и runtime bytes прежние.

Build-All завершил общий0.1.195; обе рабочие portable GUI+mctl обновлены.
552 application inputs совпали с receipt. Все4 binaries побайтно равны build;
версии/PE subsystem/asInvoker/ELF strings проверены по файлам. Все47 embedded
payload в каждом Windows EXE совпали с SHA/размерами каталога; Linux NVFlash и
notices совпали с источником. Legacy BIOS collector selection/capture body и
validator совпали с0.1.193. `git diff --check` изменённых src/docs прошёл.

[verification.json](verification.json), file-only checker рядом. Это проверка
сборки/артефактов и статический разбор lifetime; реальных SSH interrupts,
GUI/CLI запусков, NVFlash/driver/hardware или тестов помощник не выполнял.
Результаты аппаратного VBIOS dump в этом сеансе отсутствуют.
