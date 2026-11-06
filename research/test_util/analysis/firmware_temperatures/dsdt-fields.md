# DSDT: все объявленные поля

Источник: `DSDT-0171f11732dc.dsl`; 533 записей Field/BankField/IndexField, из них 469 именованных полей, 58 зарезервированных областей и 6 индексных деклараций без фиксированного адреса.

Список описывает объявления ASL, а не гарантирует доступность каждого адреса в работающей ОС. Поля внутри методов, динамические OperationRegion, BankField и IndexField требуют соответствующей операции AML; абсолютный адрес для них не угадывается. Резервные промежутки сохранены в CSV для проверки смещений.

## Сводка по address space

| Address space | Field entries |
|---|---:|
| SystemMemory | 376 |
| EmbeddedControl | 101 |
| SystemIO | 50 |
| IndexField (indirect) | 6 |

## SystemMemory и EC MMIO

| Объект | Регион / пространство | Смещение | Ширина | Адрес | Контекст |
|---|---|---:|---:|---:|---|
| `WOL5` | `OGNV` / SystemMemory | 0x0.0 | 8 bit | 0x6FFF4A98 |  |
| `DPMD` | `OGNV` / SystemMemory | 0x1.0 | 8 bit | 0x6FFF4A99 |  |
| `MOID` | `OGNV` / SystemMemory | 0x2.0 | 8 bit | 0x6FFF4A9A |  |
| `SARS` | `OGNV` / SystemMemory | 0x3.0 | 8 bit | 0x6FFF4A9B |  |
| `S24G` | `OGNV` / SystemMemory | 0x4.0 | 8 bit | 0x6FFF4A9C |  |
| `S5G1` | `OGNV` / SystemMemory | 0x5.0 | 8 bit | 0x6FFF4A9D |  |
| `S5G2` | `OGNV` / SystemMemory | 0x6.0 | 8 bit | 0x6FFF4A9E |  |
| `S5G3` | `OGNV` / SystemMemory | 0x7.0 | 8 bit | 0x6FFF4A9F |  |
| `S5G4` | `OGNV` / SystemMemory | 0x8.0 | 8 bit | 0x6FFF4AA0 |  |
| `S6G1` | `OGNV` / SystemMemory | 0x9.0 | 8 bit | 0x6FFF4AA1 |  |
| `S6G2` | `OGNV` / SystemMemory | 0xA.0 | 8 bit | 0x6FFF4AA2 |  |
| `S6G3` | `OGNV` / SystemMemory | 0xB.0 | 8 bit | 0x6FFF4AA3 |  |
| `S6G4` | `OGNV` / SystemMemory | 0xC.0 | 8 bit | 0x6FFF4AA4 |  |
| `S6G5` | `OGNV` / SystemMemory | 0xD.0 | 8 bit | 0x6FFF4AA5 |  |
| `S6G6` | `OGNV` / SystemMemory | 0xE.0 | 8 bit | 0x6FFF4AA6 |  |
| `G2VF` | `OGNV` / SystemMemory | 0xF.0 | 8 bit | 0x6FFF4AA7 |  |
| `G2OF` | `OGNV` / SystemMemory | 0x10.0 | 8 bit | 0x6FFF4AA8 |  |
| `G5VF` | `OGNV` / SystemMemory | 0x11.0 | 8 bit | 0x6FFF4AA9 |  |
| `G5OF` | `OGNV` / SystemMemory | 0x12.0 | 8 bit | 0x6FFF4AAA |  |
| `G6VF` | `OGNV` / SystemMemory | 0x13.0 | 8 bit | 0x6FFF4AAB |  |
| `G6OF` | `OGNV` / SystemMemory | 0x14.0 | 8 bit | 0x6FFF4AAC |  |
| `G2VC` | `OGNV` / SystemMemory | 0x15.0 | 8 bit | 0x6FFF4AAD |  |
| `G2OC` | `OGNV` / SystemMemory | 0x16.0 | 8 bit | 0x6FFF4AAE |  |
| `G5VC` | `OGNV` / SystemMemory | 0x17.0 | 8 bit | 0x6FFF4AAF |  |
| `G5OC` | `OGNV` / SystemMemory | 0x18.0 | 8 bit | 0x6FFF4AB0 |  |
| `G6VC` | `OGNV` / SystemMemory | 0x19.0 | 8 bit | 0x6FFF4AB1 |  |
| `G6OC` | `OGNV` / SystemMemory | 0x1A.0 | 8 bit | 0x6FFF4AB2 |  |
| `G2VW` | `OGNV` / SystemMemory | 0x1B.0 | 8 bit | 0x6FFF4AB3 |  |
| `G2OW` | `OGNV` / SystemMemory | 0x1C.0 | 8 bit | 0x6FFF4AB4 |  |
| `G5VW` | `OGNV` / SystemMemory | 0x1D.0 | 8 bit | 0x6FFF4AB5 |  |
| `G5OW` | `OGNV` / SystemMemory | 0x1E.0 | 8 bit | 0x6FFF4AB6 |  |
| `G6VW` | `OGNV` / SystemMemory | 0x1F.0 | 8 bit | 0x6FFF4AB7 |  |
| `G6OW` | `OGNV` / SystemMemory | 0x20.0 | 8 bit | 0x6FFF4AB8 |  |
| `CLSP` | `OGNV` / SystemMemory | 0x21.0 | 8 bit | 0x6FFF4AB9 |  |
| `CL01` | `OGNV` / SystemMemory | 0x22.0 | 8 bit | 0x6FFF4ABA |  |
| `CL02` | `OGNV` / SystemMemory | 0x23.0 | 8 bit | 0x6FFF4ABB |  |
| `CL03` | `OGNV` / SystemMemory | 0x24.0 | 8 bit | 0x6FFF4ABC |  |
| `CL04` | `OGNV` / SystemMemory | 0x25.0 | 8 bit | 0x6FFF4ABD |  |
| `PMID` | `OGNV` / SystemMemory | 0x26.0 | 16 bit | 0x6FFF4ABE |  |
| `PPID` | `OGNV` / SystemMemory | 0x28.0 | 16 bit | 0x6FFF4AC0 |  |
| `UHBS` | `OGNV` / SystemMemory | 0x2A.0 | 32 bit | 0x6FFF4AC2 |  |
| `AUPT` | `OGNV` / SystemMemory | 0x2E.0 | 8 bit | 0x6FFF4AC6 |  |
| `PEDD` | `OGNV` / SystemMemory | 0x2F.0 | 4096 bit | 0x6FFF4AC7 |  |
| `DDSS` | `OGNV` / SystemMemory | 0x22F.0 | 8 bit | 0x6FFF4CC7 |  |
| `GBAS` | `OGNV` / SystemMemory | 0x230.0 | 32 bit | 0x6FFF4CC8 |  |
| `D888` | `OGNV` / SystemMemory | 0x234.0 | 32 bit | 0x6FFF4CCC |  |
| `D894` | `OGNV` / SystemMemory | 0x238.0 | 32 bit | 0x6FFF4CD0 |  |
| `D8A4` | `OGNV` / SystemMemory | 0x23C.0 | 32 bit | 0x6FFF4CD4 |  |
| `D8A8` | `OGNV` / SystemMemory | 0x240.0 | 32 bit | 0x6FFF4CD8 |  |
| `PNSZ` | `OGNV` / SystemMemory | 0x244.0 | 8 bit | 0x6FFF4CDC |  |
| `ADSN` | `ADGV` / SystemMemory | 0x4.0 | 8 bit | 0x6FFF4E9C |  |
| `ADI1` | `ADGV` / SystemMemory | 0xB.0 | 8 bit | 0x6FFF4EA3 |  |
| `ADI2` | `ADGV` / SystemMemory | 0xC.0 | 8 bit | 0x6FFF4EA4 |  |
| `ADI3` | `ADGV` / SystemMemory | 0xD.0 | 8 bit | 0x6FFF4EA5 |  |
| `ADI4` | `ADGV` / SystemMemory | 0xE.0 | 8 bit | 0x6FFF4EA6 |  |
| `ADI5` | `ADGV` / SystemMemory | 0xF.0 | 8 bit | 0x6FFF4EA7 |  |
| `RSTE` | `NRSV` / SystemMemory | 0x0.0 | 32 bit | 0x70066000 | Scope(_SB).Device(PCI0) |
| `RSB0` | `NRSV` / SystemMemory | 0x10.0 | 32 bit | 0x70066010 | Scope(_SB).Device(PCI0) |
| `RSS0` | `NRSV` / SystemMemory | 0x18.0 | 32 bit | 0x70066018 | Scope(_SB).Device(PCI0) |
| `RSB1` | `NRSV` / SystemMemory | 0x20.0 | 32 bit | 0x70066020 | Scope(_SB).Device(PCI0) |
| `RSS1` | `NRSV` / SystemMemory | 0x28.0 | 32 bit | 0x70066028 | Scope(_SB).Device(PCI0) |
| `RSB2` | `NRSV` / SystemMemory | 0x30.0 | 32 bit | 0x70066030 | Scope(_SB).Device(PCI0) |
| `RSS2` | `NRSV` / SystemMemory | 0x38.0 | 32 bit | 0x70066038 | Scope(_SB).Device(PCI0) |
| `RSB3` | `NRSV` / SystemMemory | 0x40.0 | 32 bit | 0x70066040 | Scope(_SB).Device(PCI0) |
| `RSS3` | `NRSV` / SystemMemory | 0x48.0 | 32 bit | 0x70066048 | Scope(_SB).Device(PCI0) |
| `NAPX` | `VARM` / SystemMemory | 0x0.0 | 32 bit |  | Scope(_SB).Device(PCI0) / NAPE |
| `NAPD` | `VARM` / SystemMemory | 0x4.0 | 32 bit |  | Scope(_SB).Device(PCI0) / NAPE |
| `HPEN` | `PMI0` / SystemMemory | 0x0.6 | 1 bit | 0xFED80300 |  |
| `P1EB` | `PMI0` / SystemMemory | 0x60.0 | 16 bit | 0xFED80360 |  |
| `GS23` | `GSMG` / SystemMemory | 0x5E.0 | 1 bit | 0xFED8155E |  |
| `GV23` | `GSMG` / SystemMemory | 0x5E.6 | 1 bit | 0xFED8155E |  |
| `GE23` | `GSMG` / SystemMemory | 0x5E.7 | 1 bit | 0xFED8155E |  |
| `GS40` | `GSMG` / SystemMemory | 0xA2.0 | 1 bit | 0xFED815A2 |  |
| `GV40` | `GSMG` / SystemMemory | 0xA2.6 | 1 bit | 0xFED815A2 |  |
| `GE40` | `GSMG` / SystemMemory | 0xA2.7 | 1 bit | 0xFED815A2 |  |
| `CLPS` | `GSMM` / SystemMemory | 0x288.1 | 1 bit | 0xFED80288 |  |
| `TMSE` | `GSMM` / SystemMemory | 0x296.7 | 1 bit | 0xFED80296 |  |
| `SLPS` | `GSMM` / SystemMemory | 0x2B0.2 | 2 bit | 0xFED802B0 |  |
| `PWDE` | `PMI2` / SystemMemory | 0xBB.6 | 1 bit | 0xFED803BB |  |
| `SMIB` | `SMIC` / SystemMemory | 0x36A.0 | 8 bit | 0xFED8036A | Scope(_SB) |
| `PEBA` | `FRTP` / SystemMemory | 0x0.0 | 32 bit |  | Scope(_SB) |
| `IC0E` | `FRTP` / SystemMemory | 0x4.5 | 1 bit |  | Scope(_SB) |
| `IC1E` | `FRTP` / SystemMemory | 0x4.6 | 1 bit |  | Scope(_SB) |
| `IC2E` | `FRTP` / SystemMemory | 0x4.7 | 1 bit |  | Scope(_SB) |
| `IC3E` | `FRTP` / SystemMemory | 0x5.0 | 1 bit |  | Scope(_SB) |
| `IC4E` | `FRTP` / SystemMemory | 0x5.1 | 1 bit |  | Scope(_SB) |
| `IC5E` | `FRTP` / SystemMemory | 0x5.2 | 1 bit |  | Scope(_SB) |
| `UT0E` | `FRTP` / SystemMemory | 0x5.3 | 1 bit |  | Scope(_SB) |
| `UT1E` | `FRTP` / SystemMemory | 0x5.4 | 1 bit |  | Scope(_SB) |
| `I31E` | `FRTP` / SystemMemory | 0x5.5 | 1 bit |  | Scope(_SB) |
| `I32E` | `FRTP` / SystemMemory | 0x5.6 | 1 bit |  | Scope(_SB) |
| `I33E` | `FRTP` / SystemMemory | 0x5.7 | 1 bit |  | Scope(_SB) |
| `UT2E` | `FRTP` / SystemMemory | 0x6.0 | 1 bit |  | Scope(_SB) |
| `EMMD` | `FRTP` / SystemMemory | 0x6.2 | 2 bit |  | Scope(_SB) |
| `UT4E` | `FRTP` / SystemMemory | 0x6.4 | 1 bit |  | Scope(_SB) |
| `I30E` | `FRTP` / SystemMemory | 0x6.5 | 1 bit |  | Scope(_SB) |
| `XHCE` | `FRTP` / SystemMemory | 0x6.7 | 1 bit |  | Scope(_SB) |
| `UT3E` | `FRTP` / SystemMemory | 0x7.2 | 1 bit |  | Scope(_SB) |
| `ESPI` | `FRTP` / SystemMemory | 0x7.3 | 1 bit |  | Scope(_SB) |
| `EMME` | `FRTP` / SystemMemory | 0x7.4 | 1 bit |  | Scope(_SB) |
| `HFPE` | `FRTP` / SystemMemory | 0x7.5 | 1 bit |  | Scope(_SB) |
| `HD0E` | `FRTP` / SystemMemory | 0x7.6 | 1 bit |  | Scope(_SB) |
| `HD2E` | `FRTP` / SystemMemory | 0x7.7 | 1 bit |  | Scope(_SB) |
| `PCEF` | `FRTP` / SystemMemory | 0x8.0 | 1 bit |  | Scope(_SB) |
| `IC0D` | `FRTP` / SystemMemory | 0x8.5 | 1 bit |  | Scope(_SB) |
| `IC1D` | `FRTP` / SystemMemory | 0x8.6 | 1 bit |  | Scope(_SB) |
| `IC2D` | `FRTP` / SystemMemory | 0x8.7 | 1 bit |  | Scope(_SB) |
| `IC3D` | `FRTP` / SystemMemory | 0x9.0 | 1 bit |  | Scope(_SB) |
| `IC4D` | `FRTP` / SystemMemory | 0x9.1 | 1 bit |  | Scope(_SB) |
| `IC5D` | `FRTP` / SystemMemory | 0x9.2 | 1 bit |  | Scope(_SB) |
| `UT0D` | `FRTP` / SystemMemory | 0x9.3 | 1 bit |  | Scope(_SB) |
| `UT1D` | `FRTP` / SystemMemory | 0x9.4 | 1 bit |  | Scope(_SB) |
| `I31D` | `FRTP` / SystemMemory | 0x9.5 | 1 bit |  | Scope(_SB) |
| `I32D` | `FRTP` / SystemMemory | 0x9.6 | 1 bit |  | Scope(_SB) |
| `I33D` | `FRTP` / SystemMemory | 0x9.7 | 1 bit |  | Scope(_SB) |
| `UT2D` | `FRTP` / SystemMemory | 0xA.0 | 1 bit |  | Scope(_SB) |
| `EHCD` | `FRTP` / SystemMemory | 0xA.2 | 1 bit |  | Scope(_SB) |
| `UT4D` | `FRTP` / SystemMemory | 0xA.4 | 1 bit |  | Scope(_SB) |
| `I30D` | `FRTP` / SystemMemory | 0xA.5 | 1 bit |  | Scope(_SB) |
| `XHCD` | `FRTP` / SystemMemory | 0xA.7 | 1 bit |  | Scope(_SB) |
| `SD_D` | `FRTP` / SystemMemory | 0xB.0 | 1 bit |  | Scope(_SB) |
| `UT3D` | `FRTP` / SystemMemory | 0xB.2 | 1 bit |  | Scope(_SB) |
| `EMD3` | `FRTP` / SystemMemory | 0xB.4 | 1 bit |  | Scope(_SB) |
| `US4D` | `FRTP` / SystemMemory | 0xB.6 | 1 bit |  | Scope(_SB) |
| `S03D` | `FRTP` / SystemMemory | 0xB.7 | 1 bit |  | Scope(_SB) |
| `UT0O` | `FRTP` / SystemMemory | 0xC.0 | 1 bit |  | Scope(_SB) |
| `UT1O` | `FRTP` / SystemMemory | 0xC.1 | 1 bit |  | Scope(_SB) |
| `UT2O` | `FRTP` / SystemMemory | 0xC.2 | 1 bit |  | Scope(_SB) |
| `UT3O` | `FRTP` / SystemMemory | 0xC.3 | 1 bit |  | Scope(_SB) |
| `I30M` | `FRTP` / SystemMemory | 0x1A.0 | 1 bit |  | Scope(_SB) |
| `I31M` | `FRTP` / SystemMemory | 0x1A.1 | 1 bit |  | Scope(_SB) |
| `I32M` | `FRTP` / SystemMemory | 0x1A.2 | 1 bit |  | Scope(_SB) |
| `I33M` | `FRTP` / SystemMemory | 0x1A.3 | 1 bit |  | Scope(_SB) |
| `UT0I` | `FRTP` / SystemMemory | 0x2D.0 | 1 bit |  | Scope(_SB) |
| `UT1I` | `FRTP` / SystemMemory | 0x2D.1 | 1 bit |  | Scope(_SB) |
| `UT2I` | `FRTP` / SystemMemory | 0x2D.2 | 1 bit |  | Scope(_SB) |
| `UT3I` | `FRTP` / SystemMemory | 0x2D.3 | 1 bit |  | Scope(_SB) |
| `UT4I` | `FRTP` / SystemMemory | 0x2D.4 | 1 bit |  | Scope(_SB) |
| `UL0I` | `FRTP` / SystemMemory | 0x2E.0 | 1 bit |  | Scope(_SB) |
| `UL1I` | `FRTP` / SystemMemory | 0x2E.1 | 1 bit |  | Scope(_SB) |
| `UL2I` | `FRTP` / SystemMemory | 0x2E.2 | 1 bit |  | Scope(_SB) |
| `UL3I` | `FRTP` / SystemMemory | 0x2E.3 | 1 bit |  | Scope(_SB) |
| `LDQ0` | `FCFG` / SystemMemory | 0xA3078.2 | 1 bit |  | Scope(_SB) |
| `AUSS` | `FCFG` / SystemMemory | 0xA30CB.7 | 1 bit |  | Scope(_SB) |
| `IM15` | `IOMX` / SystemMemory | 0x15.0 | 8 bit | 0xFED80D15 | Scope(_SB) |
| `IM16` | `IOMX` / SystemMemory | 0x16.0 | 8 bit | 0xFED80D16 | Scope(_SB) |
| `IM1F` | `IOMX` / SystemMemory | 0x1F.0 | 8 bit | 0xFED80D1F | Scope(_SB) |
| `IM20` | `IOMX` / SystemMemory | 0x20.0 | 8 bit | 0xFED80D20 | Scope(_SB) |
| `IM44` | `IOMX` / SystemMemory | 0x44.0 | 8 bit | 0xFED80D44 | Scope(_SB) |
| `IM46` | `IOMX` / SystemMemory | 0x46.0 | 8 bit | 0xFED80D46 | Scope(_SB) |
| `IM4A` | `IOMX` / SystemMemory | 0x4A.0 | 8 bit | 0xFED80D4A | Scope(_SB) |
| `IM4B` | `IOMX` / SystemMemory | 0x4B.0 | 8 bit | 0xFED80D4B | Scope(_SB) |
| `IM57` | `IOMX` / SystemMemory | 0x57.0 | 8 bit | 0xFED80D57 | Scope(_SB) |
| `IM58` | `IOMX` / SystemMemory | 0x58.0 | 8 bit | 0xFED80D58 | Scope(_SB) |
| `IM68` | `IOMX` / SystemMemory | 0x68.0 | 8 bit | 0xFED80D68 | Scope(_SB) |
| `IM69` | `IOMX` / SystemMemory | 0x69.0 | 8 bit | 0xFED80D69 | Scope(_SB) |
| `IM6A` | `IOMX` / SystemMemory | 0x6A.0 | 8 bit | 0xFED80D6A | Scope(_SB) |
| `IM6B` | `IOMX` / SystemMemory | 0x6B.0 | 8 bit | 0xFED80D6B | Scope(_SB) |
| `IM6D` | `IOMX` / SystemMemory | 0x6D.0 | 8 bit | 0xFED80D6D | Scope(_SB) |
| `RD28` | `FACR` / SystemMemory | 0x83.4 | 1 bit | 0xFED81E83 | Scope(_SB) |
| `RQTY` | `FACR` / SystemMemory | 0x83.6 | 1 bit | 0xFED81E83 | Scope(_SB) |
| `SD28` | `FACR` / SystemMemory | 0x87.4 | 1 bit | 0xFED81E87 | Scope(_SB) |
| `PG1A` | `FACR` / SystemMemory | 0xA0.0 | 1 bit | 0xFED81EA0 | Scope(_SB) |
| `IER0` | `LUIE` / SystemMemory | 0x0.0 | 1 bit | 0xFEDC0020 | Scope(_SB) |
| `IER1` | `LUIE` / SystemMemory | 0x0.1 | 1 bit | 0xFEDC0020 | Scope(_SB) |
| `IER2` | `LUIE` / SystemMemory | 0x0.2 | 1 bit | 0xFEDC0020 | Scope(_SB) |
| `IER3` | `LUIE` / SystemMemory | 0x0.3 | 1 bit | 0xFEDC0020 | Scope(_SB) |
| `UOL0` | `LUIE` / SystemMemory | 0x0.4 | 1 bit | 0xFEDC0020 | Scope(_SB) |
| `UOL1` | `LUIE` / SystemMemory | 0x0.5 | 1 bit | 0xFEDC0020 | Scope(_SB) |
| `UOL2` | `LUIE` / SystemMemory | 0x0.6 | 1 bit | 0xFEDC0020 | Scope(_SB) |
| `UOL3` | `LUIE` / SystemMemory | 0x0.7 | 1 bit | 0xFEDC0020 | Scope(_SB) |
| `WUR0` | `LUIE` / SystemMemory | 0x1.0 | 2 bit | 0xFEDC0021 | Scope(_SB) |
| `WUR1` | `LUIE` / SystemMemory | 0x1.2 | 2 bit | 0xFEDC0021 | Scope(_SB) |
| `WUR2` | `LUIE` / SystemMemory | 0x1.4 | 2 bit | 0xFEDC0021 | Scope(_SB) |
| `WUR3` | `LUIE` / SystemMemory | 0x1.6 | 2 bit | 0xFEDC0021 | Scope(_SB) |
| `ADTD` | `ADCR` / SystemMemory | 0x0.0 | 2 bit |  | Scope(_SB) / SRAD |
| `ADPS` | `ADCR` / SystemMemory | 0x0.2 | 1 bit |  | Scope(_SB) / SRAD |
| `ADPD` | `ADCR` / SystemMemory | 0x0.3 | 1 bit |  | Scope(_SB) / SRAD |
| `ADSO` | `ADCR` / SystemMemory | 0x0.4 | 1 bit |  | Scope(_SB) / SRAD |
| `ADSC` | `ADCR` / SystemMemory | 0x0.5 | 1 bit |  | Scope(_SB) / SRAD |
| `ADSR` | `ADCR` / SystemMemory | 0x0.6 | 1 bit |  | Scope(_SB) / SRAD |
| `ADIS` | `ADCR` / SystemMemory | 0x0.7 | 1 bit |  | Scope(_SB) / SRAD |
| `ADDS` | `ADCR` / SystemMemory | 0x1.0 | 3 bit |  | Scope(_SB) / SRAD |
| `ADTD` | `ADCR` / SystemMemory | 0x0.0 | 2 bit |  | Scope(_SB) / DSAD |
| `ADPS` | `ADCR` / SystemMemory | 0x0.2 | 1 bit |  | Scope(_SB) / DSAD |
| `ADPD` | `ADCR` / SystemMemory | 0x0.3 | 1 bit |  | Scope(_SB) / DSAD |
| `ADSO` | `ADCR` / SystemMemory | 0x0.4 | 1 bit |  | Scope(_SB) / DSAD |
| `ADSC` | `ADCR` / SystemMemory | 0x0.5 | 1 bit |  | Scope(_SB) / DSAD |
| `ADSR` | `ADCR` / SystemMemory | 0x0.6 | 1 bit |  | Scope(_SB) / DSAD |
| `ADIS` | `ADCR` / SystemMemory | 0x0.7 | 1 bit |  | Scope(_SB) / DSAD |
| `ADDS` | `ADCR` / SystemMemory | 0x1.0 | 3 bit |  | Scope(_SB) / DSAD |
| `ADTD` | `ADCR` / SystemMemory | 0x0.0 | 2 bit |  | Scope(_SB) / HSAD |
| `ADPS` | `ADCR` / SystemMemory | 0x0.2 | 1 bit |  | Scope(_SB) / HSAD |
| `ADPD` | `ADCR` / SystemMemory | 0x0.3 | 1 bit |  | Scope(_SB) / HSAD |
| `ADSO` | `ADCR` / SystemMemory | 0x0.4 | 1 bit |  | Scope(_SB) / HSAD |
| `ADSC` | `ADCR` / SystemMemory | 0x0.5 | 1 bit |  | Scope(_SB) / HSAD |
| `ADSR` | `ADCR` / SystemMemory | 0x0.6 | 1 bit |  | Scope(_SB) / HSAD |
| `ADIS` | `ADCR` / SystemMemory | 0x0.7 | 1 bit |  | Scope(_SB) / HSAD |
| `ADDS` | `ADCR` / SystemMemory | 0x1.0 | 3 bit |  | Scope(_SB) / HSAD |
| `ACC0` | `TMMB` / SystemMemory | 0x0.0 | 8 bit | 0xFED40000 | Device(_SB.TPM) |
| `INTE` | `TMMB` / SystemMemory | 0x8.0 | 32 bit | 0xFED40008 | Device(_SB.TPM) |
| `INTV` | `TMMB` / SystemMemory | 0xC.0 | 8 bit | 0xFED4000C | Device(_SB.TPM) |
| `INTS` | `TMMB` / SystemMemory | 0x10.0 | 32 bit | 0xFED40010 | Device(_SB.TPM) |
| `INTF` | `TMMB` / SystemMemory | 0x14.0 | 32 bit | 0xFED40014 | Device(_SB.TPM) |
| `TSTS` | `TMMB` / SystemMemory | 0x18.0 | 32 bit | 0xFED40018 | Device(_SB.TPM) |
| `FIFO` | `TMMB` / SystemMemory | 0x24.0 | 32 bit | 0xFED40024 | Device(_SB.TPM) |
| `IDTF` | `TMMB` / SystemMemory | 0x30.0 | 32 bit | 0xFED40030 | Device(_SB.TPM) |
| `SCMD` | `TMMB` / SystemMemory | 0x4C.0 | 32 bit | 0xFED4004C | Device(_SB.TPM) |
| `HERR` | `CRBD` / SystemMemory | 0x4.0 | 32 bit | 0xFED40004 | Device(_SB.TPM) |
| `HCMD` | `CRBD` / SystemMemory | 0x40.0 | 32 bit | 0xFED40040 | Device(_SB.TPM) |
| `HSTS` | `CRBD` / SystemMemory | 0x44.0 | 32 bit | 0xFED40044 | Device(_SB.TPM) |
| `FERR` | `TPMR` / SystemMemory | 0x4.0 | 32 bit | 0x100000003 | Device(_SB.TPM) / STRT |
| `BEGN` | `TPMR` / SystemMemory | 0xC.0 | 32 bit | 0x10000000B | Device(_SB.TPM) / STRT |
| `RQST` | `ATNV` / SystemMemory | 0x0.0 | 32 bit | 0x6FFF4F18 | Scope(_SB.TPM) |
| `RCNT` | `ATNV` / SystemMemory | 0x4.0 | 32 bit | 0x6FFF4F1C | Scope(_SB.TPM) |
| `ERRO` | `ATNV` / SystemMemory | 0x8.0 | 32 bit | 0x6FFF4F20 | Scope(_SB.TPM) |
| `FLAG` | `ATNV` / SystemMemory | 0xC.0 | 32 bit | 0x6FFF4F24 | Scope(_SB.TPM) |
| `MISC` | `ATNV` / SystemMemory | 0x10.0 | 32 bit | 0x6FFF4F28 | Scope(_SB.TPM) |
| `OPTN` | `ATNV` / SystemMemory | 0x14.0 | 32 bit | 0x6FFF4F2C | Scope(_SB.TPM) |
| `SRSP` | `ATNV` / SystemMemory | 0x18.0 | 32 bit | 0x6FFF4F30 | Scope(_SB.TPM) |
| `XIF1` | `ECMG` / SystemMemory | 0x402.0 | 16 bit | 0xFED50402 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XIF2` | `ECMG` / SystemMemory | 0x404.0 | 16 bit | 0xFED50404 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XST0` | `ECMG` / SystemMemory | 0x432.0 | 16 bit | 0xFED50432 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XST1` | `ECMG` / SystemMemory | 0x434.0 | 16 bit | 0xFED50434 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XST2` | `ECMG` / SystemMemory | 0x436.0 | 16 bit | 0xFED50436 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XST3` | `ECMG` / SystemMemory | 0x438.0 | 16 bit | 0xFED50438 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CPTM` | `ECMG` / SystemMemory | 0x43E.0 | 8 bit | 0xFED5043E | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `VGAT` | `ECMG` / SystemMemory | 0x44F.0 | 8 bit | 0xFED5044F | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `FFAN` | `ECMG` / SystemMemory | 0x460.0 | 4 bit | 0xFED50460 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `S0E3` | `ECMG` / SystemMemory | 0x466.4 | 1 bit | 0xFED50466 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `S0E1` | `ECMG` / SystemMemory | 0x466.6 | 1 bit | 0xFED50466 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `SDAN` | `ECMG` / SystemMemory | 0x468.0 | 4 bit | 0xFED50468 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BSOK` | `ECMG` / SystemMemory | 0x490.2 | 1 bit | 0xFED50490 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `ISDB` | `ECMG` / SystemMemory | 0x497.0 | 1 bit | 0xFED50497 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `ADPT` | `ECMG` / SystemMemory | 0x49F.3 | 4 bit | 0xFED5049F | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CYCN` | `ECMG` / SystemMemory | 0x4A6.0 | 16 bit | 0xFED504A6 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BTP0` | `ECMG` / SystemMemory | 0x704.0 | 16 bit | 0xFED50704 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CUME` | `ECMG` / SystemMemory | 0x727.6 | 1 bit | 0xFED50727 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `TB43` | `ECMG` / SystemMemory | 0x728.0 | 1 bit | 0xFED50728 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CMSP` | `ECMG` / SystemMemory | 0x728.1 | 1 bit | 0xFED50728 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CSMS` | `ECMG` / SystemMemory | 0x72B.1 | 1 bit | 0xFED5072B | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DXDB` | `ECMG` / SystemMemory | 0x72B.5 | 3 bit | 0xFED5072B | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `EYER` | `ECMG` / SystemMemory | 0x72D.0 | 8 bit | 0xFED5072D | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `EMON` | `ECMG` / SystemMemory | 0x72E.0 | 8 bit | 0xFED5072E | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `HCPT` | `ECMG` / SystemMemory | 0x73A.0 | 8 bit | 0xFED5073A | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `GNEN` | `ECMG` / SystemMemory | 0x743.0 | 1 bit | 0xFED50743 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `ECDC` | `ECMG` / SystemMemory | 0x743.1 | 1 bit | 0xFED50743 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CTVA` | `ECMG` / SystemMemory | 0x744.0 | 8 bit | 0xFED50744 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DBCT` | `ECMG` / SystemMemory | 0x745.0 | 8 bit | 0xFED50745 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MXDB` | `ECMG` / SystemMemory | 0x746.0 | 8 bit | 0xFED50746 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MIDB` | `ECMG` / SystemMemory | 0x747.0 | 8 bit | 0xFED50747 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `PDIN` | `ECMG` / SystemMemory | 0x74C.0 | 4 bit | 0xFED5074C | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `TBME` | `ECMG` / SystemMemory | 0x751.4 | 1 bit | 0xFED50751 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `UFME` | `ECMG` / SystemMemory | 0x751.7 | 1 bit | 0xFED50751 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `TPID` | `ECMG` / SystemMemory | 0x760.0 | 8 bit | 0xFED50760 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `OCP1` | `ECMG` / SystemMemory | 0x763.0 | 8 bit | 0xFED50763 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `OCP2` | `ECMG` / SystemMemory | 0x764.0 | 8 bit | 0xFED50764 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `APL1` | `ECMG` / SystemMemory | 0x783.0 | 8 bit | 0xFED50783 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `APL2` | `ECMG` / SystemMemory | 0x784.0 | 8 bit | 0xFED50784 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `APL4` | `ECMG` / SystemMemory | 0x785.0 | 8 bit | 0xFED50785 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `APTC` | `ECMG` / SystemMemory | 0x786.0 | 7 bit | 0xFED50786 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `APTN` | `ECMG` / SystemMemory | 0x786.7 | 1 bit | 0xFED50786 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CTWA` | `ECMG` / SystemMemory | 0x788.0 | 8 bit | 0xFED50788 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `GC6S` | `ECMG` / SystemMemory | 0x7A4.2 | 1 bit | 0xFED507A4 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CGLM` | `ECMG` / SystemMemory | 0x7B9.0 | 7 bit | 0xFED507B9 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DBEN` | `ECMG` / SystemMemory | 0x7C4.3 | 1 bit | 0xFED507C4 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DBST` | `ECMG` / SystemMemory | 0x7C4.5 | 1 bit | 0xFED507C4 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `WHMS` | `ECMG` / SystemMemory | 0x7C5.5 | 1 bit | 0xFED507C5 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `WMS0` | `ECMG` / SystemMemory | 0x7C6.0 | 2 bit | 0xFED507C6 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `LCSE` | `ECMG` / SystemMemory | 0x7C7.1 | 1 bit | 0xFED507C7 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `OCPL` | `ECMG` / SystemMemory | 0x7C7.2 | 3 bit | 0xFED507C7 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DC0C` | `ECMG` / SystemMemory | 0x7CC.0 | 1 bit | 0xFED507CC | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `TC1C` | `ECMG` / SystemMemory | 0x7CC.1 | 1 bit | 0xFED507CC | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `TC2C` | `ECMG` / SystemMemory | 0x7CC.2 | 1 bit | 0xFED507CC | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `TC1P` | `ECMG` / SystemMemory | 0x7CC.4 | 1 bit | 0xFED507CC | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `TC2P` | `ECMG` / SystemMemory | 0x7CC.5 | 1 bit | 0xFED507CC | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `TCSA` | `ECMG` / SystemMemory | 0x7CC.7 | 1 bit | 0xFED507CC | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DBD1` | `ECMG` / SystemMemory | 0x7D0.0 | 8 bit | 0xFED507D0 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DBD2` | `ECMG` / SystemMemory | 0x7D1.0 | 8 bit | 0xFED507D1 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `GFID` | `ECMG` / SystemMemory | 0x7D2.0 | 5 bit | 0xFED507D2 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CPUA` | `ECMG` / SystemMemory | 0x7D4.0 | 8 bit | 0xFED507D4 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DBAP` | `ECMG` / SystemMemory | 0x7D5.0 | 8 bit | 0xFED507D5 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DBSP` | `ECMG` / SystemMemory | 0x7D6.0 | 8 bit | 0xFED507D6 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CGCT` | `ECMG` / SystemMemory | 0x7D7.0 | 8 bit | 0xFED507D7 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `ETPP` | `ECMG` / SystemMemory | 0x7F7.0 | 8 bit | 0xFED507F7 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `UVER` | `ECMG` / SystemMemory | 0xEA0.0 | 16 bit | 0xFED50EA0 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `RESV` | `ECMG` / SystemMemory | 0xEA2.0 | 16 bit | 0xFED50EA2 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CCI0` | `ECMG` / SystemMemory | 0xEA4.0 | 8 bit | 0xFED50EA4 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CCI1` | `ECMG` / SystemMemory | 0xEA5.0 | 8 bit | 0xFED50EA5 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CCI2` | `ECMG` / SystemMemory | 0xEA6.0 | 8 bit | 0xFED50EA6 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CCI3` | `ECMG` / SystemMemory | 0xEA7.0 | 8 bit | 0xFED50EA7 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CTL0` | `ECMG` / SystemMemory | 0xEA8.0 | 8 bit | 0xFED50EA8 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CTL1` | `ECMG` / SystemMemory | 0xEA9.0 | 8 bit | 0xFED50EA9 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CTL2` | `ECMG` / SystemMemory | 0xEAA.0 | 8 bit | 0xFED50EAA | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CTL3` | `ECMG` / SystemMemory | 0xEAB.0 | 8 bit | 0xFED50EAB | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CTL4` | `ECMG` / SystemMemory | 0xEAC.0 | 8 bit | 0xFED50EAC | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CTL5` | `ECMG` / SystemMemory | 0xEAD.0 | 8 bit | 0xFED50EAD | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CTL6` | `ECMG` / SystemMemory | 0xEAE.0 | 8 bit | 0xFED50EAE | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CTL7` | `ECMG` / SystemMemory | 0xEAF.0 | 8 bit | 0xFED50EAF | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGI0` | `ECMG` / SystemMemory | 0xEB0.0 | 8 bit | 0xFED50EB0 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGI1` | `ECMG` / SystemMemory | 0xEB1.0 | 8 bit | 0xFED50EB1 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGI2` | `ECMG` / SystemMemory | 0xEB2.0 | 8 bit | 0xFED50EB2 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGI3` | `ECMG` / SystemMemory | 0xEB3.0 | 8 bit | 0xFED50EB3 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGI4` | `ECMG` / SystemMemory | 0xEB4.0 | 8 bit | 0xFED50EB4 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGI5` | `ECMG` / SystemMemory | 0xEB5.0 | 8 bit | 0xFED50EB5 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGI6` | `ECMG` / SystemMemory | 0xEB6.0 | 8 bit | 0xFED50EB6 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGI7` | `ECMG` / SystemMemory | 0xEB7.0 | 8 bit | 0xFED50EB7 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGI8` | `ECMG` / SystemMemory | 0xEB8.0 | 8 bit | 0xFED50EB8 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGI9` | `ECMG` / SystemMemory | 0xEB9.0 | 8 bit | 0xFED50EB9 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGIA` | `ECMG` / SystemMemory | 0xEBA.0 | 8 bit | 0xFED50EBA | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGIB` | `ECMG` / SystemMemory | 0xEBB.0 | 8 bit | 0xFED50EBB | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGIC` | `ECMG` / SystemMemory | 0xEBC.0 | 8 bit | 0xFED50EBC | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGID` | `ECMG` / SystemMemory | 0xEBD.0 | 8 bit | 0xFED50EBD | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGIE` | `ECMG` / SystemMemory | 0xEBE.0 | 8 bit | 0xFED50EBE | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGIF` | `ECMG` / SystemMemory | 0xEBF.0 | 8 bit | 0xFED50EBF | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGO0` | `ECMG` / SystemMemory | 0xEC0.0 | 8 bit | 0xFED50EC0 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGO1` | `ECMG` / SystemMemory | 0xEC1.0 | 8 bit | 0xFED50EC1 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGO2` | `ECMG` / SystemMemory | 0xEC2.0 | 8 bit | 0xFED50EC2 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGO3` | `ECMG` / SystemMemory | 0xEC3.0 | 8 bit | 0xFED50EC3 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGO4` | `ECMG` / SystemMemory | 0xEC4.0 | 8 bit | 0xFED50EC4 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGO5` | `ECMG` / SystemMemory | 0xEC5.0 | 8 bit | 0xFED50EC5 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGO6` | `ECMG` / SystemMemory | 0xEC6.0 | 8 bit | 0xFED50EC6 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGO7` | `ECMG` / SystemMemory | 0xEC7.0 | 8 bit | 0xFED50EC7 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGO8` | `ECMG` / SystemMemory | 0xEC8.0 | 8 bit | 0xFED50EC8 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGO9` | `ECMG` / SystemMemory | 0xEC9.0 | 8 bit | 0xFED50EC9 | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGOA` | `ECMG` / SystemMemory | 0xECA.0 | 8 bit | 0xFED50ECA | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGOB` | `ECMG` / SystemMemory | 0xECB.0 | 8 bit | 0xFED50ECB | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGOC` | `ECMG` / SystemMemory | 0xECC.0 | 8 bit | 0xFED50ECC | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGOD` | `ECMG` / SystemMemory | 0xECD.0 | 8 bit | 0xFED50ECD | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGOE` | `ECMG` / SystemMemory | 0xECE.0 | 8 bit | 0xFED50ECE | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MGOF` | `ECMG` / SystemMemory | 0xECF.0 | 8 bit | 0xFED50ECF | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MM08` | `MMNM` / SystemMemory | 0x0.0 | 8 bit |  | Scope(_SB).Device(INOU) / MMRW |
| `MM16` | `MMNM` / SystemMemory | 0x0.0 | 16 bit |  | Scope(_SB).Device(INOU) / MMRW |
| `MM32` | `MMNM` / SystemMemory | 0x0.0 | 32 bit |  | Scope(_SB).Device(INOU) / MMRW |

## EmbeddedControl, SystemIO и индексные/банковые поля

| Объект | Регион / space | Смещение | Ширина | Context |
|---|---|---:|---:|---|
| `DBG8` | `DEB0` / SystemIO | 0x0.0 | 32 bit |  |
| `DBGX` | `DEB3` / SystemIO | 0x0.0 | 8 bit |  |
| `DBG9` | `DEB1` / SystemIO | 0x0.0 | 16 bit |  |
| `SMIC` | `SMI0` / SystemIO | 0x0.0 | 8 bit | Scope(_SB).Device(PCI0).Device(SBRG) |
| `PIDX` | `PIRQ` / SystemIO | 0x0.0 | 8 bit | Scope(_SB) |
| `PDAT` | `PIRQ` / SystemIO | 0x1.0 | 8 bit | Scope(_SB) |
| `IndexField` | `PIDX` / indirect | runtime | dynamic bit | Scope(_SB) |
| `PD64` | `KBDD` / SystemIO | 0x0.0 | 8 bit | Scope(_SB) |
| `IO80` | `DBG0` / SystemIO | 0x0.0 | 8 bit |  |
| `P80H` | `DBG1` / SystemIO | 0x0.0 | 32 bit |  |
| `PC80` | `PCPT` / SystemIO | 0x0.0 | 32 bit |  |
| `APMC` | `PSMI` / SystemIO | 0x0.0 | 8 bit |  |
| `APMD` | `PSMI` / SystemIO | 0x1.0 | 8 bit |  |
| `PEWS` | `P1E0` / SystemIO | 0x1.6 | 1 bit |  |
| `WSTA` | `P1E0` / SystemIO | 0x1.7 | 1 bit |  |
| `PEWD` | `P1E0` / SystemIO | 0x3.6 | 1 bit |  |
| `RTCS` | `IOCC` / SystemIO | 0x1.2 | 1 bit |  |
| `SMIW` | `SSMI` / SystemIO | 0x0.0 | 16 bit | Scope(_SB) |
| `ECMI` | `ECMC` / SystemIO | 0x0.0 | 8 bit | Scope(_SB) |
| `ECMD` | `ECMC` / SystemIO | 0x1.0 | 8 bit | Scope(_SB) |
| `IndexField` | `ECMI` / indirect | runtime | dynamic bit | Scope(_SB) |
| `FPII` | `FPIC` / SystemIO | 0x0.0 | 8 bit | Scope(_SB) |
| `FPID` | `FPIC` / SystemIO | 0x1.0 | 8 bit | Scope(_SB) |
| `IndexField` | `FPII` / indirect | runtime | dynamic bit | Scope(_SB) |
| `SMI` | `TSMI` / SystemIO | 0x0.0 | 16 bit | Scope(_SB.TPM) |
| `IND1` | `CMS1` / SystemIO | 0x0.0 | 8 bit |  |
| `DAT1` | `CMS1` / SystemIO | 0x1.0 | 8 bit |  |
| `IndexField` | `IND1` / indirect | runtime | dynamic bit |  |
| `CMD0` | `IO` / SystemIO | 0x0.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CMD2` | `IO` / SystemIO | 0x2.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CMD4` | `IO` / SystemIO | 0x4.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CMD6` | `IO` / SystemIO | 0x6.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `KBDT` | `IO` / SystemIO | 0x0.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `ECDT` | `IO` / SystemIO | 0x2.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `OUTS` | `IO` / SystemIO | 0x4.0 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `INPS` | `IO` / SystemIO | 0x4.1 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `OUT6` | `IO` / SystemIO | 0x6.0 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `INP6` | `IO` / SystemIO | 0x6.1 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DEVS` | `ECMP` / EmbeddedControl | 0x7B.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XIF0` | `ECXP` / EmbeddedControl | 0x0.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XIF3` | `ECXP` / EmbeddedControl | 0x6.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XIF4` | `ECXP` / EmbeddedControl | 0x8.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XIF5` | `ECXP` / EmbeddedControl | 0xA.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XIF6` | `ECXP` / EmbeddedControl | 0xC.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XIF7` | `ECXP` / EmbeddedControl | 0xE.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XIF8` | `ECXP` / EmbeddedControl | 0x10.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XIF9` | `ECXP` / EmbeddedControl | 0x12.0 | 64 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XIFA` | `ECXP` / EmbeddedControl | 0x1A.0 | 64 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XIFB` | `ECXP` / EmbeddedControl | 0x22.0 | 64 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XIFC` | `ECXP` / EmbeddedControl | 0x2A.0 | 64 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XTP0` | `ECXP` / EmbeddedControl | 0x3A.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XCIN` | `ECXP` / EmbeddedControl | 0x3C.0 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XTIN` | `ECXP` / EmbeddedControl | 0x3C.2 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BLLV` | `ECXP` / EmbeddedControl | 0x3D.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XHPP` | `ECXP` / EmbeddedControl | 0x3E.0 | 7 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XSEC` | `ECXP` / EmbeddedControl | 0x40.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XLPT` | `ECXP` / EmbeddedControl | 0x41.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `Q70D` | `ECXP` / EmbeddedControl | 0x42.3 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `Q70M` | `ECXP` / EmbeddedControl | 0x42.4 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XBRL` | `ECXP` / EmbeddedControl | 0x43.0 | 3 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XBKL` | `ECXP` / EmbeddedControl | 0x43.7 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `VGTP` | `ECXP` / EmbeddedControl | 0x56.0 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BLSC` | `ECXP` / EmbeddedControl | 0x56.5 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `MDTP` | `ECXP` / EmbeddedControl | 0x56.6 | 2 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `EIST` | `ECXP` / EmbeddedControl | 0x58.7 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DEXS` | `ECXP` / EmbeddedControl | 0x5A.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XSLT` | `ECXP` / EmbeddedControl | 0x5B.0 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `GC6I` | `ECXP` / EmbeddedControl | 0x5B.3 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `GPST` | `ECXP` / EmbeddedControl | 0x5B.4 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `TCOS` | `ECXP` / EmbeddedControl | 0x63.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `TURB` | `ECXP` / EmbeddedControl | 0x66.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `PL1L` | `ECXP` / EmbeddedControl | 0x6A.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `PL2L` | `ECXP` / EmbeddedControl | 0x6B.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `PL3L` | `ECXP` / EmbeddedControl | 0x6E.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `PL4L` | `ECXP` / EmbeddedControl | 0x6F.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `XS4F` | `ECXP` / EmbeddedControl | 0x72.4 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `SLID` | `ECXP` / EmbeddedControl | 0x76.1 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `S3WA` | `ECXP` / EmbeddedControl | 0x76.3 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `HPEN` | `ECXP` / EmbeddedControl | 0x76.5 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `VDDT` | `ECXP` / EmbeddedControl | 0x77.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `VDDE` | `ECXP` / EmbeddedControl | 0x78.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `SOCT` | `ECXP` / EmbeddedControl | 0x79.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `SOCE` | `ECXP` / EmbeddedControl | 0x7A.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `TOPD` | `ECXP` / EmbeddedControl | 0x7B.1 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `WUSB` | `ECXP` / EmbeddedControl | 0x7B.2 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `FGPT` | `ECXP` / EmbeddedControl | 0x7B.3 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `WEBC` | `ECXP` / EmbeddedControl | 0x7B.4 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BLTH` | `ECXP` / EmbeddedControl | 0x7B.5 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DV3G` | `ECXP` / EmbeddedControl | 0x7B.6 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `WLAN` | `ECXP` / EmbeddedControl | 0x7B.7 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `OSDF` | `ECXP` / EmbeddedControl | 0x7C.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CGVT` | `ECXP` / EmbeddedControl | 0x7D.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `LDAT` | `ECXP` / EmbeddedControl | 0x8A.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `HDAT` | `ECXP` / EmbeddedControl | 0x8B.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `RFLG` | `ECXP` / EmbeddedControl | 0x8C.0 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `WFLG` | `ECXP` / EmbeddedControl | 0x8C.1 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BFLG` | `ECXP` / EmbeddedControl | 0x8C.2 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CFLG` | `ECXP` / EmbeddedControl | 0x8C.3 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `DRDY` | `ECXP` / EmbeddedControl | 0x8C.7 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CMDL` | `ECXP` / EmbeddedControl | 0x8D.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `CMDH` | `ECXP` / EmbeddedControl | 0x8E.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `ACIC` | `ECXP` / EmbeddedControl | 0x90.0 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BPST` | `ECXP` / EmbeddedControl | 0x90.1 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `ACWT` | `ECXP` / EmbeddedControl | 0x9F.0 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `HTMF` | `ECXP` / EmbeddedControl | 0x9F.1 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `ACFL` | `ECXP` / EmbeddedControl | 0x9F.2 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `RSOC` | `ECXP` / EmbeddedControl | 0xAB.0 | 8 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIF0` | `ECXP` / EmbeddedControl | 0xC0.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIF1` | `ECXP` / EmbeddedControl | 0xC2.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIF2` | `ECXP` / EmbeddedControl | 0xC4.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIF3` | `ECXP` / EmbeddedControl | 0xC6.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIF4` | `ECXP` / EmbeddedControl | 0xC8.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIF5` | `ECXP` / EmbeddedControl | 0xCA.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIF6` | `ECXP` / EmbeddedControl | 0xCC.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIF7` | `ECXP` / EmbeddedControl | 0xCE.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIF8` | `ECXP` / EmbeddedControl | 0xD0.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIF9` | `ECXP` / EmbeddedControl | 0xD2.0 | 64 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIFA` | `ECXP` / EmbeddedControl | 0xDA.0 | 64 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIFB` | `ECXP` / EmbeddedControl | 0xE2.0 | 64 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BIFC` | `ECXP` / EmbeddedControl | 0xEA.0 | 64 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BST0` | `ECXP` / EmbeddedControl | 0xF2.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BST1` | `ECXP` / EmbeddedControl | 0xF4.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BST2` | `ECXP` / EmbeddedControl | 0xF6.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BST3` | `ECXP` / EmbeddedControl | 0xF8.0 | 16 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `ACIN` | `ECXP` / EmbeddedControl | 0xFC.0 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `BTIN` | `ECXP` / EmbeddedControl | 0xFC.2 | 1 bit | Scope(_SB.PCI0.SBRG).Device(EC0) |
| `IO80` | `DBG8` / SystemIO | 0x0.0 | 8 bit | Scope(_SB).Device(AMW0) |
| `IO32` | `IOSP` / SystemIO | 0x0.0 | 32 bit | Scope(_SB).Device(INOU) / IORD |
| `IW32` | `IOSW` / SystemIO | 0x0.0 | 32 bit | Scope(_SB).Device(INOU) / IOWD |
| `INDX` | `CMS2` / SystemIO | 0x0.0 | 8 bit | Scope(_SB).Device(INOU) / RCMS |
| `DATA` | `CMS2` / SystemIO | 0x1.0 | 8 bit | Scope(_SB).Device(INOU) / RCMS |
| `INDX` | `CMS2` / SystemIO | 0x0.0 | 8 bit | Scope(_SB).Device(INOU) / WCMS |
| `DATA` | `CMS2` / SystemIO | 0x1.0 | 8 bit | Scope(_SB).Device(INOU) / WCMS |
| `IND0` | `RI01` / SystemIO | 0x0.0 | 8 bit | Scope(_SB).Device(INOU) / RIOP |
| `DAT0` | `RI01` / SystemIO | 0x1.0 | 8 bit | Scope(_SB).Device(INOU) / RIOP |
| `IndexField` | `IND0` / indirect | runtime | dynamic bit | Scope(_SB).Device(INOU) / RIOP |
| `IND1` | `WI01` / SystemIO | 0x0.0 | 8 bit | Scope(_SB).Device(INOU) / WIOP |
| `DAT1` | `WI01` / SystemIO | 0x1.0 | 8 bit | Scope(_SB).Device(INOU) / WIOP |
| `IndexField` | `IND1` / indirect | runtime | dynamic bit | Scope(_SB).Device(INOU) / WIOP |
| `SMIC` | `PSMI` / SystemIO | 0x0.0 | 8 bit | Scope(_SB.PCI0) / UPWD |
| `SMID` | `PSMI` / SystemIO | 0x1.0 | 8 bit | Scope(_SB.PCI0) / UPWD |

## EC-значения, связанные с температурой

| Поле | Адрес / пространство | Что позволяет утверждать DSDT |
|---|---|---|
| `CPTM` | 0xFED5043E / SystemMemory | Комментарий/потребитель DSDT обозначает как CPU thermal-control temperature |
| `VGAT` | 0xFED5044F / SystemMemory | Поле есть; само объявление не задаёт единицу или масштаб |
| `XHPP` | 0x3E / EmbeddedControl | 7-битное поле, используемое `ECTZ._TMP`; это не отдельное именованное поле NPU |
| `VDDT` | 0x77 / EmbeddedControl | Имя поля не доказывает температуру или единицы |
| `VDDE` | 0x78 / EmbeddedControl | Имя поля не доказывает температуру или единицы |
| `SOCT` | 0x79 / EmbeddedControl | Имя поля не доказывает температуру SoC или единицы |
| `SOCE` | 0x7A / EmbeddedControl | Имя поля не доказывает температуру SoC или единицы |

Полный перечень, включая Scope, смещения, зарезервированные биты и строки источника, сохранён в [`dsdt-fields.csv`](dsdt-fields.csv). Все OperationRegion перечислены в [`dsdt-regions.csv`](dsdt-regions.csv).
