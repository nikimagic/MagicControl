$AnalysisOutput = Join-Path ([IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../..'))) ('out/test_util/generated/' + (Split-Path -Leaf $PSScriptRoot) + '/' + [DateTime]::UtcNow.ToString('yyyyMMdd-HHmmss-fff') + '-' + [Guid]::NewGuid().ToString('N').Substring(0,8))
[IO.Directory]::CreateDirectory($AnalysisOutput) | Out-Null
& 'C:\Users\admin\.cache\codex-runtimes\codex-primary-runtime\dependencies\python\python.exe' (Join-Path $PSScriptRoot 'probe_ac100_all.py') --output (Join-Path $AnalysisOutput 'ac100-all-run1') *> (Join-Path $AnalysisOutput 'ac100-all-launch.txt')
