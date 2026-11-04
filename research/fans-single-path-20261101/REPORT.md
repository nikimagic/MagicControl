# FANs: общий request, execution и readback

База `7751e940`, общий продукт0.1.426. Пакет от01.11.2026.
GUI/CLI/внешний restore и согласованный Custom должны использовать один путь.
Геометрия, scroll и рабочие hardware algorithms сохраняются. Только автономные
fixtures, без запуска приложения, реальных transports/firmware tools.

## Найденные расхождения

1. Три public family writers, отдельные operation/worker wrappers и EC policy
   helpers. GUI выполнял дополнительный readback, CLI часть его обходил.
2. CLI Wujie Manual отправлял Online вместо автономной EC Manual кнопки GUI.
3. CLI fixed ranges расходились с общими5..100: HPBSD36/38, Honor0.
4. Native Default игнорировал OwnershipLost; auto-looking readback мог скрывать
   потерю владения. CLI не выполнял ту же проверку результата, что GUI.
5. Внешний Wujie restore и FAN шаг Custom вызывали controllers напрямую.
6. Caller lease не передавался Max/Online в Wujie очередь, создавая риск повторной
   admission против собственной DeviceOperation.
7. Две отдельные Online channel-команды могли coalesce и потерять первый канал.
   Это воспроизведено Memory EC тестом и устранено одной profiles-командой GUI.

## Реализация

FanWriteRequest общий: Default/Manual/Max/Min/Online/Restore и native SaveDraft,
exact optional payload, controlled channels/link, wait/adoptExisting и origin.
Пустой payload не порождает кривую/target. Никакой normalization/resampling в DTO.

executeFanResource(context,request) — единственный Windows control entry.
Реальные GM/native/Wujie adapters остаются приватны у existing owner.
GUI queue хранит тот же DTO, а не ещё один payload на каждый backend. Command,
unconditional post-read, validation и ошибки в одном пути. Failed read не
публикует фиктивный zero snapshot. CLI проверяет результат, не только факт вызова.
EC GUI сохраняет async pending; wait request не выдаёт queue acceptance за proof.

Wujie Online применяет profiles одним checked controller request, как GUI.
Меняется только ecManual intent=false, ни одна точка не пересчитывается.
Manual использует прежнюю autonomous CPU-shared EC таблицу. Caller lease
передаётся в existing enqueue для Manual/Max/Default; неизменённый worker
не захватывает его повторно. Restore передаёт startup adoption и тот же lease;
recovery Native before Restore по-прежнему narrow, без APP consumer reset.

Native Min берётся из fresh actual minimum, не CLI hardcode. Fixed input5..100
совпадает с owner contract; настоящие backend guards/thermal maxima остаются.
Native OwnershipLost всегда failure, даже если последующий read выглядит Auto.
Draft Save не становится hardware apply. HPBSD mandatory upper bands/slopes,
nominal-RPM meaning, HLYL watchdog и GM codecs не менялись.

Старые public writeGm/Native/EcFanResource, applyGm/NativeFanOperation,
executeGm/NativeFanWorker, applyEcManual/Preset/restore wrappers и SaveDraft
forwarder удалены из API. Operation bodies/private readback остаются одной
реализацией, не compatibility aliases. Новых production файлов/controllers/
queues/workers нет. Linux existing unavailable adapter принимает тот же request,
не получает Windows EC writer по имени модели.

## Карта

| Вход | Общий entry | Владелец/завершение |
|---|---|---|
| Windows GUI GM/native | private model queue с FanWriteRequest -> executeFanResource | прежний exact controller setter + shared readback/result; native view только принимает |
| Windows GUI EC | тот же request/entry, pending | прежний FanController worker/status/notice; GUI не блокируется |
| Windows CLI | input mapping -> тот же request/entry, wait | тот же readback; прежний foreground watchdog/online lifetime |
| Wujie внешний startup/resume | Restore origin/adoptExisting/lease -> тот же entry | narrow recovery при необходимости, прежний restoreSavedProfiles |
| Custom FAN/CPU | Compound request -> тот же FAN entry | FAN подтверждается до прежнего CPU writer; порядок и partial-failure сообщение сохранены |
| Native/GM внутренний replay/diagnostics | тот же model owner | прежние checked setters/journals; не второй frontend writer |
| Linux mode/link | тот же DTO -> unavailable adapter | сохранены сообщения недоступности; draft CSV/monitoring отдельно, без hardware permission |

Явный Wujie Default — прежний ApplicationPresence reset FAN/Copilot/APP последним,
без CPU/PL/RAM-curve reset. Internal Restore/Compound Default остаётся узким.
Ни выход процесса, ни startup без потребителя не дают разрешения на новый writer.
Режим/receipt/readback не объявляется измеренными RPM/PWM или физической защитой.

## Проверки

Расширен existing architecture_audit.cmake: retired API, raw setters вне owner,
CLI input bounds, общий restore и прежние CPU/lighting/battery guards. Это
source regression guard, не полный C++ call graph.

- Portable request test: exact curve copies/markers/channels, origin/adoption,
  пустые payload, общие inclusive20..100C/5..100 bounds и unknown intent.
- Fake NativeFixedFans: fixed command/read order, OwnershipLost, combined command/
  read failures, absent snapshot, mismatched readback, fresh model Min, exact
  curves/draft-only I/O, missing backend для всех17 объявленных профилей.
- Memory EcAccess + настоящий FanController с fake environment/owner/clock:
  Manual, Max и Online под существующим caller lease; оба online channels;
  сохранение firmware Down0/keyboard pulse bits; narrow internal Default и
  explicit Default APP/Copilot reset при сохранённых CPU policy bits. Keys store
  — только owned fixture-файл в build; настоящая служба/датчики не создаются.
- Windows9/9, Linux6/6 включая существующие runtime/CPU/lighting/battery/contract
  regressions. Адаптированные старые fixtures компилируются, не весь широкий suite.

SOURCE-OBSERVATIONS.json:145 сравнений. Все GUI/protocol sources, Native/Honor/
HPBSD/GM controllers, curves, limits и ApplicationPresence неизменны. Полный
большой Wujie controller отличается только передачей lease/reset flag в трёх
existing enqueue methods; worker/temperature/RAM/ownership algorithms прежние.
CPU Custom отличается только маршрутом своего прежнего FAN шага, restore —
общим entry. Это не физическая проверка каждого ноутбука.

Версии общего Release/обеих working portable/четыре executable SHA-256 —
VERIFICATION.json. Приложение и реальные EC/HID/SMU/SDK/service/firmware commands
не запускались. CI, licensing, Git artifact policy, архивы и публикация вне пакета.

## Итоговый продукт

Общий Release **0.1.427**, manifest `complete`, обе платформы `verified`.
Working portable Windows/Linux обновлены, включая личный `mctl`. Для каждого
из четырёх executable совпали build/portable/manifest SHA-256; оба Windows PE
ProductVersion и обе portable sidecar-версии также проверены чтением файлов.
Архивы GitHub_Releases не менялись, публикации не было. В продуктовый коммит
включаются только перечисленные файлы пакета; личные runtime settings/evidence
и прежние чужие изменения исключены.
