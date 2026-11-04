# Logger EC editor — 22.10.2026

## Result

- Common Release build 0.1.137 complete; Windows/Linux portable staged.
- Outer Logger card contains separate EC and Windows diagnostic cards.
- Existing styled numeric fields support HEX. Enter reads address or applies byte.
  Enter during passive polling queues the command instead of losing it.
- Known multibyte fields (up to four bytes) show constituent bytes vertically,
  active row in green, HEX/DEC/INT8 and individual b7..b0 states. LE/BE below.
- Read-only RichEdit supports mouse selection and copying, with bundled Noto
  body font and shared palette. Linux uses selectable text; no raw backend is
  connected there, so raw controls remain unavailable. Honor is unchanged.
- Field names and uncertain bit interpretations use the exact board catalog.

## Command boundary

Explicit Enter only, no persistent raw commands, periodic editor reads or startup
writes. Require exact-board identity, existing transport guards and DeviceOperation.
Writes compare a fresh value against the previous observation, change allowed bits
with read-modify-write, perform one attempt and read back. No blind retry/rollback.
Identity/ROM addresses 454/456/740/770 read-only. Preserve bit7 of thresholds and
Wujie AP_OEM. HYDROC transport remains restricted to 7B9/7D0 for writes.
Known multibyte reads require two matching sets within three attempts; no unknown
adjacent address sweep. Only the selected byte can be written.

## Verification

Tools/Build-All.ps1 -Resume completed the same unfinished 0.1.137 after the final
Enter queue correction. No source edits after successful completion.
Both portable versions and GUI SHA-256 match build/product-build.json:

- Windows: 160A5AD4FDDE288D1B6746AC898DEED69F1F80E346BD120F3445F410CFCAFB4B
- Linux: F5C77241CE7D4B4B54BD2EB4D7BF94AA41980CBFAA336D2FB5A3EA487BDBF8B1

Whitespace check passes. Builds retain pre-existing compiler warnings.
No automated tests, GUI runtime checks or live EC operations were performed.
Physical effects of raw edits and visual selection/highlighting await user checks.
Personal INI/CSV/logs are excluded from the commit. No GitHub packages or push.

## Downloads deployment

Downloads/MagicControl received the first 0.1.137 build successfully, preserving
all three existing INI/CSV files (GUI SHA256 3A94D4257AA0C3052A5B41597C6AE5B3685BD5C0857021C1A3793ADC4B0F07AD).
Replacing it with the final Enter-queue correction is currently blocked by the
running EXE. User has been asked to close via tray Exit. Both working repository
portable copies contain the final verified build; no process was terminated.
