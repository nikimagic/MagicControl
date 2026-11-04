# MagicControl0.1.143 — correct GM5HG0A installed-driver admission

The second owner archive logs_GM5HG0A (2).zip, dump-20261022-105821-0,
reports one ACPI\INOU0000\0, service UWACPIDriver, INF oem35.inf,
version11.2.18.347, installed SYS C:\Windows\System32\drivers\UWACPIDriver.sys.
SHA256:e9840521975e9073881a488f9e978a18c93aed44c57fe2382b1d513fd27126e2.
This exactly matches the previously reviewed bundled Uniwill11.2.18.347 SYS
from HYDROC5.23.51.56 and MECHREVO GX5.57.1.2. Its IOCTL read ABI was reviewed
in research/devices/hydroc-16-g2/oem-5.23.51.56/REVERSE-ENGINEERING-20261019.txt.

0.1.142 incorrectly pinned the other reviewed driver10.48.11.331; all reads
were refused before opening the EC device. No register evidence was obtained.
Change only GM5 diagnostic driver pin to its exact installed/reviewed SYS.
Existing model pins, identities, transports, UI and payload writers unchanged.
No generic version bypass/allow-any-driver rule, installation or EC write added.
A fresh owner dump remains necessary to obtain ProjectID/capability/raw RAM.

Build via Tools/Build-All.ps1, stage and compare both portable versions/hashes.
No application, hardware tests or additional tests run. No GitHub publication.
