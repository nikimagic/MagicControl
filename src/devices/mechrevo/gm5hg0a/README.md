# GM5HG0A — MECHREVO Yilong15Pro

## Identity and owner-authorized common mechanisms,0.1.144

Exact MECHREVO/GM5HG0A/Yilong15Pro Series GM5HG0A, BIOS N.1.09MRO19,
EC SMBIOS1.15. Native UWACPIDriver11.2.18.347 installed on the owner machine:
e9840521975e9073881a488f9e978a18c93aed44c57fe2382b1d513fd27126e2.
Shared PnP/service/Windows-path/SHA guard; no driver installation/replacement.
0.1.142 used the wrong reviewed driver pin; corrected in0.1.143.

Owner explicitly requests GM6 common controls for GM5. One Gm6Controller supports
both boards, while actual SMBIOS chooses separate profile, INI and power draft.
GM5 identity probe is78E.bit6; ProjectID must be stable, nonzero/nonFF and unchanged
within the controller lifetime. No GM6 project25 is substituted. Other model probes
keep default maskFF and unchanged equality semantics.

- Common RamFan1.5 Default, Manual16-point RAM, Manual online and Fan Boost.
- Online CPU temperature: AMD PawnIO; GPU: NVIDIA. Shared display-route/hybrid
  presentation covers Radeon780M + RTX4060; no firmware MUX switch added.
- Native AMD SPL/SPPT/FPPT CPU-only EC overrides783/784/785, fresh OEM presets,
  bounded Custom and Auto. No FAN/table/mode writes from CPU-only SETs.
  AMD FPPT Double uses even-watt half-byte encoding and ×2 readback; readback is not SMU proof.
- Shared RGB SingleZone controls, actual capability and common rollback/lifecycle.
- Battery XIF/XST telemetry/logging, raw editor, upper7B9 and explicit owned EC
  policy through the existing native battery controller. Physical enforcement
  is unmeasured on GM5. 7D0/7D1 are DBD1/DBD2: never automatic charge-restart writes;
  lower threshold editor is unavailable. Other models retain their existing codec.
- Unknown/foreign OEM ownership blocks writes. Linux keeps shared menu/cards,
  recognition and system telemetry without inheriting a Windows EC writer.
- Physical button next to power (OEM176) uses the shared WMI/FIFO/OSD mechanism
  from0.1.145: Office/Gaming/Turbo/Custom, own OEM5.52.50.0 default curves.
  If49F.1 reports no Turbo, the button uses Office/Gaming; saved Custom is exited
  to Office. Custom uses its own session/CSV intent and AMD FPPT codec.
- Factory-mailbox export remains unavailable; no GM6Project25 or its curves are
  sent to GM5. Physical button templates are from the owner-supplied GM5 OEM packet.

## Local evidence

User archives logs_GM5HG0A.zip and logs_GM5HG0A (2).zip. Second archive shows the
exact installed driver; first DSDT SHA256:
64e288a1ed9e473368332202ace8fea0fe59506fbf527f5c43b611f32a45eee2.
INOU.ECRR byte reader=FED50000+address, ECMG length1000.
CPTM43E, VGAT44F, CYCN4A6, CGLM7B9.low7, DBD1/DBD2=7D0/7D1.
1173 writes Arg1/Arg2*8 to DBD1/DBD2 and notifies NPCF.

44relevant local decompiled source files inventoried (43different contents) in
research/gm5hg0a-20261022/ALL-LOCAL-OEM-VERSIONS.json. OEM reference RamFan1p5
ReCheckCpuPLMinimumValueA maps AmdSPL/SPPT/FPPT to SetPL1/2/4 at783/784/785.
MyFanCtrl selects RAM1.5 via78E.6 and project/commercial/customize branches.
This is owner-authorized protocol reuse, not a claim that GM5 physical controls
have already been tested. The supplied archives contain no successful EC reads.
