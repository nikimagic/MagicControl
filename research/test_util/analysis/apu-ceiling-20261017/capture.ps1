$AnalysisOutput = Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))) ('out/test_util/generated/' + (Split-Path -Leaf $PSScriptRoot) + '/' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
[IO.Directory]::CreateDirectory($AnalysisOutput) | Out-Null
$ErrorActionPreference = 'Stop'
& 'C:\Users\admin\Documents\mechrevo_dsh\MagicControl\tests\util\windows\SensorProbe.exe' --count 2 --interval 3 --ec-only --output (Join-Path $AnalysisOutput 'current') *> (Join-Path $AnalysisOutput 'capture.txt')
$LASTEXITCODE | Set-Content -LiteralPath (Join-Path $AnalysisOutput 'exit-code.txt')
