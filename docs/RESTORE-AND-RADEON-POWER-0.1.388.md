# Wujie recovery and Radeon graphics power

The 0.1.387 portable log contains a reproducible queue defect. At
2026-10-29 06:26:59.878 UTC, Performance revision9 was dequeued during recovery.
The worker tried to acquire DeviceOperation even though the dequeued command
already held it through ConfigurationRestore. Its early continue destroyed the
promise (broken promise) without clearing applying. Later restores and SETs
commands were rejected. The fix uses the accepted operation/revision before
recovery admission. Transient identity failures retain the request and promise
for retry; board checks and the original grace period remain. Completion also
releases the wake observation pause. No fan tables or control algorithms change.

The new regression uses an in-memory EC and simulated sleep clocks, including
temporary identity read failure, completed futures, released admission and a
subsequent command. It does not perform writes to the real EC.

Radeon 880M power is a separate issue: GPUPower often exceeds package energy
power, including 4W versus package0.885W at15:40:59.590 UTC. Exact ASIC boundaries
are still unknown; this does not establish that GPUPower equals CPU Package.
Integrated AMD display power now accepts only an identity-bound explicit GFX
reading and labels it GFX. Generic GPUPower/ASIC/board values stay in raw provider
diagnostics; absent GFX stays unknown, valid zero stays zero. CPU and package
counters are unchanged, and no subtraction, summation or cross-sensor clamping
is introduced. Discrete GPU power preference is unchanged. The Windows ADLX
power getters additionally require successful capability queries.

The legacy APU telemetry path also retains GFX rather than overriding it with
GPUPower. The catalog no longer fills GPU power from an unbound legacy slot.
The shared card label applies on Windows/Linux; no card geometry changes.

Validation: Windows fan-performance executable (including both new queued-wake
scenarios) passed with zero failures. Eight further CTest checks passed: unified
architecture audit, catalog/state/ignored legacy switch, sleep identity failure,
sleep recovery, SETs wake invalidation and worker start failure. Linux catalog,
state, ignored legacy switch and architecture audit passed (4/4).

New Windows portable0.1.388 was also queried three times with --read-only
--no-ec. The bound880M power metric selected ADL PMLog/30 GFX at1/0/0W;
the supported zeros were retained. Load was10/8/7%. This verifies source
selection, not physical accuracy or the independent CPU/package counters
(unavailable under this no-EC admission). Evidence:
research/gpu-power-check-20261029/live-gfx-0.1.388.json.

Product hashes and settings integrity are recorded in the verification artifact
after the common Windows/Linux build. Physical sleep on the updated GUI still
requires observation; no real EC commands or sleep were invoked by these checks.
