# Read-only exact-device diagnostic. Only USB connection info + GET_DESCRIPTOR.
# No SET, stream, pin activation, EC access, driver install, or arbitrary IOCTL.
$ErrorActionPreference = 'Stop'
Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class WujieCameraDescriptors {
    [DllImport("kernel32.dll", CharSet=CharSet.Unicode, SetLastError=true)]
    private static extern SafeFileHandle CreateFile(string path, uint access, uint share,
        IntPtr security, uint creation, uint flags, IntPtr template);
    [DllImport("kernel32.dll", SetLastError=true)]
    private static extern bool DeviceIoControl(SafeFileHandle h, uint code,
        byte[] input, uint inputLength, byte[] output, uint outputLength,
        out uint returned, IntPtr overlapped);
    private static byte[] Query(SafeFileHandle h, uint code, byte[] request) {
        byte[] output = new byte[request.Length]; uint returned;
        if (!DeviceIoControl(h, code, request, (uint)request.Length, output,
            (uint)output.Length, out returned, IntPtr.Zero))
            throw new Win32Exception(Marshal.GetLastWin32Error());
        if (returned > output.Length) throw new InvalidOperationException("Bad returned length");
        Array.Resize(ref output, (int)returned); return output;
    }
    private static byte[] GetDescriptor(SafeFileHandle h, byte type, byte index, ushort size) {
        byte[] request = new byte[12 + size];
        request[0]=1; // port 1, verified PnP parent + location
        request[4]=0x80; request[5]=6; // USB IN GET_DESCRIPTOR
        request[6]=index; request[7]=type;
        request[10]=(byte)size; request[11]=(byte)(size >> 8);
        var response=Query(h, 0x220410, request);
        if(response.Length < 12) throw new InvalidOperationException("Short descriptor request");
        byte[] descriptor=new byte[response.Length-12];
        Array.Copy(response,12,descriptor,0,descriptor.Length); return descriptor;
    }
    public static byte[][] Read() {
        const string path=@"\\?\USB#ROOT_HUB30#5&27346db4&0&0#{f18a0e88-c30c-11d0-8815-00a0c906bed8}";
        // USBView's hub-open flags. This is handle access, NOT a USB SET/write.
        // The two IOCTLs above only query connection info and GET descriptors.
        using(var h=CreateFile(path,0x40000000,2,IntPtr.Zero,3,0,IntPtr.Zero)) {
            if(h.IsInvalid) throw new Win32Exception(Marshal.GetLastWin32Error());
            byte[] request=new byte[4096]; request[0]=1;
            var connection=Query(h,0x220448,request);
            if(connection.Length<35 || BitConverter.ToUInt16(connection,12)!=0x2b7e ||
                BitConverter.ToUInt16(connection,14)!=0xb663 ||
                BitConverter.ToUInt32(connection,31)!=1)
                throw new InvalidOperationException("Exact camera/connected status not confirmed");
            var header=GetDescriptor(h,2,0,9);
            if(header.Length<9 || header[0]!=9 || header[1]!=2)
                throw new InvalidOperationException("Invalid config header");
            ushort total=BitConverter.ToUInt16(header,2);
            if(total<9 || total>16384) throw new InvalidOperationException("Invalid config length");
            var config=GetDescriptor(h,2,0,total);
            if(config.Length!=total) throw new InvalidOperationException("Incomplete config");
            return new byte[][] {connection,config};
        }
    }
}
'@
$result=[WujieCameraDescriptors]::Read()
[IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'camera-usb-connection.bin'),$result[0])
[IO.File]::WriteAllBytes((Join-Path $PSScriptRoot 'camera-usb-config.bin'),$result[1])
[ordered]@{
    sampledAt=[DateTimeOffset]::Now.ToString('o')
    device='USB VID_2B7E PID_B663 REV_0008'
    port=1
    handleAccess='GENERIC_WRITE (USBView hub-open convention; no writes issued)'
    ioctls=@('0x220448 connection info','0x220410 GET_DESCRIPTOR Configuration')
    streamStarted=$false
    controlSetSent=$false
    connectionBytes=$result[0].Length
    configurationBytes=$result[1].Length
} | ConvertTo-Json | Set-Content (Join-Path $PSScriptRoot 'camera-usb-descriptors.json') -Encoding utf8
Get-Content (Join-Path $PSScriptRoot 'camera-usb-descriptors.json')
