# NOT run automatically. Restricted I2EC discovery on Wujie only.
# This uses writes to HOST I/O address selectors 0x681/0x682. It never writes
# the I2EC data port 0x683, EC RAM, SMBus registers, device 0x50 or firmware.
# Saved firmware/configuration evidence is not proof that live I/O decode is
# enabled; the caller must accept the uncertainty before starting this probe.
param([ValidateSet('first','retry')][string]$Attempt='first')
$ErrorActionPreference='Stop'
$output=Join-Path $PSScriptRoot $(if($Attempt -eq 'retry'){'i2ec-identity-probe-retry.json'}else{'i2ec-identity-probe.json'})
if(Test-Path -LiteralPath $output){throw 'Existing result must not be overwritten'}
$expectedHash='6E360EE4A0C42B2A4EAF46208C1592C87F8CD71ACA2A6C52831A2AD356E61235'
try {
    $admin=([Security.Principal.WindowsPrincipal][Security.Principal.WindowsIdentity]::GetCurrent()).IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
    if(!$admin){throw 'Administrator token required'}
    $board=(Get-CimInstance Win32_BaseBoard).Product
    if($board -ne 'WUJIE Series-X5SP4NAG'){throw 'SMBIOS board mismatch'}
    if((Get-FileHash -LiteralPath "$env:WINDIR\System32\drivers\UWACPIDriver.sys" -Algorithm SHA256).Hash -ne $expectedHash){throw 'Unreviewed UWACPIDriver hash'}
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using System.Collections.Generic;
public sealed class WujieI2ecIdentityProbe:IDisposable {
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateFileW(string n,uint a,uint s,IntPtr p,uint c,uint f,IntPtr t);
 [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)] static extern IntPtr CreateMutexExW(IntPtr p,string n,uint f,uint a);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool DeviceIoControl(IntPtr h,uint code,byte[] input,uint inputSize,byte[] output,uint outputSize,out uint returned,IntPtr p);
 [DllImport("kernel32.dll",SetLastError=true)] static extern uint WaitForSingleObject(IntPtr h,uint timeout);
 [DllImport("kernel32.dll",SetLastError=true)] static extern bool ReleaseMutex(IntPtr h);
 [DllImport("kernel32.dll")] static extern bool CloseHandle(IntPtr h);
 IntPtr device,gate; int latchWrites;
 public WujieI2ecIdentityProbe(){
  device=CreateFileW(@"\\.\ACPIDriver",0xc0000000,3,IntPtr.Zero,3,0,IntPtr.Zero);
  if(device==new IntPtr(-1))throw new Win32Exception(Marshal.GetLastWin32Error());
  gate=CreateMutexExW(IntPtr.Zero,@"Global\MagicControl.EcTransactions.v2",0,0x100001);
  if(gate==IntPtr.Zero){int e=Marshal.GetLastWin32Error();CloseHandle(device);device=IntPtr.Zero;throw new Win32Exception(e);}
 }
 byte Invoke(uint code,params uint[] args){
  byte[] input=new byte[args.Length*4],output=new byte[16];uint returned;
  for(int i=0;i<args.Length;i++)Array.Copy(BitConverter.GetBytes(args[i]),0,input,i*4,4);
  if(!DeviceIoControl(device,code,input,(uint)input.Length,output,(uint)output.Length,out returned,IntPtr.Zero))throw new Win32Exception(Marshal.GetLastWin32Error());
  if(returned==0||returned>16)throw new Exception("Invalid IOCTL reply length: "+returned);
  return output[0];
 }
 byte EcRead(ushort address){return Invoke(0x9c40a488,address,0);}
 byte I2ecRead(ushort address){
  // Fixed WIOP writes only the address-high/low selectors at 681/682.
  // Fixed RIOP repeats the low selector at 682 and reads the data port 683.
  Invoke(0x9c40a4cc,0x681,(uint)(address>>8),(uint)(address&255));latchWrites+=2;
  byte value=Invoke(0x9c40a4c8,0x682,(uint)(address&255));latchWrites++;
  return value;
 }
 public Dictionary<string,int> Run(bool firstOnly){
  uint w=WaitForSingleObject(gate,5000);if(w!=0&&w!=0x80)throw new Exception("EC transaction mutex unavailable");
  try{
   ushort[] ids={0x454,0x456,0x740};byte[] expected={82,72,26};
   for(int i=0;i<ids.Length;i++)if(EcRead(ids[i])!=expected[i])throw new Exception("Existing EC identity mismatch");
   var result=new Dictionary<string,int>();
   for(int i=0;i<ids.Length;i++){
    byte actual=I2ecRead(ids[i]);result.Add("ec_"+ids[i].ToString("X3"),actual);
    if(actual!=expected[i])throw new Exception("I2EC identity mismatch; probe stopped at "+ids[i].ToString("X3")+
      "; expected=0x"+expected[i].ToString("X2")+"; observed=0x"+actual.ToString("X2"));
    if(firstOnly){result.Add("host_address_latch_writes",latchWrites);return result;}
   }
   // Config bytes are read only after all three known EC identities match.
   foreach(ushort addr in new ushort[]{0x200d,0x2012,0x2014,0x2015})result.Add("ec_"+addr.ToString("X4"),I2ecRead(addr));
   result.Add("host_address_latch_writes",latchWrites);return result;
  }finally{ReleaseMutex(gate);}
 }
 public void Dispose(){if(device!=IntPtr.Zero&&device!=new IntPtr(-1))CloseHandle(device);if(gate!=IntPtr.Zero)CloseHandle(gate);device=gate=IntPtr.Zero;}
}
'@
    $probe=[WujieI2ecIdentityProbe]::new()
    try {$values=$probe.Run($Attempt -eq 'retry')} finally {$probe.Dispose()}
    @{success=$true;utc=[DateTime]::UtcNow.ToString('o');board=$board;driver_sha256=$expectedHash;
      host_port_selector_writes=$true;ec_data_port_writes=0;ec_memory_write_calls=0;smbus_transaction_calls=0;values=$values} |
      ConvertTo-Json -Depth 4 | Set-Content -LiteralPath $output -Encoding utf8
}catch{
    @{success=$false;utc=[DateTime]::UtcNow.ToString('o');host_port_selector_writes='possibly attempted';
      ec_data_port_writes=0;ec_memory_write_calls=0;smbus_transaction_calls=0;error=$_.ToString()} |
      ConvertTo-Json -Depth 3 | Set-Content -LiteralPath $output -Encoding utf8
}
