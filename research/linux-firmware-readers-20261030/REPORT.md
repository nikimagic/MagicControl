# Linux firmware readers: search and Windows version comparison

Expanded OEM, GitHub, Chinese-forum and Linux-ecosystem follow-up:
[GLOBAL-SEARCH.md](GLOBAL-SEARCH.md). Two additional original SONiC ELF files
were downloaded and identified as 5.15.00.0064 and 5.11.01.1745. No original
Linux 5.17 binary obtained; nothing was added to the application.

30 October 2026. Research only. No candidate was executed, installed, added to
the application runtime or used against hardware. Product version is unchanged.

## Verified Windows baseline

The actual bundled binaries were hashed and matched to their provenance files;
see windows-baseline.json, not just package filenames.

| Component | Windows version | Official/OEM source already used |
|---|---|---|
| AMI AFUWIN | 5.17.00.0153 | AMI public Aptio V package |
| Intel FPT | 16.1.25.1871 | Gigabyte NHxxPNP/RCxxKE BIOS package |
| Intel FPT19 | 19.0.5.1948 | Colorful iGameCenter 2026-09-12 OEM package |
| Insyde H2OFFT | 6.76; SYS 3.0.0.3 | Lenovo kscn43ww |
| NVIDIA NVFlash | 5.867.0 | Original published binary, existing provenance |

## Results and admission decisions

### AMI AFULNX: exists, no accepted current Linux binary

OEM documentation confirms a native Linux AFULNX, using AMI SMI Flash support.
The local AMI 5.17.00.0153 public package contains Windows/DOS/EFI, not AFULNX.
Publicly found 5.05.04 and Juniper's 5.16.02.0111 are below our Windows AFU
baseline; neither is accepted for integration. Juniper's package requires an
SSR subscription login; Blackcore's official tool link redirects to authentication.
The GitHub Zibri variant explicitly advertises modifications, engineering/GAN
flags and old Ubuntu compatibility; it is not an original current OEM candidate.

Clarification after inspecting the actual AMI release-note PDF in our official
5.17 package: the common AFUEFI/AFULNX/AFUWIN release history dates 5.05.04 to
2014-01-24 (PDF page 32), 5.16.02.0111 to 2023-10-18 (page 12), and 5.17.00.0153
to 2025-10-31 (page 10). The 5.17 release explicitly adds an RH9.6 Linux flash
driver build; 5.16.04 adds Linux 6.3-or-later GDK support. Thus modern AFULNX
development IS documented. The missing item is its original downloadable Linux
binary, not proof that AFULNX as a product stopped at 5.16. A lower version alone
does not establish incompatibility or insecurity. Juniper's official guide
independently shows AFULNX 5.16.02.0111 saving a BIOS and an August 2024 run.
Exact local evidence and hashes are in ami-version-dates.json. The 2023-09-05
date seen in another online copy is not substituted for our inspected official
package's 2023-10-18 date.

Primary evidence:
- https://download-2.msi.com/archive/mnu_exe/server/S425-S428-CG480-S6053-CG481-S6053-v1.0-BIOS-UG.pdf
- https://www.juniper.net/documentation/us/en/software/session-smart-router/docs/howto_update_bios/index.html
- https://support.blackcoretech.com/support/solutions/articles/70000655548-linux-deployment-tools

### Follow-up: is AFU 5.17 combined for Windows/Linux?

Re-downloaded the original AMI archive on 30 October. Its 20,713,273 bytes and
SHA-256 b59f141c37d5f55179459f6e07f0d1ed81fa742d1998b50954f1f9d6f3955307
match the previously saved package exactly. HTTP Last-Modified is 6 March 2026,
which is a server publication timestamp, not the individual release date.
Inspected the ZIP and its 19 nested ZIPs without executing files. AFU directories
are afudos, afuefi and afuwin. No AFULNX/Linux-named archive member or ELF binary
was found. Native Windows AFU is PE x86-64, subsystem 3 (Windows console), with
the Windows amigendrv64.sys driver. EFI binaries have subsystem 10; they are
UEFI applications, not Linux executables. Shared release notes and command
descriptions do not make any one of these binaries multi-OS.

Also downloaded https://download.asrock.com/TSD/Afulnx.zip directly from the OEM.
SHA-256 d488f6f8edc8c48443b1385596da24bcf74ef12b6738b10f9e0b8f0c029fef22,
364,900 bytes. Both its readme and static executable strings report
AFULNX_64 **5.16.03.0120**, not 5.17. The original ELF64 x86-64 file is 1,191,888
bytes, SHA-256 8c9280d1def31874d6ccc65d981b87bb2a2dca3f38bed84a69ce039159cc594e.
This is a concrete original Linux candidate, but below the requested version;
it stays outside runtime/packaging and was not executed. HTTP Last-Modified
(5 September 2025) must not be treated as the tool's release date.

SECO's official AFU guide independently lists separate Windows AFUWIN and Linux
EtaAfuOemLnx32/64 programs, with a Linux kernel-header/driver build dependency:
https://developer.seco.com/how-to/bios-update-afu/ . Its public guide does not
identify the Linux binary as 5.17. Public AMI, OEM, GitHub and Chinese-language
searches in this follow-up did not yield a downloadable original AFULNX 5.17.
This search result does not establish that such a binary does not exist.
See afu-package-review.json and afu-combined-binary-check.json for exact members,
versions and file formats. No version was synthesized or substituted.

### Insyde Linux H2OFFT: official candidate downloaded, not admitted

Downloaded Valve's latest plain date-version package visible in the examined
jupiter-main index: jupiter-hw-support-20260930.1-1-any.pkg.tar.zst (51,580,246
bytes). Archive SHA-256: 0624f32c8edd362f90c46bf99091f753661473930e4d18c5c152366b75d6d6a9.
The detached package signature was saved, but not verified against a trusted key.

Extracted only named files through tar's stdout, without installing or executing
scripts. The x86-64 ELF h2offt is 1,301,523 bytes, SHA-256
7295a98ec04fe3a73d4778d2add15ccef0ef685e34322fca725af6913d222447.
Static strings identify SEG **200.01.00.10** and copyright 2012–2020, with both
`-G` (IHISI save) and `-O` (save current BIOS) help text. The binary contains
phy_alloc.ko loading paths, /dev/mem, EFI and kernel-lockdown references.
The extracted H2OFFTx64.sh builds a matching driver if driver sources exist;
the inspected updater directory did not supply that driver tree.

SEG numbering is not comparable numerically to Windows 6.76. A 2026 package date
does not prove a new reader; copyright does not establish its exact build date
either. Currentness/security equivalence, driver/kernel compatibility and Honor
IHISI behavior are unproved. Therefore this candidate stays research-only.
Do not transplant Valve's platform.ini or updater scripts to Honor.

Primary evidence:
- https://steamdeck-packages.steamos.cloud/archlinux-mirror/jupiter-main/os/x86_64/
- https://support.tq-group.com/en/templates/uefi/uefi_tools/h2offt

### Intel: current CHIPSEC source is a concrete candidate

Downloaded CHIPSEC 2.0.9 source files from commit
25457060db45217c74b46f61cc934098ddff8d64. Verified Git blob hashes against the
upstream recursive tree, including VERSION; see chipsec-pinned-verification.json.
The SPI command imports **Intel** SPI HAL; it implements read_spi/read_spi_to_file.
The RPL platform XML explicitly covers 13th/14th generation and has a 2026
copyright. Linux kernel driver source is available at the same pinned revision.
This is a contemporary source-based candidate for GM6/Raptor Lake, not proof that
every flash region on the owner's exact board is readable or that Secure Boot
will admit a compiled driver. No build, driver load or SPI transaction performed.
AMD platform XML/PSP support alone must not be called AMD SPI-dump support: this
examined SPI command explicitly imports chipsec.hal.intel.spi.

FPT Linux exists in Intel's tool family, but no original Linux 16/19 binary with
matching provenance/version was obtained in this search. Intel's support points
to CSME/BKC packages. Do not substitute FWUpdLcl or CSME Version Detection Tool:
neither establishes a full system-SPI reader. Old ME 8/11 packages are excluded.

Primary evidence:
- https://github.com/chipsec/chipsec/tree/chipsec2
- https://chipsec.github.io/modules/chipsec.utilcmd.spi_cmd.html
- https://community.intel.com/t5/Embedded-Intel-Core-Processors/fpt-tool-for-Alder-Lake/m-p/1601671

### AMD: flashprog ROM3 is the most specific open-source path found

Official release index shows stable 1.5 (2026-02-13) and 1.6-rc2 (2026-08-15).
The 1.6.x source already saved in our Wujie research has an explicit
amd_rom3read implementation for ROM Armor: 64-bit physical ROM3 mapping at
0xFD00000000, a read-only mapping path and rejected write/erase methods.
This directly addresses the earlier 32-bit/16-MiB aperture limitation in our
Windows OEM transport. It is a candidate for Wujie and HPBSD after exact hardware
admission, not a tested universal AMD reader. It requires actual access to the
mapped range; Linux root alone does not establish permission under lockdown.

Source paths inspected:
- ../wujie-spi-reader-20261024/sources/flashprog-amd_rom3read.c
- ../wujie-spi-reader-20261024/sources/flashprog-chipset_enable.c

Fresh 1.5/1.6-rc2 release downloads timed out; the www host did not resolve.
No new release binary/archive was accepted. The release candidate is explicitly
not a stable release. Current flashrom and AMD's fork source were downloaded
and hashed; those inspected chipset files do not contain this ROM3 implementation.
AMD's ownership of a fork is not evidence that its code is newer or supports
these exact machines. Generic internal probing is not approved for our notebooks.

Primary evidence:
- https://flashprog.org/releases/
- https://review.sourcearcade.org/plugins/gitiles/flashprog/+/refs/heads/1.6.x/amd_rom3read.c
- https://github.com/flashrom/flashrom
- https://github.com/amd/flashrom-amd

### EC RAM and EC firmware remain separate

The downloaded current Linux ec_sys.c explicitly defines EC_SPACE_SIZE=256.
It cannot replace a full 0x000–0xFFF Uniwill RAM collector, a full ITE XRAM reader,
or an EC flash dump. A native OEM/board transport is required for full RAM.
Our existing ec_reverse follow-mode reader source sends EC/KBC commands and
temporarily changes operating state; it is not a passive universal reader and
was not run. BIOS extraction of an embedded EC image is not a live EC flash dump.

Primary evidence:
- https://github.com/torvalds/linux/blob/master/drivers/acpi/ec_sys.c

## Integration decision

Nothing was copied to src/platform/*/runtime, packaging or portable tools.
Most concrete next implementation candidates are pinned CHIPSEC Intel SPI and
reviewed flashprog ROM3 for AMD. Fresh original AMI/Insyde Linux counterparts
remain unconfirmed. All failed downloads and exact artifact hashes are retained
in sources.json, candidates.json and pinned-sources.json. No protection settings
were changed. No claim of successful BIOS/EC reading on Linux is made.
