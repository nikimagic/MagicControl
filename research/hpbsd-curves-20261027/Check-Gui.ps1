param([string]$Version='0.1.232',[switch]$Exercise,[switch]$LeaveManual)
$ErrorActionPreference='Stop';$ProgressPreference='SilentlyContinue'
$root='C:\Users\Public\Downloads\MagicControl'
$output=Join-Path $root ('.magiccontrol-update\gui-curves-'+$Version)
if(Test-Path -LiteralPath $output){throw 'Existing GUI evidence retained; do not repeat a hardware test'}
New-Item -ItemType Directory -Path $output|Out-Null
Add-Type -AssemblyName System.Drawing
Add-Type -ReferencedAssemblies System.Drawing -TypeDefinition @'
using System;using System.Text;using System.Collections.Generic;using System.Diagnostics;using System.Drawing;using System.Drawing.Imaging;using System.Runtime.InteropServices;
public static class HpbsdGuiCheck {
 public delegate bool Visitor(IntPtr window,IntPtr data);
 [DllImport("user32.dll")]static extern bool EnumThreadWindows(uint thread,Visitor visit,IntPtr data);
 [DllImport("user32.dll")]static extern bool EnumChildWindows(IntPtr window,Visitor visit,IntPtr data);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetClassName(IntPtr window,StringBuilder text,int size);
 [DllImport("user32.dll",CharSet=CharSet.Unicode)]static extern int GetWindowText(IntPtr window,StringBuilder text,int size);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]static extern IntPtr ReadText(IntPtr window,uint message,IntPtr size,[Out]StringBuilder text);
 [DllImport("user32.dll",CharSet=CharSet.Unicode,EntryPoint="SendMessageW")]static extern IntPtr WriteText(IntPtr window,uint message,IntPtr unused,string text);
 [DllImport("user32.dll")]static extern IntPtr GetParent(IntPtr window);
 [DllImport("user32.dll")]static extern int GetDlgCtrlID(IntPtr window);
 [DllImport("user32.dll")]static extern bool IsWindowEnabled(IntPtr window);
 [DllImport("user32.dll")]static extern bool IsWindowVisible(IntPtr window);
 [DllImport("user32.dll")]public static extern bool ShowWindow(IntPtr window,int command);
 [DllImport("user32.dll")]static extern IntPtr SendMessage(IntPtr window,uint message,IntPtr w,IntPtr l);
 [DllImport("user32.dll")]static extern bool PrintWindow(IntPtr window,IntPtr dc,uint flags);
 [DllImport("user32.dll")]static extern bool GetWindowRect(IntPtr window,out Rect rect);
 [DllImport("user32.dll")]static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
 [StructLayout(LayoutKind.Sequential)]struct Rect {public int left,top,right,bottom;}
 public sealed class Row {public int id;public string text;public bool enabled,visible;public long hwnd;public int minimum,maximum;}
 public static IntPtr MainForProcess(int pid){IntPtr result=IntPtr.Zero;Visitor v=(w,p)=>{var c=new StringBuilder(128);GetClassName(w,c,128);if(c.ToString()=="MagicControlMainWindow"){result=w;return false;}return true;};using(Process process=Process.GetProcessById(pid)){foreach(ProcessThread t in process.Threads){EnumThreadWindows((uint)t.Id,v,IntPtr.Zero);if(result!=IntPtr.Zero)break;}}GC.KeepAlive(v);return result;}
 public static Row[] Read(IntPtr main){var rows=new List<Row>();Visitor v=(w,p)=>{int id=GetDlgCtrlID(w);if(!((id>=400&&id<=431)||(id>=320&&id<=327)||(id>=500&&id<=531)))return true;var c=new StringBuilder(64);GetClassName(w,c,64);bool spin=c.ToString()=="msctls_updown32";if(c.ToString()!="Edit"&&c.ToString()!="Button"&&!spin)return true;var s=new StringBuilder(128);if(c.ToString()=="Edit")ReadText(w,0xd,new IntPtr(128),s);else GetWindowText(w,s,128);long range=spin?SendMessage(w,0x466,IntPtr.Zero,IntPtr.Zero).ToInt64():0;rows.Add(new Row{id=id,text=s.ToString(),enabled=IsWindowEnabled(w),visible=IsWindowVisible(w),hwnd=w.ToInt64(),minimum=(short)(range>>16),maximum=(short)(range&65535)});return true;};EnumChildWindows(main,v,IntPtr.Zero);GC.KeepAlive(v);return rows.ToArray();}
 public static void Click(Row button){if(button.id<320||button.id>327||!button.enabled||!button.visible)throw new Exception("Expected visible enabled fan-mode button");SendMessage(new IntPtr(button.hwnd),0xf5,IntPtr.Zero,IntPtr.Zero);}
 public static void EditValue(Row field,int value){if((field.id!=400&&field.id!=417&&field.id!=408&&field.id!=414)||!field.enabled||!field.visible)throw new Exception("Expected editable ordinary Manual field");var w=new IntPtr(field.hwnd);WriteText(w,0xc,IntPtr.Zero,value.ToString());SendMessage(GetParent(w),0x111,new IntPtr(field.id|(0x200<<16)),w);}
 public static void Capture(IntPtr window,string path){IntPtr old=SetThreadDpiAwarenessContext(new IntPtr(-4));try{Rect r;if(!GetWindowRect(window,out r)||r.right-r.left<300||r.bottom-r.top<200)throw new Exception("Invalid GUI rectangle");using(var bitmap=new Bitmap(r.right-r.left,r.bottom-r.top))using(var g=Graphics.FromImage(bitmap)){IntPtr dc=g.GetHdc();bool ok;try{ok=PrintWindow(window,dc,2);}finally{g.ReleaseHdc(dc);}if(!ok)throw new Exception("PrintWindow failed");bitmap.Save(path,ImageFormat.Png);}}finally{if(old!=IntPtr.Zero)SetThreadDpiAwarenessContext(old);}}
}
'@
$p=@(Get-Process MagicControl|Where-Object {$_.Path -eq (Join-Path $root 'MagicControl.exe')})
if($p.Count -ne 1 -or (Get-Item $p[0].Path).VersionInfo.ProductVersion -ne $Version){throw 'Exact canonical GUI version required'}
$main=[HpbsdGuiCheck]::MainForProcess($p[0].Id);if($main -eq [IntPtr]::Zero){throw 'Main window not found in this user session'}
[void][HpbsdGuiCheck]::ShowWindow($main,5);Start-Sleep -Seconds 2
$phases=@();$manualRequested=$false;$failure=$null;$boundaryProbe=$null
try {
 $rows=@([HpbsdGuiCheck]::Read($main));$fields=@($rows|Where-Object {$_.id -ge 400 -and $_.id -le 431 -and $_.visible})
 foreach($spin in @($rows|Where-Object {$_.id -ge 500 -and $_.visible})){if(($spin.id-500)%2 -eq 0){$lo=20;$hi=100}else{$lo=5;$hi=100};if($spin.minimum -ne $lo -or $spin.maximum -ne $hi){throw ('Incorrect native numeric range: '+$spin.id)}}
 $phases+=[ordered]@{phase='Default';rows=$rows}
 if($fields.Count -ne 22 -or @($fields|Where-Object {$_.enabled}).Count -ne 0){throw 'Expected22visible read-only native curve fields in Default'}
 [HpbsdGuiCheck]::Capture($main,(Join-Path $output 'Default.png'))
 if($Exercise){
  if(($rows|Where-Object {$_.id -eq 408}).text -ne '0' -or ($rows|Where-Object {$_.id -eq 424}).text -ne '0'){throw 'Expected native factory cold rows before temporary Manual'}
  $manualRequested=$true;[HpbsdGuiCheck]::Click(($rows|Where-Object {$_.id -eq 321}));Start-Sleep -Seconds 3
  $rows=@([HpbsdGuiCheck]::Read($main));$phases+=[ordered]@{phase='Manual';rows=$rows}
  $mutable=@(400,401,402,403,404,405,406,408,409,410,411,412,413,414,416,417,424,425)
  foreach($id in $mutable){$field=@($rows|Where-Object {$_.id -eq $id});if($field.Count -ne 1 -or !$field[0].visible -or !$field[0].enabled){throw ('Native curve field not editable: '+$id)}}
  foreach($id in @(407,415,418,426)){if(($rows|Where-Object {$_.id -eq $id}).visible){throw ('Mandatory MAX row must be separate from editable inputs: '+$id)}}
  foreach($spin in @($rows|Where-Object {$_.id -ge 500 -and $_.visible})){if(($spin.id-500)%2 -eq 0){$lo=20;$hi=100}else{$lo=5;$hi=100};if($spin.minimum -ne $lo -or $spin.maximum -ne $hi){throw ('Manual reset the numeric range: '+$spin.id)}}
  $draftPath=Join-Path $root 'settings\hpbsd-fan-curves.txt';$draftHash=(Get-FileHash -LiteralPath $draftPath).Hash
  $original=@{};foreach($id in @(400,417,408,414)){$original[$id]=[int]($rows|Where-Object {$_.id -eq $id}).text}
  try{
   foreach($probe in @(@(400,20),@(417,100),@(408,5),@(414,100))){[HpbsdGuiCheck]::EditValue(($rows|Where-Object {$_.id -eq $probe[0]}),$probe[1])}
   Start-Sleep -Milliseconds 500;$probeRows=@([HpbsdGuiCheck]::Read($main));foreach($probe in @(@(400,20),@(417,100),@(408,5),@(414,100))){if([int]($probeRows|Where-Object {$_.id -eq $probe[0]}).text -ne $probe[1]){throw 'Inclusive input boundary was clamped'}}
   $saved=[IO.File]::ReadAllLines($draftPath);if($saved[1] -ne '20 5' -or ($saved[7].Split(' '))[-1] -ne '100' -or ($saved[10].Split(' '))[0] -ne '100'){throw 'Inclusive input boundaries were not saved'}
   $boundaryProbe=[ordered]@{temperature=@(20,100);percent=@(5,100);saved=$true;hardware_applied=$false}
   [HpbsdGuiCheck]::Capture($main,(Join-Path $output 'InclusiveBounds.png'))
  }finally{foreach($id in @(400,417,408,414)){[HpbsdGuiCheck]::EditValue(($rows|Where-Object {$_.id -eq $id}),$original[$id])};Start-Sleep -Milliseconds 500;if((Get-FileHash -LiteralPath $draftPath).Hash -ne $draftHash){throw 'Original curve draft not restored after inclusive boundary probe'}}
  [HpbsdGuiCheck]::Capture($main,(Join-Path $output 'Manual.png'))
 }
}catch{$failure=$_.Exception.Message}finally{
 if($manualRequested){try{$rows=@([HpbsdGuiCheck]::Read($main));[HpbsdGuiCheck]::Click(($rows|Where-Object {$_.id -eq 320}));Start-Sleep -Seconds 3;$rows=@([HpbsdGuiCheck]::Read($main));$phases+=[ordered]@{phase='Restored';rows=$rows};if(($rows|Where-Object {$_.id -eq 408}).text -ne '0' -or ($rows|Where-Object {$_.id -eq 424}).text -ne '0'){throw 'Factory rows not restored'};[HpbsdGuiCheck]::Capture($main,(Join-Path $output 'Restored.png'))}catch{$failure=([string]$failure)+'; RESTORE_UNCONFIRMED '+$_.Exception.Message}}
}
$leftManual=$false
if($LeaveManual -and $Exercise -and !$failure){
 try{$rows=@([HpbsdGuiCheck]::Read($main));[HpbsdGuiCheck]::Click(($rows|Where-Object {$_.id -eq 321}));Start-Sleep -Seconds 3;$rows=@([HpbsdGuiCheck]::Read($main));$phases+=[ordered]@{phase='ManualFinal';rows=$rows};$fields=@($rows|Where-Object {$_.id -ge 400 -and $_.id -le 431 -and $_.visible});if($fields.Count -ne 18 -or @($fields|Where-Object {!$_.enabled}).Count -ne 0){throw 'Final Manual fields not editable'};[HpbsdGuiCheck]::Capture($main,(Join-Path $output 'ManualFinal.png'));$leftManual=$true}catch{$failure=$_.Exception.Message;try{$rows=@([HpbsdGuiCheck]::Read($main));[HpbsdGuiCheck]::Click(($rows|Where-Object {$_.id -eq 320}))}catch{$failure+='; Default recovery not requested'}}
}
$result=[ordered]@{version=$Version;pid=$p[0].Id;session=$p[0].SessionId;exercise=[bool]$Exercise;leftManual=$leftManual;boundaryProbe=$boundaryProbe;success=($null -eq $failure);error=$failure;phases=$phases;method='Own-app typed Win32 fields/buttons and PrintWindow only'}
$result|ConvertTo-Json -Depth 7|Set-Content -LiteralPath (Join-Path $output 'report.json') -Encoding utf8
[ordered]@{output=$output;success=$result.success;error=$failure;phases=@($phases|ForEach-Object {$_.phase})}|ConvertTo-Json -Compress
if($failure){throw $failure}
