# GM6 logs9: blocked Custom slots and observed timing

22.10.2026. Source: logs_GM6IR0C (9).zip. Saved-file analysis only; no app/GUI/hardware tests.

Both sessions are0.1.158:22:24:33.944–22:39:05.225 and22:40:08.488–22:41:48.600 MSK. They do not contain the159 auxiliary-decoding corrections.

## Confirmed defect and fix

The first session records58 preset button clicks:41 on181(slot1),6 on182(slot2),11 on183(slot3). None reached selectCustomPreset: no custom_preset clicked/saved/applied, no corresponding settings_operation. Normal Custom and profile commands did work.

Root: the typed-GM6 BN_CLICKED admission whitelist admitted Manual180 but not181..183; the later dispatch branch existed and was unreachable. Fix adds exact-GM6 custom preset commands to that whitelist and reuses one customPresetCommand predicate for admission, dispatch and quick completion. Disabled controls, preview, busy/operation rules and backend requireCustomPreset/native guards stay intact. Other profile writers do not gain admission. Add clicked/lookup-error stage logs for the next owner capture. Existing save/apply implementation, file format and native FAN/PL writes unchanged.

## Actual software completion observations in158

12 software_command_timing records. profile_refresh_ms0–266, median47; most repeated normal Custom refreshes16ms. Turbo full settings operations1.755–1.845s; apply_ms1.562–1.563s, refresh93–204ms. Earlier157 full Turbo2.499–3.501s. Thus the deferred-provider change is visible, while native I/O still dominates. This is owner telemetry, not a benchmark run by the agent.

First session13 requested/13 confirmed settings operations; second3/3. Unhandled preset clicks are not successes and are not counted as completed operations. Custom25/30/30, Economy10/15/20 and native profile changes have readback.

## Battery/reboot evidence, not changed by this fix

First session:85%,0mA,7C3=4,770=FF,ReachedD5; lower50→80 when the85% command ran. Second session: Windows uptime is39140ms compared with6895656ms at the first start, indicating a reboot between sessions. The portable path is the same Desktop/MagicControl-0.1.158-windows-x64-portable.

After reboot7C3=7,770=FF,upper85 withoutReached. Current rises0→1632→1802mA at85% before the later95% selection. After95% it reaches86%. So the upper byte alone did not preserve enforcement across that reboot.

Both startups report keyboard restore explicit_file=false. That code and NativeBattery use the same gm6ir0c-device-settings.ini. The battery worker explicitly skips restore when the file is absent; it never fabricates ChargeAuto preference from observed EC state. This explains the lack of a saved platform4 restore choice, not who initially removed/omitted the file. No INI is included in this archive; no claim of deletion or attempted restore failure. Selecting the approved EC off/platform4 preference explicitly creates its saved choice; preserve/migrate per-model INI, manual power TXT and fan CSV when extracting a new release directory. This patch does not silently write a battery platform on startup without preferences.

Implementation remains exact GM6. Common build/portable verification is VERIFICATION-0.1.160.json. The end-to-end UI preset event must still be checked on the owner's machine; no runtime tests were added or executed.
