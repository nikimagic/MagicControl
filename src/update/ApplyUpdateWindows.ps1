# One transient Windows file adapter. The C++ core owns download, admission,
# preserved-file policy and the prepared plan. No GUI/EC/driver initialization.
param([string]$Root,[string]$Stage)
$ErrorActionPreference='Stop'
$ProgressPreference='SilentlyContinue'
$utf8=New-Object Text.UTF8Encoding($false)
$Root=[IO.Path]::GetFullPath($Root).TrimEnd('\')
$Stage=[IO.Path]::GetFullPath($Stage).TrimEnd('\')
$cache=[IO.Path]::GetDirectoryName($Stage)
$newCache=Join-Path $Root 'magiccontrol-update'
$oldCache=Join-Path $Root '.magiccontrol-update'
$changed=New-Object 'System.Collections.Generic.List[object]'
$lease=$null;$legacyLease=$null;$leased=$false;$parentExited=$false;$launched=$false
function In-Folder([string]$Path,[string]$Base){
 $p=[IO.Path]::GetFullPath($Path);$b=[IO.Path]::GetFullPath($Base).TrimEnd('\')+'\'
 return $p.StartsWith($b,[StringComparison]::OrdinalIgnoreCase)
}
function No-Links([string]$Base,[string]$Relative){
 if(!$Relative -or [IO.Path]::IsPathRooted($Relative) -or $Relative.Contains(':') -or $Relative.Contains('\') -or $Relative -match '[\x00-\x1f]'){throw 'Unsafe package path'}
 $p=$Base
 foreach($part in $Relative.Split('/')){
  if(!$part -or $part -eq '.' -or $part -eq '..'){throw 'Unsafe package component'}
  $p=Join-Path $p $part
  if((Test-Path -LiteralPath $p) -and ((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Package path is a link'}
 }
 if(!(In-Folder $p $Base)){throw 'Package path escaped its root'}
 return $p
}
function Replace-File([string]$From,[string]$To){
 $until=[datetime]::UtcNow.AddSeconds(3)
 while($true){try{
  # Windows PowerShell 5.1 binds $null to an empty string for this overload.
  # File.Replace then treats it as an invalid backup PATH. Pass a true null
  # string for both installation and rollback; keep the checked atomic move.
  if([IO.File]::Exists($To)){[IO.File]::Replace($From,$To,[NullString]::Value)}else{[IO.File]::Move($From,$To)}
  return
 }catch{
  $code=$_.Exception.GetBaseException().HResult -band 65535
  if($code -notin @(5,32,33) -or [datetime]::UtcNow -ge $until){throw}
  Start-Sleep -Milliseconds 100
 }}
}
function Start-Application {
 # The adapter inherits the elevated app token. Start the exact verified EXE
 # directly, as PlatformWindows::launch does; Start-Process otherwise enters
 # ShellExecute and can wait indefinitely before returning a process handle.
 # Keep Windows protection settings and the file's origin metadata unchanged.
 $start=New-Object Diagnostics.ProcessStartInfo
 $start.FileName=Join-Path $Root 'MagicControl.exe'
 $start.WorkingDirectory=$Root
 $start.UseShellExecute=$false
 $start.WindowStyle=[Diagnostics.ProcessWindowStyle]::Normal
 $process=[Diagnostics.Process]::Start($start)
 if(!$process){throw 'Application process was not created'}
 $process.Dispose()
}
function Remove-CacheEntry([string]$Path){
 if(!(In-Folder $Path $cache)){throw 'Cleanup escaped update cache'}
 $item=Get-Item -LiteralPath $Path -Force
 if($item.Attributes -band [IO.FileAttributes]::ReparsePoint){
  if($item.PSIsContainer){[IO.Directory]::Delete($item.FullName,$false)}else{[IO.File]::Delete($item.FullName)}
  return
 }
 if($item.PSIsContainer){
  foreach($child in @(Get-ChildItem -LiteralPath $item.FullName -Force)){Remove-CacheEntry $child.FullName}
  [IO.Directory]::Delete($item.FullName,$false)
 }else{Remove-Item -LiteralPath $item.FullName -Force}
}
try{
 if($cache -ne $newCache -and $cache -ne $oldCache){throw 'Invalid staging root'}
 foreach($p in @($Root,$cache,$Stage)){
  if((Get-Item -LiteralPath $p -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Update root is a link'}
 }
 $planPath=No-Links $Stage 'install-plan.json'
 $plan=[IO.File]::ReadAllText($planPath)|ConvertFrom-Json
 if($plan.product -ne 'MagicControl' -or $plan.platform -ne 'windows' -or $plan.version -notmatch '^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z.-]+)?(?:\+[0-9A-Za-z.-]+)?$'){throw 'Invalid prepared plan'}
 $package=No-Links $Stage ('unpacked/'+$plan.package)
 $manifest=No-Links $package 'package-manifest.json'
 if((Get-FileHash -LiteralPath $manifest).Hash.ToLowerInvariant() -ne $plan.manifest_sha256){throw 'Prepared manifest changed'}
 $parentText=[IO.File]::ReadAllText((Join-Path $Stage 'parent-pid.txt'))
 if($parentText -notmatch '^\d+$' -or [uint32]$parentText -eq 0 -or [uint32]$parentText -eq $PID){throw 'Invalid parent process'}
 $parent=Get-Process -Id ([uint32]$parentText) -ErrorAction SilentlyContinue
 if($parent){try{if(!$parent.WaitForExit(120000)){throw 'Application did not exit normally'}}finally{$parent.Dispose()}}
 $parentExited=$true
 # Older/manual adapters may have launched this script from the former EXE
 # helper. Wait for it; never kill it or remove its rollback while active.
 foreach($helper in [Diagnostics.Process]::GetProcessesByName('MagicControl.Update')){
  try{if((In-Folder $helper.MainModule.FileName $cache) -and !$helper.WaitForExit(15000)){throw 'Previous update helper still active'}}finally{$helper.Dispose()}
 }
 $oldLocks=Join-Path $Root '.locks'
 if(Test-Path -LiteralPath $oldLocks){
  if((Get-Item -LiteralPath $oldLocks -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Legacy lock root is a link'}
  $oldLockPath=Join-Path $oldLocks 'update-transaction.lock'
  if((Test-Path -LiteralPath $oldLockPath) -and ((Get-Item -LiteralPath $oldLockPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Legacy update lock is a link'}
  $legacyLease=[IO.File]::Open($oldLockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite)
  $legacyLease.Lock(0,1)
 }
 $lockDir=Join-Path $Root 'locks';[IO.Directory]::CreateDirectory($lockDir)|Out-Null
 if((Get-Item -LiteralPath $lockDir -Force).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Update lock root is a link'}
 $lockPath=Join-Path $lockDir 'update-transaction.lock'
 if((Test-Path -LiteralPath $lockPath) -and ((Get-Item -LiteralPath $lockPath -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Update lock is a link'}
 $lease=[IO.File]::Open($lockPath,[IO.FileMode]::OpenOrCreate,[IO.FileAccess]::ReadWrite,[IO.FileShare]::ReadWrite)
 $lease.Lock(0,1)
 $leased=$true
 $exe=Join-Path $Root 'MagicControl.exe'
 if((Get-Item -LiteralPath $exe).VersionInfo.ProductVersion -ne $plan.previous_version){throw 'Installed version changed'}
 foreach($f in $plan.files){
  $source=No-Links $package $f.path
  [void](No-Links $Root $f.path)
  if((Get-Item -LiteralPath $source).Length -ne $f.bytes -or (Get-FileHash -LiteralPath $source).Hash.ToLowerInvariant() -ne $f.sha256){throw ('Payload changed: '+$f.path)}
 }
 # New-app startup must not treat an in-progress script install as a completed
 # legacy EXE install. Final success is written only after launch and cleanup.
 $resultPath=No-Links $Root 'update-result.txt'
 [IO.File]::WriteAllText($resultPath,'Подготовка файлов обновления.',$utf8)
 foreach($f in $plan.files){
  $source=No-Links $package $f.path;$target=No-Links $Root $f.path
  $backup=No-Links $Stage ('backup/'+$f.path);$next=No-Links $Stage ('next/'+$f.path)
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($target))|Out-Null
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($backup))|Out-Null
  [IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($next))|Out-Null
  $existed=[IO.File]::Exists($target)
  if($existed){[IO.File]::Copy($target,$backup,$false)}
  [IO.File]::Copy($source,$next,$false)
  $changed.Add([pscustomobject]@{Path=$f.path;Existed=$existed})
  Replace-File $next $target
 }
 foreach($f in $plan.files){
  $target=No-Links $Root $f.path
  if((Get-FileHash -LiteralPath $target).Hash.ToLowerInvariant() -ne $f.sha256){throw 'Installed file verification failed'}
 }
 [IO.File]::WriteAllText($resultPath,'Файлы обновлены и проверены. Запуск MagicControl.',$utf8)
 Start-Application
 $launched=$true
 [IO.File]::WriteAllText($resultPath,'MagicControl запущен. Очистка папки обновлений.',$utf8)
 $receipt=Join-Path $lockDir $(if($cache -eq $oldCache){'update-cleanup-legacy.json'}else{'update-cleanup.json'})
 if((Test-Path -LiteralPath $receipt) -and ((Get-Item -LiteralPath $receipt -Force).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Cleanup receipt is a link'}
 $temporary=$receipt+'.'+[guid]::NewGuid().ToString('N')
 try{
  [IO.File]::WriteAllText($temporary,([pscustomobject]@{version=$plan.version;manifest_sha256=$plan.manifest_sha256}|ConvertTo-Json -Compress),$utf8)
  Replace-File $temporary $receipt
 }finally{if([IO.File]::Exists($temporary)){[IO.File]::Delete($temporary)}}
 # This script was read into memory before invocation; its source file and
 # all other update contents can be removed while PowerShell finishes.
 foreach($child in @(Get-ChildItem -LiteralPath $cache -Force)){Remove-CacheEntry $child.FullName}
 if(@(Get-ChildItem -LiteralPath $cache -Force).Count){throw 'Update cache not empty'}
 [IO.File]::Delete($receipt)
 [IO.File]::WriteAllText((Join-Path $Root 'update-result.txt'),'Обновление установлено. Настройки и кривые сохранены. Папка обновлений очищена.',$utf8)
}catch{
 $failure=$_.Exception.Message
 if($launched){
  # Installed and running: cleanup failure is not an installation rollback.
  [IO.File]::WriteAllText((Join-Path $Root 'update-result.txt'),('Обновление установлено. Очистка отложена: '+$failure),$utf8)
 }else{
  $restored=$true
  for($i=$changed.Count-1;$i -ge 0;$i--){try{
   $f=$changed[$i];$target=No-Links $Root $f.Path
   if($f.Existed){$backup=No-Links $Stage ('backup/'+$f.Path);$next=No-Links $Stage ('restore/'+$f.Path);[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($next))|Out-Null;[IO.File]::Copy($backup,$next,$false);Replace-File $next $target}
   elseif([IO.File]::Exists($target)){[IO.File]::Delete($target)}
  }catch{$restored=$false}}
  if($parentExited -and $leased){
   $detail=if($restored){' Предыдущая версия восстановлена.'}else{' Восстановление неполное; резервная копия сохранена.'}
   [IO.File]::WriteAllText((Join-Path $Root 'update-result.txt'),('Обновление не установлено: '+$failure+$detail),$utf8)
   Start-Application
  }
 }
 exit 1
}finally{if($lease){$lease.Dispose()};if($legacyLease){$legacyLease.Dispose()}}
