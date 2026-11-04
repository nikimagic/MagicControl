# Статус ACPI и команды управления зарядкой

Проверено21.10.2026 по первичной документации. Ниже интерпретация протокола,
не инструкция выполнять команды и не доказательство возможностей GM6.

- ACPI6.6 §3.9.6 и §10.2.2.11: BatteryState.bit3 сообщает ChargeLimiting;
  отдельные bits0/1 отражают фактическое направление. Статус сам не включает
  аппаратный зарядник. В exact GM6 AML бит3 добавляется по CGLM1..99.
  https://uefi.org/specs/ACPI/6.6/03_ACPI_Concepts.html#battery-charge-limiting
  https://uefi.org/specs/ACPI/6.6/10_Power_Source_and_Power_Meter_Devices.html#bst-battery-status
  https://uefi.org/sites/default/files/resources/ACPI_Spec_6.6.pdf
- ACPI _BMC.bit1 может отключать зарядку, если соответствующий_BMD capability
  существует. В захваченных15 AML GM6 этих методов нет. Наличие стандарта не
  создаёт функцию на плате.
  https://uefi.org/specs/ACPI/6.5/10_Power_Source_and_Power_Meter_Devices.html#bmc-battery-maintenance-control
- Windows battery_DSM function1 задаёт процент МАКСИМАЛЬНОГО ТОКА зарядки:
  40 означает40% скорости, не stop на40%SOC; 0 запрашивает остановку.
  Нужен реализованный UUID4c2067e3-887d-475c-9720-4af1d3ed602e. Его нет
  в изученных GM6 AML. Эту ветку нельзя выдавать за готовый GM6 % API.
  https://learn.microsoft.com/en-us/windows-hardware/design/component-guidelines/acpi-battery-and-power-subsystem-firmware-implementation
- _BTP задаёт порог оставшейся ёмкости для уведомления, а не запрет зарядки.
  Та же Microsoft-страница описывает его как механизм Notify0x80.
- BATTERY_INFORMATION.Capabilities.bits0/1 обозначают поддержку запросов
  BatteryCharge/BatteryDischarge. На присланном скриншоте GM6 значение80000000:
  эти две возможности не объявлены. Такой IOCTL не является произвольным%
  setter; документация BATTERY_SET_INFORMATION не содержит поля порога SOC.
  https://learn.microsoft.com/en-us/windows/win32/power/battery-information-str
  https://learn.microsoft.com/en-us/windows/win32/power/battery-set-information-str

UEFI HTML был доступен через поисковый индекс, прямые open вернули403;
доступ не обходился. Microsoft страницы прочитаны напрямую. Выводы о GM6
основаны на сохранённых exact AML, не на общих требованиях стандарта.
