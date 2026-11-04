# Exact-camera read-only KS query. No pins, graph, stream, SET or reset.
$ErrorActionPreference='Stop'
$sampleName='camera-ks-controls'
if ($args.Count -gt 0) {
    if ($args[0] -notmatch '^[a-zA-Z0-9-]+$') { throw 'Invalid sample label' }
    $sampleName=[string]$args[0]
}
Add-Type -TypeDefinition @'
using System;
using System.Collections.Generic;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public sealed class CameraKsReadResult {
    public string Camera, Set, Kind, Hex;
    public uint Node, Id, Returned;
    public int Error;
}
public static class WujieCameraKsRead {
    [DllImport("kernel32.dll",CharSet=CharSet.Unicode,SetLastError=true)]
    private static extern SafeFileHandle CreateFile(string p,uint a,uint s,IntPtr z,uint c,uint f,IntPtr t);
    [DllImport("kernel32.dll",SetLastError=true)]
    private static extern bool DeviceIoControl(SafeFileHandle h,uint code,byte[] input,uint n,
        byte[] output,uint cap,out uint returned,IntPtr overlapped);
    private static CameraKsReadResult Read(SafeFileHandle h,string camera,Guid set,uint id,
        uint flags,uint node,string kind) {
        if(flags!=1 && flags!=0x10000001 && flags!=0x10000200)
            throw new InvalidOperationException("Read-only flags required");
        byte[] request=new byte[flags==1 && kind!="global-pin-instances"?24:32];
        Array.Copy(set.ToByteArray(),request,16);
        Array.Copy(BitConverter.GetBytes(id),0,request,16,4);
        Array.Copy(BitConverter.GetBytes(flags),0,request,20,4);
        if(request.Length==32) Array.Copy(BitConverter.GetBytes(node),0,request,24,4);
        byte[] data=new byte[4096]; uint returned;
        bool ok=DeviceIoControl(h,0x2f0003,request,(uint)request.Length,data,(uint)data.Length,out returned,IntPtr.Zero);
        int error=ok?0:Marshal.GetLastWin32Error();
        return new CameraKsReadResult {Camera=camera,Set=set.ToString(),Id=id,Node=node,Kind=kind,
            Returned=returned,Error=error,Hex=ok?BitConverter.ToString(data,0,(int)Math.Min(returned,(uint)data.Length)):""};
    }
    public static List<CameraKsReadResult> Run() {
        var rows=new List<CameraKsReadResult>();
        Guid topology=new Guid("720d4ac0-7533-11d0-a5d6-28db04c10000");
        Guid pins=new Guid("8c134960-51ad-11cf-878a-94f801c10000");
        Guid deviceSpecific=new Guid("941c7ac0-c559-11d0-8a2b-00a0c9255ac1");
        Guid[] extensions={new Guid("0fb885c3-68c2-4547-90f7-8f47579d95fc"),
            new Guid("63610682-5070-49ab-b8cc-b3855e8d221d"),
            new Guid("0f3f95dc-2632-4c4e-92c9-a04782f43bc8")};
        foreach(string part in new string[]{"00#7&2789a4b1&0&0000","02#7&2789a4b1&0&0002"}) {
            string category=part.StartsWith("02")?"24e552d7-6523-47f7-a647-d3465bf1f5ca":"65e8773d-8f56-11d0-a3b9-00a0c9223196";
            string path=@"\\?\USB#VID_2B7E&PID_B663&MI_"+part+"#{"+category+@"}\GLOBAL";
            using(var h=CreateFile(path,0xc0000000,3,IntPtr.Zero,3,0,IntPtr.Zero)) {
                if(h.IsInvalid) { rows.Add(new CameraKsReadResult{Camera=part,Kind="open",Error=Marshal.GetLastWin32Error()});continue; }
                var pinTypes=Read(h,part,pins,1,1,0,"pin-types");rows.Add(pinTypes);
                if(pinTypes.Error==0 && pinTypes.Returned==4) {
                    byte[] raw=Array.ConvertAll(pinTypes.Hex.Split('-'),x=>Convert.ToByte(x,16));
                    uint pinCount=BitConverter.ToUInt32(raw,0);
                    if(pinCount>64) throw new InvalidOperationException("Invalid pin count");
                    for(uint pin=0;pin<pinCount;pin++)
                        rows.Add(Read(h,part,pins,8,1,pin,"global-pin-instances"));
                }
                var nodes=Read(h,part,topology,1,1,0,"topology-nodes");rows.Add(nodes);
                if(nodes.Error!=0 || nodes.Returned<8) continue;
                string[] hex=nodes.Hex.Split('-');byte[] data=Array.ConvertAll(hex,x=>Convert.ToByte(x,16));
                uint count=BitConverter.ToUInt32(data,4);
                if(count>64 || 8+16*count>data.Length) throw new InvalidOperationException("Invalid node list");
                for(uint node=0;node<count;node++) {
                    byte[] bytes=new byte[16]; Array.Copy(data,8+16*node,bytes,0,16);
                    if(new Guid(bytes)!=deviceSpecific) continue;
                    foreach(Guid ext in extensions) {
                        var info=Read(h,part,ext,0,0x10000001,node,"extension-info"); rows.Add(info);
                        if(info.Error!=0) continue;
                        // Metadata-only queries to selectors suggested by external sources.
                        // A matching GUID does not establish their meaning on this module.
                        uint[] selectors=ext==extensions[0]?new uint[]{1,2,3,4,5}:new uint[]{1,2,13};
                        foreach(uint selector in selectors)
                            rows.Add(Read(h,part,ext,selector,0x10000200,node,"basic-support"));
                        foreach(uint selector in selectors)
                            rows.Add(Read(h,part,ext,selector,0x10000001,node,"get-current"));
                    }
                }
            }
        }
        return rows;
    }
}
'@
$rows=[WujieCameraKsRead]::Run()
[ordered]@{sampledAt=[DateTimeOffset]::Now.ToString('o');streamStarted=$false;controlSetSent=$false;results=@($rows)} |
    ConvertTo-Json -Depth 6 | Set-Content (Join-Path $PSScriptRoot ($sampleName+'.json')) -Encoding utf8
$rows | ForEach-Object { '{0} node={1} {2} id={3} err={4} n={5} {6}' -f $_.Camera,$_.Node,$_.Kind,$_.Id,$_.Error,$_.Returned,$_.Hex }
