# Внешние бинарные зависимости MagicControl

Git содержит исходный код и текстовую документацию. EXE, DLL, SYS,
драйверы, прошивки, шрифты, изображения и результаты сборки хранятся
локально отдельно и не повторяются в каждом коммите.

| Что находится локально | Назначение |
|---|---|
| `src/platform/windows/runtime/` | Подписанные OEM-инструменты и драйверы Windows |
| `src/platform/linux/runtime/` | Внешние Linux-утилиты |
| `src/gui/assets/fonts/` | Шрифты интерфейса |
| `MagicControl/windows/`, `MagicControl/linux/` | Рабочие portable GUI/CLI |
| `GitHub_Releases/` | Готовые архивы и шаблоны упаковки |
| `research/` | Локальные дампы и исследовательские бинарники |

Для сборки из свежего клона внешние зависимости нужно отдельно вернуть
в эти пути, сверив SHA-256 и provenance. Лицензии и текстовые описания
компонентов остаются в исходниках. Неизвестная версия не угадывается.

## Windows и Linux runtime

| Файл | Версия | SHA-256 | Описание / источник |
|---|---|---|---|
| `src/platform/linux/runtime/NvFlash/nvflash` | пакет 5.867.0 | `754693fb8f833be75601f95cb08a6e0cf538053cce84ab69020ec74ae96a6e70` | [PROVENANCE.json](../src/platform/linux/runtime/NvFlash/PROVENANCE.json) · [скачать](https://www.computerbase.de/downloads/systemtools/grafikkarten/nvflash/) |
| `src/platform/windows/runtime/AmiAfu/AFUWINx64.exe` | пакет 5.17.00.0153 | `535633b1c097581f34d31c3e2b9c594779c6e88dfd52cf3c02ac72c1b3e6649b` | [PROVENANCE.json](../src/platform/windows/runtime/AmiAfu/PROVENANCE.json) · [скачать](https://f.hubspotusercontent10.net/hubfs/9443417/Support/BIOS_Firmware_Update/Aptio_V_AMI_Firmware_Update_Utility.zip) |
| `src/platform/windows/runtime/AmiAfu/amigendrv64.sys` | 10.0.10011.16384 | `4892914632d4a8c327e6963b8a841266d6cdc466af03603b99d486799d947b64` | [PROVENANCE.json](../src/platform/windows/runtime/AmiAfu/PROVENANCE.json) · [скачать](https://f.hubspotusercontent10.net/hubfs/9443417/Support/BIOS_Firmware_Update/Aptio_V_AMI_Firmware_Update_Utility.zip) |
| `src/platform/windows/runtime/InsydeH2offt/BiosImageProcx64.dll` | 1.0.0.1 | `e8587d0a603027915954588a4646188ed5f688c641784f0b19eaeafdf444ea38` | [PROVENANCE.json](../src/platform/windows/runtime/InsydeH2offt/PROVENANCE.json) · [скачать](https://download.lenovo.com/consumer/mobiles/kscn43ww.exe) |
| `src/platform/windows/runtime/InsydeH2offt/FlsHook.exe` | 1.0.0.9 | `8203ed25c7007eaee9a69a45b3255c99ef607287f0a64dbccd0ba4bec8ad9a74` | [PROVENANCE.json](../src/platform/windows/runtime/InsydeH2offt/PROVENANCE.json) · [скачать](https://download.lenovo.com/consumer/mobiles/kscn43ww.exe) |
| `src/platform/windows/runtime/InsydeH2offt/H2OFFT-Wx64.exe` | 6.76 | `046514122b241581a61f7c3fda9e5d444b76de7f919efc89ab3ad2eb5d3a7de5` | [PROVENANCE.json](../src/platform/windows/runtime/InsydeH2offt/PROVENANCE.json) · [скачать](https://download.lenovo.com/consumer/mobiles/kscn43ww.exe) |
| `src/platform/windows/runtime/InsydeH2offt/H2OFFT.cat` | пакет 6.76 | `822055e3d0a6573d6604793aa1956de51ada3d7bc3c2893e128ce49e236d93d7` | [PROVENANCE.json](../src/platform/windows/runtime/InsydeH2offt/PROVENANCE.json) · [скачать](https://download.lenovo.com/consumer/mobiles/kscn43ww.exe) |
| `src/platform/windows/runtime/InsydeH2offt/H2OFFT64.sys` | 3.0.0.3 | `adac8f1ed5e46a8bcbdeb54e6ee08bf3695c65f2ab5e2b31cf1d438babf54463` | [PROVENANCE.json](../src/platform/windows/runtime/InsydeH2offt/PROVENANCE.json) · [скачать](https://download.lenovo.com/consumer/mobiles/kscn43ww.exe) |
| `src/platform/windows/runtime/IntelFpt/FPTW64.exe` | 16.1.27.2181 | `41fa119d68f15dc567df37d9513f433507680a6cdea200ac0cdff9cc88c68aae` | [PROVENANCE.json](../src/platform/windows/runtime/IntelFpt/PROVENANCE.json) · [скачать](https://download.gigabyte.com/FileList/BIOS/nb-bios-g5_g7-vf-win11-64bit-ddr4-bios-fb10.zip) |
| `src/platform/windows/runtime/IntelFpt/driver-reference.bin` | 1.0.0.105 | `33abb3e6d36a101c89096d8d30173c79a9efe84f7780fc701fab35e398ac58b2` | [PROVENANCE.json](../src/platform/windows/runtime/IntelFpt/PROVENANCE.json) · [скачать](https://download.gigabyte.com/FileList/BIOS/nb-bios-g5_g7-vf-win11-64bit-ddr4-bios-fb10.zip) |
| `src/platform/windows/runtime/IntelFpt19/FPTW64.exe` | 19.0.5.1948 | `88491119862830235854cd29b9857de21870ac4dc16f74544b1a665f64d62807` | [PROVENANCE.json](../src/platform/windows/runtime/IntelFpt19/PROVENANCE.json) · [скачать](https://download.colorful.cn/iGameCenter/iGameCenter-2.0.0.82-2026.09.12-Installer_Prod.exe) |
| `src/platform/windows/runtime/IntelFpt19/driver-reference.bin` | пакет 19.0.5.1948 | `acd5d366a73e9d60c654f9b6d76397d0439606939d6b0c7ee24c4f397452899a` | [PROVENANCE.json](../src/platform/windows/runtime/IntelFpt19/PROVENANCE.json) · [скачать](https://download.colorful.cn/iGameCenter/iGameCenter-2.0.0.82-2026.09.12-Installer_Prod.exe) |
| `src/platform/windows/runtime/LenovoEnergy/LenovoEnergySetup.exe` | 15.11.29.70 | `884163897dd3a8315f1984aa14862421f59e96e9ab7e11bb088124c7aa00989f` | [NOTICE.txt](../src/platform/windows/runtime/LenovoEnergy/NOTICE.txt) |
| `src/platform/windows/runtime/NvFlash/driver-1.reference.bin` | пакет 5.867.0 | `79050454d06ab7b6607bca09386f704df021c3c367cc0743de6845301bb192ac` | [PROVENANCE.json](../src/platform/windows/runtime/NvFlash/PROVENANCE.json) · [скачать](https://www.computerbase.de/downloads/systemtools/grafikkarten/nvflash/) |
| `src/platform/windows/runtime/NvFlash/driver-2.reference.bin` | пакет 5.867.0 | `8c2d9847b70c3dc1aac5d9e5330c67deb86f369dcd3ca85845adf2a001088dde` | [PROVENANCE.json](../src/platform/windows/runtime/NvFlash/PROVENANCE.json) · [скачать](https://www.computerbase.de/downloads/systemtools/grafikkarten/nvflash/) |
| `src/platform/windows/runtime/NvFlash/nvflash64.exe` | 5.867.0 | `c0620a7be0ffdad3782d9ddf238d1a19c31b587cbcfee7036819c8a50e2d89cd` | [PROVENANCE.json](../src/platform/windows/runtime/NvFlash/PROVENANCE.json) · [скачать](https://www.computerbase.de/downloads/systemtools/grafikkarten/nvflash/) |
| `src/platform/windows/runtime/PawnIO/AMDFamily17.bin` | не указана | `dae74615761b78bdf064dfb3e136252ddcc6fc727d88f14738d0e5800d427a91` | источник не указан |
| `src/platform/windows/runtime/PawnIO/IntelMSR.bin` | не указана | `d6ed85d65ab17a22f813ef98207d6d537155ee2ded5976a21cb48413c9b92e5f` | источник не указан |
| `src/platform/windows/runtime/PawnIO/IntelPCHThermal.bin` | не указана | `478500ae78aebef3ae7a0b5fe4848d2d5737e8f7c5d51200c284a61c21d7865d` | источник не указан |
| `src/platform/windows/runtime/PawnIO/LpcIO.bin` | не указана | `b3896a1cab0d808fca31fe2ebcae045d59dac690da87b17c858bb8da357eb45e` | источник не указан |
| `src/platform/windows/runtime/PawnIO/PawnIOLib.dll` | 2.2.0 | `d71f62627d66983bb9f5b1c269f27bcd8c1b8a46e794377a6330f84c198f4443` | источник не указан |
| `src/platform/windows/runtime/PawnIO/PawnIO_setup.exe` | 2.2.0.0 | `1f519a22e47187f70a1379a48ca604981c4fcf694f4e65b734aaa74a9fba3032` | источник не указан |
| `src/platform/windows/runtime/PawnIO/RyzenSMU.bin` | не указана | `301d9ca397108e09f31bfbd5ac4c9bb4f352a5de68532c32db3ba7ddcde93450` | источник не указан |
| `src/platform/windows/runtime/PawnIO/SmbusI801.bin` | не указана | `a0f7d066e7efda28c0e754c1f52dbd8dc280d388ba8575b514a80cb67490530c` | источник не указан |
| `src/platform/windows/runtime/PawnIO/SmbusPIIX4.bin` | не указана | `91f9b4b1c39e3d399ce48477a89d8f6bd3e58a2241064a4daec3a1513dff56e5` | источник не указан |
| `src/platform/windows/runtime/Uniwill/11.2.18.347/UWACPIDriver.cat` | пакет 11.2.18.347 | `97df04eab235f33e21e9a44b5c55a1c8b17b245d658664a3a8aa7b5e1c577444` | [PROVENANCE.json](../src/platform/windows/runtime/Uniwill/11.2.18.347/PROVENANCE.json) |
| `src/platform/windows/runtime/Uniwill/11.2.18.347/UWACPIDriver.sys` | пакет 11.2.18.347 | `e9840521975e9073881a488f9e978a18c93aed44c57fe2382b1d513fd27126e2` | [PROVENANCE.json](../src/platform/windows/runtime/Uniwill/11.2.18.347/PROVENANCE.json) |
| `src/platform/windows/runtime/Uniwill/UWACPIDriver.cat` | пакет 10.48.11.331 | `27a8c480c1d9cdaa1ce5c4c8c9963c9df519061ce1476e398e8f6cdab2bacf6b` | [PROVENANCE.json](../src/platform/windows/runtime/Uniwill/PROVENANCE.json) |
| `src/platform/windows/runtime/Uniwill/UWACPIDriver.sys` | пакет 10.48.11.331 | `6e360ee4a0c42b2a4eaf46208c1592c87f8cd71aca2a6c52831a2ad356e61235` | [PROVENANCE.json](../src/platform/windows/runtime/Uniwill/PROVENANCE.json) |

## Релизы

Версия и ОС указаны в имени локального архива `GitHub_Releases/MagicControl-<версия>-<ОС>-x64-portable`.
Контрольные суммы выпущенных архивов остаются в локальном `GitHub_Releases/SHA256SUMS.txt`.
Общий упаковщик использует локальные шаблоны `MagicControl-0.1.0-*` и локальные runtime.
Сборки и сами архивы в историю Git не добавляются.

## EXE, DLL и SYS в исследованиях и инструментах

Файлы остались по указанным локальным путям. Версия — из свойств файла;
если её нет, приведена версия пакета из пути или «не указана».

| Файл | Версия | SHA-256 |
|---|---|---|
| `research/devices/hlyl-wxx9/honor-20261018/keyboard-working/source/PawnIOLib.dll` | 2.2.0 | `d71f62627d66983bb9f5b1c269f27bcd8c1b8a46e794377a6330f84c198f4443` |
| `research/devices/hlyl-wxx9/honor-20261018/oem/WmiUtil.dll` | 11.0.0.0 | `ae4595e83180f4012bcd478730e58dd358aa5190462594532053af551ed8fc7b` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/BasicService/BasicService.exe` | 11.0.0.0 | `a93c207f30e401d90152605187dc7d0da7710760ba3ceff3c32548b9d1102298` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/BasicService/HardwareHal.dll` | 11.0.0.0 | `2110489c2643608ea986f3414e091450a5763d38b1fa0920ce768522223898d0` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/BasicService/HwDriverAdapter.dll` | 11.0.0.0 | `0b5bc5c1b7391ed5848077d99e67b5d537784c7c29dfc60c3c53492a95d49278` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/BasicService/InteractInfoWithMCU.dll` | 1.0.0.3 | `160e048c9115fb62f666da41904d47c3bee33cf277e470fd36e08153b9682fbe` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/BasicService/ProductAdaptMgr.dll` | 11.0.0.0 | `db1e148181350d8fc995f1c8cb900c6b41197720545f58f89cc7f625de2b758b` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/BasicService/WmiUtil.dll` | 11.0.0.0 | `53d7f726218104864f139f7b5415fc83bc22816c7eccd6a7b4704ca6295c6573` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/Huawei OSD/HardwareHal.dll` | 1.0.0.1 | `f275db607af5a0c8b6d9ae84368e94f1c03a689891fca5d35763bf0424639141` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/HwLcdEnhancement/HardwareHal.dll` | 11.0.0.0 | `b8398b5e6c2eb6823be323bc435b06f7d81e5e5cbbae915a975a9f912d483b2b` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/HwLcdEnhancement/ProductAdaptMgr.dll` | 11.0.0.0 | `d734b9b0a41df1b89bae1ca324451de35faa467e13384654074e62536a523876` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/HwLcdEnhancement/WmiUtil.dll` | 11.0.0.0 | `ae4595e83180f4012bcd478730e58dd358aa5190462594532053af551ed8fc7b` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/PCManager/HardwareHal.dll` | 11.0.0.0 | `b8398b5e6c2eb6823be323bc435b06f7d81e5e5cbbae915a975a9f912d483b2b` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/PCManager/HwDriverAdapter.dll` | 11.0.0.0 | `645ccabe290b2ffc38624a3c63fc6748bfc7f045306e94e4f1b80558afd819b6` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/PCManager/InteractInfoWithMCU.dll` | 1.0.0.3 | `dea8a067b1e4c298319357927ba2c49b45d354b179853c9ae56a7c16e64e71a8` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/PCManager/MateBookService.exe` | 11.0.0.0 | `ad0718816b481ce5731533a59a91a7dd122822a12167a3bce9d044df3d885a98` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/PCManager/PCManager.exe` | 11.0.0.0 | `6215c06e1d425627079a81380055796bbbae3a5868c143842f38191640417a73` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/PCManager/ProductAdaptMgr.dll` | 11.0.0.0 | `d734b9b0a41df1b89bae1ca324451de35faa467e13384654074e62536a523876` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/PCManager/WFPDriver.sys` | 1.0.0.1 | `ec3c28ebbfce6b02d035c503a4f16a5594745896e90615a3b2e38d5764302095` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/PCManager/WmiUtil.dll` | 11.0.0.0 | `ae4595e83180f4012bcd478730e58dd358aa5190462594532053af551ed8fc7b` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/PCManager/upgrade/dokan1.sys` | 1.3.1.1000 | `40a18e8aafbc682f912d9cda4630ec572a605373f59d69128853ce2448c99410` |
| `research/devices/hlyl-wxx9/honor-20261019/oem/PCManager/virtbus.sys` | 1.0.0.8 | `6ace5085abafa2f6092f31aad6feb8801fd6af60e04e77809fb88bf40efda19b` |
| `research/devices/hydroc-16-g2/oem-5.23.51.56/drivers/AirplaneDriver/vhidmini.sys` | 10.0.10011.16384 | `1372ad8322bf4e0e3d0668c8719b259691c55998fd1cb00340db8882e3e70038` |
| `research/devices/hydroc-16-g2/oem-5.23.51.56/drivers/UWACPIDriver/UWACPIDriver.sys` | пакет 5.23.51.56 | `e9840521975e9073881a488f9e978a18c93aed44c57fe2382b1d513fd27126e2` |
| `tests/util/runtime/PawnIO/PawnIOLib.dll` | 2.2.0 | `d71f62627d66983bb9f5b1c269f27bcd8c1b8a46e794377a6330f84c198f4443` |
| `tests/util/windows/EcBitTest.exe` | не указана | `a3901a8a5bb1a1505257f109bcf1bd07bb009b5cd1e8907d50ba010084a5c366` |
| `tests/util/windows/PawnIO/PawnIOLib.dll` | 2.2.0 | `d71f62627d66983bb9f5b1c269f27bcd8c1b8a46e794377a6330f84c198f4443` |
| `tests/util/windows/SensorProbe.exe` | не указана | `a2128e2249f6d07f5bd11cbc3ebb46263e8df856d73a0be5a24343997024068f` |
