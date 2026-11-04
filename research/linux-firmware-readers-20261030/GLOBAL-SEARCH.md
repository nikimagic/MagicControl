# Expanded AFULNX 5.17 search

30 October 2026. Target: original Linux x86-64 AFULNX 5.17 or newer. No vendor
tool executed, no driver loaded, no product runtime or hardware setting changed.

## Search coverage and its limits

- Queried original/OEM sources and public search results for AMI, ASRock/Rack,
  MSI, Supermicro, ADLINK, AIC, SECO, Blackcore, Juniper, QNAP, Nokia/Celestica,
  Netberg, Advantech, DFI, ASUS, Gigabyte, Lenovo, Tyan/MiTAC, AAEON, Kontron,
  Axiomtek, IBASE, Portwell, Inspur, H3C and Sugon. A query with no results is
  recorded as no indexed lead, NOT a full inspection of every OEM BIOS package.
- Searched GitHub repository metadata and paginated public code results. The
  named-repository query enumerated 38 entries, mostly driver-source forks.
  The code search eventually reported 319 results, including irrelevant matches
  inside encoded images/notebooks. These are search metadata, not 319 inspected
  binaries. Relevant original sources and candidate trees were followed directly.
- Checked Chinese-language searches and CSDN, 51CTO, Chiphell, 52pojie, Gitee,
  right.com.cn, plus Win-Raid, BIOS-Mods, ServerTheHome and Linux/Arch/Fedora
  discussions and packaging leads. No indexed original 5.17 binary was identified.
- Exact version searches were repeated with AFULNX, AFULNX_64, afulnx64,
  AfuLnx64.zip, AFU_LINUX and AFULinX64 spelling variants. Windows AFU, EFI tools,
  AMISCE, kernel version 5.17 and modified Zibri 5.05 were not counted as matches.

## New concrete results

| Lead | What was actually established | Decision |
|---|---|---|
| SONiC / Celestica DS3000 | Downloaded ELF64 at pinned original SONiC revision; embedded version 5.15.00.0064 | Below requested version; research only |
| SONiC / Netberg Aurora 610 | Downloaded ELF64 at the same revision; embedded version 5.11.01.1745 | Below requested version; research only |
| QNAP compatibility toolkit | Repository explicitly supplies only driver sources; reference is AFULNX 5.14.02.0026 | Not a newer executable |
| AMD-BC-250 tools | Readme identifies original AFULNX 5.16.02.0111; tree also carries 5.05/5.12/5.14 | No 5.17 |
| likueimo/tools | AFULNX readme identifies 5.14.02.0026 | No 5.17 |
| immune-gmbh attestation SDK | Calls an externally supplied AFULNX with /O /Q; no binary or 5.17 version established | Wrapper is not a reader download |
| ADLINK Yocto recipe | Names AfuLnx/64/AfuLnx64.zip in OEM GitLab and uses user/token authentication | No authentication attempted; version not established |
| Win-Raid collection, Sep 2026 | Poster lists Linux 5.16.04.0135; moderator removed the corresponding link | Version claim only; no binary verified |
| AIC modern server BIOS packages | Manuals explicitly describe an AfuLnx64 directory; current firmware catalog lists packages | Live site timed out via Python and in-app browser; BIOS package contents/version not obtained |
| Slimbook firmware tree | Original public AMI tools and board EFI/Windows utilities in inspected tree | No Linux 5.17 binary in that inspected tree |
| Hetzner-NFS mirror | Firmware scripts reference AFULNX, but inspected tree does not include it | No 5.17 download obtained |

The two new downloaded binaries and SHA-256 values are in global-candidates.json.
They were inspected as files, not started even with --help. Candidate binaries
remain under research/candidates and are not staged into the product.

## What remains unknown

Original AFULNX 5.17 is still not obtained. Shared AMI 5.17 release notes document
Linux driver changes, but do not identify a publicly downloadable Linux binary.
Public search cannot establish that no such file exists; similarly, a new OEM
package date cannot establish that it contains version 5.17. The strongest
unresolved routes are original OEM downloads requiring access and the currently
unreachable AIC firmware package. No login/paywall/captcha was bypassed, and no
messages were sent to vendors or forum users.

Sources:
- https://github.com/sonic-net/sonic-buildimage/tree/ec21e592e49c120bef206b4cc7a1601e4ed51fac/platform/broadcom/sonic-platform-modules-cel/ds3000/utils
- https://github.com/sonic-net/sonic-buildimage/tree/ec21e592e49c120bef206b4cc7a1601e4ed51fac/platform/barefoot/sonic-platform-modules-netberg/aurora-610/utils
- https://github.com/grisno/qnap-afulnx-driver-tool
- https://github.com/AMD-BC-250/uefi/tree/7a984bbef7eba6a87347436346c1310c8b6444e8/tools/afulnx
- https://github.com/che-adlink/meta-adlink-demo/blob/3ae1accbb88c6f9cb54c2f0e1fe241c245889d8a/recipes-kernel/afulnx/afulnx_1.0.bb
- https://github.com/immune-gmbh/attestation-sdk/blob/f44e4502aecaf555b7231a5ff216df038853c6b7/pkg/flashrom/dump_afulnx64.go
- https://winraid.level1techs.com/t/offer-legacy-bios-modding-hardware-tuning-tools/118898
- https://www.aicipc.com/downloads/
