$ErrorActionPreference = 'Stop'
& 'C:\Users\admin\Documents\mechrevo_dsh\MagicControl\tests\test_util\windows\SensorProbe.exe' --count 2 --interval 3 --ec-only --output 'C:\Users\admin\Documents\mechrevo_dsh\MagicControl\tests\test_util\analysis\apu-ceiling-20261017\current' *> 'C:\Users\admin\Documents\mechrevo_dsh\MagicControl\tests\test_util\analysis\apu-ceiling-20261017\capture.txt'
$LASTEXITCODE | Set-Content -LiteralPath 'C:\Users\admin\Documents\mechrevo_dsh\MagicControl\tests\test_util\analysis\apu-ceiling-20261017\exit-code.txt'
