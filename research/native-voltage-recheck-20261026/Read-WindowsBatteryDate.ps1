# Read-only Windows Battery Class queries. No EC, SMBus or vendor interface.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class WindowsBatteryDateReader {
    [StructLayout(LayoutKind.Sequential)]
    struct InterfaceData { public uint cbSize; public Guid guid; public uint flags; public IntPtr reserved; }
    [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern IntPtr SetupDiGetClassDevsW(ref Guid guid,string enumerator,IntPtr parent,uint flags);
    [DllImport("setupapi.dll",SetLastError=true)]
    static extern bool SetupDiEnumDeviceInterfaces(IntPtr list,IntPtr device,ref Guid guid,uint index,ref InterfaceData data);
    [DllImport("setupapi.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern bool SetupDiGetDeviceInterfaceDetailW(IntPtr list,ref InterfaceData data,IntPtr detail,uint size,out uint required,IntPtr device);
    [DllImport("setupapi.dll",SetLastError=true)]
    static extern bool SetupDiDestroyDeviceInfoList(IntPtr list);
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    static extern SafeFileHandle CreateFileW(string path,uint access,uint share,IntPtr security,uint disposition,uint flags,IntPtr template);
    [DllImport("kernel32.dll",SetLastError=true)]
    static extern bool DeviceIoControl(SafeFileHandle h,uint code,byte[] input,uint inputSize,byte[] output,uint outputSize,out uint returned,IntPtr overlapped);
    public sealed class Result {
        public uint InterfaceIndex;
        public bool Opened,TagQuerySucceeded,DateQuerySucceeded,ValidDate;
        public uint BatteryTag,ReturnedBytes;
        public string ManufactureDate,RawDateHex,Error;
        public int Win32Error;
    }
    static void Fail(Result r,string phase,int code) {
        r.Win32Error=code;r.Error=phase+": "+new Win32Exception(code).Message;
    }
    public static Result[] Read() {
        Guid guid=new Guid("72631e54-78a4-11d0-bcf7-00aa00b7b32a");
        IntPtr list=SetupDiGetClassDevsW(ref guid,null,IntPtr.Zero,0x12);
        if(list==new IntPtr(-1))throw new Win32Exception(Marshal.GetLastWin32Error());
        var results=new List<Result>();
        try {
            for(uint index=0;index<64;index++) {
                var data=new InterfaceData { cbSize=(uint)Marshal.SizeOf(typeof(InterfaceData)) };
                if(!SetupDiEnumDeviceInterfaces(list,IntPtr.Zero,ref guid,index,ref data)) {
                    int e=Marshal.GetLastWin32Error();if(e==259)break;throw new Win32Exception(e);
                }
                var r=new Result {InterfaceIndex=index};results.Add(r);
                uint bytes;
                SetupDiGetDeviceInterfaceDetailW(list,ref data,IntPtr.Zero,0,out bytes,IntPtr.Zero);
                if(bytes<8||bytes>1048576) {r.Error="Invalid interface detail size";continue;}
                IntPtr detail=Marshal.AllocHGlobal((int)bytes);
                try {
                    Marshal.WriteInt32(detail,IntPtr.Size==8?8:6);
                    if(!SetupDiGetDeviceInterfaceDetailW(list,ref data,detail,bytes,out bytes,IntPtr.Zero)) {
                        Fail(r,"Interface detail",Marshal.GetLastWin32Error());continue;
                    }
                    string path=Marshal.PtrToStringUni(IntPtr.Add(detail,4));
                    // GENERIC_READ only. No write access or set-information IOCTL.
                    using(var h=CreateFileW(path,0x80000000,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
                        if(h.IsInvalid) {Fail(r,"Open battery",Marshal.GetLastWin32Error());continue;}
                        r.Opened=true;
                        var wait=new byte[4];var tag=new byte[4];uint returned;
                        r.TagQuerySucceeded=DeviceIoControl(h,0x294040,wait,4,tag,4,out returned,IntPtr.Zero);
                        if(!r.TagQuerySucceeded) {Fail(r,"Query tag",Marshal.GetLastWin32Error());continue;}
                        if(returned!=4||(r.BatteryTag=BitConverter.ToUInt32(tag,0))==0) {r.Error="No current battery tag";continue;}
                        var request=new byte[12];
                        Array.Copy(tag,0,request,0,4);
                        Array.Copy(BitConverter.GetBytes((uint)5),0,request,4,4); // BatteryManufactureDate, SDK poclass.h
                        var date=new byte[4];
                        r.DateQuerySucceeded=DeviceIoControl(h,0x294044,request,12,date,4,out returned,IntPtr.Zero);
                        int error=r.DateQuerySucceeded?0:Marshal.GetLastWin32Error();
                        r.ReturnedBytes=returned;r.RawDateHex=BitConverter.ToString(date);
                        if(!r.DateQuerySucceeded) {Fail(r,"BatteryManufactureDate",error);continue;}
                        if(returned!=4) {r.Error="Date response length is not4";continue;}
                        int day=date[0],month=date[1],year=BitConverter.ToUInt16(date,2);
                        try { if(year<1980)throw new ArgumentOutOfRangeException();
                            r.ManufactureDate=new DateTime(year,month,day).ToString("yyyy-MM-dd");r.ValidDate=true;
                        } catch(ArgumentOutOfRangeException) {r.Error="Driver returned an invalid calendar date";}
                    }
                } finally {Marshal.FreeHGlobal(detail);}
            }
        } finally {SetupDiDestroyDeviceInfoList(list);}
        return results.ToArray();
    }
}
'@
$result=[pscustomobject]@{
    Timestamp=(Get-Date -Format o)
    Source='Windows Battery Class IOCTL_BATTERY_QUERY_INFORMATION / BatteryManufactureDate (level5)'
    Access='GENERIC_READ; QUERY_TAG and QUERY_INFORMATION only; no EC access'
    Batteries=@([WindowsBatteryDateReader]::Read())
}
$result | ConvertTo-Json -Depth 5
