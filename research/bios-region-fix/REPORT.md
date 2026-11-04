# BIOS region handling, MagicControl0.1.189

Source evidence: `../gm6ir0c-logs15-20261024/bios-comparison.json` and REPORT.md.
GM6 AFU5.17 /O returned exit0 and a16MiB UEFI region; SMBIOS reports32MiB.
The previous full-size-only validator rejected pass1 and never collected pass2.
Offline comparison with both full stock and patched images proves the returned
region shape and ties it to patched new.fd. Differences after the first384KiB
are zero against patched; the prefix comprises two NVAR stores.

## Change

- Keep full Intel SPI size/Descriptor/region/ME validation. Keep AMD expected-size
  capture unchanged. No new tools or command switches, driver admission changes,
  hardware writes, firmware-programming or security-policy changes.
- Separately recognize a plausible Intel UEFI-region shape: power-of-two size
  dividing and not exceeding SMBIOS, first FV at0, minimum2 valid FV headers,
  header checksum, bounded FV lengths and a reset jump in the final16bytes.
  This is structural validation, not proof of physical capacity/region bounds.
- Read twice. Validate both shapes and require identical sizes/types/SHA256.
  Store `bios-region.bin` and `bios-region-verify.bin`, manifest schema3 with
  region_verified/region_only=true/capture_saved=true/capture_complete=false.
  Message explicitly says full SPI was not captured. Do not guess a region base.
- Persist each observed byte count and SHA before shape/exit-code validation,
  including failed or different passes; retain all raw stdout/stderr/events.
- Progress reports actual accumulated read bytes. No false percentage from the
  SMBIOS full-ROM size; no geometry, header or scroll changes.

## Checks and limits

Source reviewed against the saved GM6 region and AMD successful capture.
Build-All builds Windows/Linux GUI and stages both working portable folders.
`verification.json` records the final completed build, source-input identity,
versions and hashes. No product tests were added/run; no application, helper,
AFU/FPT or driver was launched by the assistant. Two new Intel hardware reads
remain owner verification. Full Intel SPI reading through FPT is not implemented
by this change; Descriptor/ME/GbE remain absent from the supplied AFU region.

## Lighting audit following0.1.188

Reviewed current SingleZone setter, Windows handlers and HYDROC packets.
Mono uses model-specific RGB; Pulse uses native1–4 presets rather than arbitrary
RGB; Multi owns firmware rainbow and disables the native RGB controls; Cycle6
owns a host timer with six saved colours and separate interval. Explicit OFF
stops the host timer and clears native Pulse/rainbow with masked writes;
brightness preserves/resumes the saved native effect through the same setter.
HID Breathing RGB now preserves effect2 (0.1.188). Wave/rainbow remain the
existing HID configurations; no physical colour or OFF success is inferred
from source or readback alone. Fresh logs15 have mode changes, but no explicit
OFF/colour click that could validate those physical effects.
