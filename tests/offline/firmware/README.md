# Файловые тесты BIOS и NVIDIA

Ревизия25.10.2026:8 отдельных CTest suites на каждой ОС, дополнительно Linux
ASan/UBSan. NVIDIA collector теперь18 сценариев: первоначальный и повторный
NVML BDF binding, неоднозначность, смена OS PCI, failure очистки, отмена до
запуска/после первого чтения, Intel-primary hybrid, недоступная NVIDIA и
несовпадающий vendor list. State suite проверяет реальный чистый gate,
используемый Windows cleanup, precedence исходной ошибки и сообщения окна.
Никакие SCM/нативные драйверные адаптеры не линкуются и не вызываются.
Добавлены PCI3 device-list overlap/unterminated/outside regressions.

Поручение владельца25.10.2026: раздельно проверить AFU, FPT16, FPT19, NVIDIA
на синтетике и имеющихся образах, ничего не запускать на железе. Уточнение:
реальные личные образы есть только WUJIE и GM6. FPT19/NVFlash capture нет.

## Безопасная отдельная сборка

```text
cmake -S tests/offline/firmware -B build/firmware-offline/<OS> -DCMAKE_BUILD_TYPE=Release
cmake --build build/firmware-offline/<OS> --config Release
ctest --test-dir build/firmware-offline/<OS> -C Release --output-on-failure
```

Windows проверялась MSVC/VS2026, Linux — GCC15/WSL Ubuntu26.04. Для файловых
fixtures нужен Python3; NVIDIA simulation использует только системный SHA256:
BCrypt Windows или GLib Linux. Ни один target не связан с платформенным
адаптером, core/GUI entry point, NVML, драйвером или vendor executable.
Сборка отдельно от корня исключает случайный запуск аппаратных registered tests.
Общий Build-All остаётся tests=OFF и не выполняет эту suite автоматически.

## Покрытие

- `firmware_afu_synthetic`: blank00/FF и их смесь; размеры<1МиБ/>64МиБ;
  нулевой/несовпадающий/overflow SMBIOS; границы1/32/64МиБ; отсутствие Intel
  Descriptor/ME/reset допустимо. Результат — declared_rom_image без утверждения
  физической SPI-разметки. Усечённый DWORD и переполнение смещения word().
- `firmware_fpt16_synthetic`: отдельный allowlist LPC16, один компонент;
  `firmware_fpt19_synthetic`: allowlist LPC19 + точные HYDROC/YAOSHI Ultra275HX
  guards, два компонента. Оба проверяют **тот же** общий IFD2 parser, а не
  скопированные валидаторы. Capacity берётся из Descriptor, не SMBIOS.
  Есть размеры/blank, descriptor offsets, unsupported density/components,
  точная capacity, disabled/overlap/gap/outside regions, extent Descriptor,
  ME$FPT/$CPD, blankGbE, FV checksum/length/header/truncation, payload-only
  изменение, reset jump/NOP/truncated operands, denied host read/extended12.
- `firmware_nvidia_synthetic`: PCI ROM55AA/PCIR, указатели/границы/длины,
  vendor/device/code, legacy checksum, EFI без ложной legacy checksum,
  несколько images, device list PCI3, trailing bytes. Независимые EEPROM
  capacity/size/unknown/overflow/clamp,16МиБ предел, positive compare, typed
  read-only argv; VBIOS2000KiB не принимается как full2МиБ.
- `firmware_nvidia_contract`: прежние88чистых parser/argv/catalogue проверок,
  14NVIDIA профилей/7семейств +2интегрированных +Unknown.
- `firmware_nvidia_collector`: прежние9сценариев/337проверок реального общего
  collector, связанного только с синтетическими OS/GPU/vendor stubs. Оба
  дампа; partial; unknown/oversize; truncated/full hash mismatch; silent
  compare; смена chip ID; pending reader; ordinary mismatch с успешным full.
- `firmware_real_files`: два одинаковых WUJIE AFU прохода32МиБ, GM6 stock и
  patched full32МиБ, фактический GM6 region16МиБ. У каждого immutable SHA/size
  и происхождение в real-files.json. Full-файлы проверяются как структура;
  region обязан отвергаться как fullSPI. Это не утверждает снятие GM6 нашим
  FPT, отсутствие патчей или возможность восстановить ноутбук этим файлом.

## Найденный дефект

Оба GM6 full-файла имеют reset bytes90 90 E9 A3 AE..., то есть два NOP перед
ближним переходом. Прежняя проверка только opcode0/1 ложно отказывала после
успешного разбора Descriptor и18FV с корректными header checksum. Минимальная
правка resetJump сохраняет прежний допуск opcode0/1 и дополнительно пропускает
только ведущие90, затем требует целый E9 rel16
либо EA ptr16:16 внутри последних16байт. Нет поиска произвольного E9 в данных.
Читалки, их argv, signatures/pins, driver guards и UI не менялись.

## Что тесты не доказывают

Полнота адресного чтения, структурная корректность и bootability различаются.
Два FV, форма reset и отсутствие gaps — консервативные admission rules текущего
decoder; отказ не доказывает повреждение BIOS. FV checksum относится только
к заголовку. AFU size/SMBIOS — declared image, не независимо доказанный chip.
Проверка fullNVIDIA отдельно требует распознанный EEPROM и точный размер;
равные ROM SHA и PCI chain этого не заменяют. Реальных FPT19 и NVFlash дампов
нет: их физическая работоспособность не объявляется подтверждённой синтетикой.

Личные BIOS/ZIP не копируются в Git и не отправляются. Test runner читает
фиксированные файлы/ZIP members, сверяет SHA, создаёт копии только в build/,
исполняет только изолированный validator executable и сверяет неизменность
копий. Если личных fixtures нет, CTest явно пишет SKIP(return77). Подмена или
несовпадение хэша — FAIL. Локальный запуск требует все5fixtures без SKIP.
