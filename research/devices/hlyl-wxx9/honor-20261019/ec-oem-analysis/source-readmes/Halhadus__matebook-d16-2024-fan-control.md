# Fan Control Bash Script for Huawei Matebook D16 2024

This script allows you to control the fan using the acpi_call module for D16 2024.

Note: Using this script carries risks that could result in significant damage to your computer. I do not recommend using it; if you do, you do so at your own risk.

The script scans all temperature sensors every 2 seconds and acts on the one with the highest temperature. The fan turns on when the highest temperature sensor reaches 42 degrees, and the RPM increases or decreases by approximately 100-150 RPM (one point) for every 2 degrees Celsius increase. The fan turns off when the temperature drops below 33 degrees Celsius.

Check https://github.com/nbfc-linux/nbfc-linux/issues/256
