# MagicControl0.1.146 — exact GM5 support link

The shared BiosSupportLinks resolver now selects the official MECHREVO download
catalogue for the exact MECHREVO/GM5HG0A/Yilong15Pro Series GM5HG0A descriptor.
Both Windows and Linux consume this resolver; no page geometry changes.

Verified2026-10-22 against the official website and its public client API:
category VGMSYq (gaming), serial rwvS4q (Wing), display_product VKlSN7,
author90 (R7 4060/4070). The product label has been renamed to2026, but this
author filter returns eight original MECHREVO2024/YL15P driver downloads:
AMD chipset/VGA/DRTM, NVIDIA VGA, Senary audio, LAN, WLAN/BT and MEP.
The unfiltered2026 product page is therefore unsuitable for this machine.

URL: https://www.mechrevo.com/cn/downloads?category=VGMSYq&serial=rwvS4q&display_product=VKlSN7&author=90

Saved official API responses: `research/build-evidence-20261107/audit-batch/OTHER/mechrevo-web/gm5-exact-drivers.json`,
`wing-options.json` and `gm5-product169.json` in the same directory. These were
physically moved from `build/mechrevo-web`; file hashes are recorded in the
batch's `FILES-before.csv`. No BIOS updater is enabled.
Other board branches remain unchanged. No EC writes or hardware tests.

Build: common Release Windows/Linux via Tools/Build-All.ps1; stage and verify
both working portable copies. No application tests requested or run.

Verified staged portable:
windows 0.1.146 SHA256 5B8E8ACE23B6DF07956FF63CA3D8B8C74D61DF47D1A2AE0E84C19099EDF685B0
linux 0.1.146 SHA256 5DFC15A0BCE98C70A51AC008CA761912FAA2D481F3D8817C580A7C090DEC9048
