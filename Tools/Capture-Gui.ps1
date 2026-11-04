# ---------------------------------------------------------------------------
# Launches MagicControl, waits for its window, captures it to a PNG and closes it.
#
# Everything happens inside one process on purpose: a launched GUI child does not
# survive the shell command that started it in every environment, so the capture
# must occur while the launcher is still alive.
#
# A protocol is written next to the PNG so the run can be reviewed afterwards
# without relying on console output.
#
# Usage:
#   powershell -NoProfile -File Tools/Capture-Gui.ps1 -Output docs/screenshots/x.png
# ---------------------------------------------------------------------------
[CmdletBinding()]
param(
    [string] $Exe,
    [string] $Output = 'MagicControl-window.png',
    [int] $TimeoutSeconds = 25,
    [int] $SettleMilliseconds = 2600,
    [string] $Tab = '',
    [int] $ScrollNotches = 0,
    [int] $ScrollDelayMilliseconds = 90,
    [int] $ScrollFineSteps = 0,
    [int] $ScrollFineDelta = 20,
    [string] $ClickAt = '',
    [switch] $KeepRunning
)

$ErrorActionPreference = 'Stop'

$root = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $root 'build\windows\bin\Release\MagicControl.exe' }

$fullOutput = [System.IO.Path]::GetFullPath((Join-Path $root $Output))
$logPath = [System.IO.Path]::ChangeExtension($fullOutput, '.log')

$log = New-Object System.Collections.Generic.List[string]
function Write-Log([string] $message) {
    $line = "{0:HH:mm:ss.fff}  {1}" -f (Get-Date), $message
    $log.Add($line)
    Write-Host $line
}

function Save-Log {
    $directory = [System.IO.Path]::GetDirectoryName($logPath)
    if ($directory -and -not (Test-Path $directory)) {
        New-Item -ItemType Directory -Force -Path $directory | Out-Null
    }
    # Encoding must be explicit: PowerShell 5.1 writes a byte order mark with
    # -Encoding utf8, and a BOM is not part of the text. UTF8Encoding($false)
    # produces plain UTF-8, which matches the project convention because the
    # same files are consumed on Linux.
    $utf8NoBom = New-Object System.Text.UTF8Encoding($false)
    [System.IO.File]::WriteAllLines($logPath, $log, $utf8NoBom)
}

Add-Type -AssemblyName System.Drawing
Add-Type -AssemblyName System.Windows.Forms

Add-Type -ReferencedAssemblies 'System.Drawing' -TypeDefinition @'
using System;
using System.Drawing;
using System.Runtime.InteropServices;

public static class GuiCapture {
    [DllImport("user32.dll", SetLastError = true)]
    public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool SetForegroundWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsWindowVisible(IntPtr hWnd);

    [DllImport("user32.dll")]
    public static extern uint GetDpiForWindow(IntPtr hWnd);

    [DllImport("user32.dll")]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool PrintWindow(IntPtr hWnd, IntPtr hdc, uint flags);

    [DllImport("user32.dll")]
    private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);

    [DllImport("user32.dll")]
    private static extern void mouse_event(uint flags, int dx, int dy, int data, UIntPtr extra);

    private const uint MOUSEEVENTF_WHEEL = 0x0800;
    private const int WHEEL_DELTA = 120;

    /// Sends one wheel notch downwards.
    public static void ScrollDown() {
        mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -WHEEL_DELTA, UIntPtr.Zero);
    }

    /// Sends an arbitrary wheel delta, in WHEEL_DELTA units.
    ///
    /// A precision touchpad does not send whole notches: it streams many small
    /// values, so testing with 120 only exercises the easy path.
    public static void ScrollBy(int delta) {
        mouse_event(MOUSEEVENTF_WHEEL, 0, 0, -delta, UIntPtr.Zero);
    }

    /// Sends one wheel notch upwards.
    public static void ScrollUp() {
        mouse_event(MOUSEEVENTF_WHEEL, 0, 0, WHEEL_DELTA, UIntPtr.Zero);
    }

    [DllImport("user32.dll")]
    private static extern bool SetCursorPos(int x, int y);

    private const uint MOUSEEVENTF_LEFTDOWN = 0x0002;
    private const uint MOUSEEVENTF_LEFTUP = 0x0004;

    /// Moves the pointer and performs a left click at a screen position.
    public static void ClickAt(int screenX, int screenY) {
        SetCursorPos(screenX, screenY);
        System.Threading.Thread.Sleep(120);
        mouse_event(MOUSEEVENTF_LEFTDOWN, 0, 0, 0, UIntPtr.Zero);
        System.Threading.Thread.Sleep(60);
        mouse_event(MOUSEEVENTF_LEFTUP, 0, 0, 0, UIntPtr.Zero);
    }

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left, Top, Right, Bottom; }

    /// Makes this process per-monitor DPI aware.
    ///
    /// Without it a script host is treated as DPI unaware, Windows virtualises
    /// every coordinate it reads, and GetWindowRect returns half the real size on
    /// a 200 percent display. Capturing that rectangle yields only the top-left
    /// quadrant of the window, which looks like a layout bug in the application
    /// when the application is in fact correct.
    public static void MakeDpiAware() {
        // DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2 == (HANDLE)-4
        SetThreadDpiAwarenessContext(new IntPtr(-4));
    }

    /// Asks the window to paint itself into a bitmap.
    ///
    /// Preferred over CopyFromScreen: it renders through the window's own device
    /// context, so the result is exactly the window at its true size regardless
    /// of scaling, occlusion, or what else is on screen.
    public static Bitmap Render(IntPtr hWnd, int width, int height, out bool succeeded) {
        Bitmap bitmap = new Bitmap(width, height);
        using (Graphics graphics = Graphics.FromImage(bitmap)) {
            IntPtr hdc = graphics.GetHdc();
            // PW_RENDERFULLCONTENT (2) is required for windows that draw with
            // composition or through a redirected surface.
            succeeded = PrintWindow(hWnd, hdc, 2);
            graphics.ReleaseHdc(hdc);
        }
        return bitmap;
    }
}
'@

# Must happen before any window measurement is read.
[GuiCapture]::MakeDpiAware()

$process = $null
try {
    if (-not (Test-Path $Exe)) { throw "Executable not found: $Exe" }
    Write-Log "executable : $Exe"

    # An optional page selector, so a screenshot can open on BIOS or SETs
    # directly instead of needing a click first.
    $startArguments = @()
    if ($Tab) {
        $startArguments = @("--tab=$Tab")
        Write-Log "start tab  : $Tab"
    }

    # -ArgumentList rejects an empty collection, so the call is built with the
    # parameter only when there is something to pass.
    if ($startArguments.Count -gt 0) {
        $process = Start-Process -FilePath $Exe -ArgumentList $startArguments -PassThru
    } else {
        $process = Start-Process -FilePath $Exe -PassThru
    }
    Write-Log "started pid $($process.Id)"

    # --- Wait for the window handle ----------------------------------------
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    $handle = [IntPtr]::Zero
    while ((Get-Date) -lt $deadline) {
        Start-Sleep -Milliseconds 250
        $process.Refresh()
        if ($process.HasExited) { throw "The process exited early with code $($process.ExitCode)." }
        if ($process.MainWindowHandle -ne [IntPtr]::Zero) {
            $handle = $process.MainWindowHandle
            break
        }
    }
    if ($handle -eq [IntPtr]::Zero) { throw "No window appeared within $TimeoutSeconds seconds." }

    Write-Log "window     : handle=$handle title='$($process.MainWindowTitle)'"
    Write-Log "visible    : $([GuiCapture]::IsWindowVisible($handle))"
    Write-Log "dpi        : $([GuiCapture]::GetDpiForWindow($handle))"

    # Let the first paint happen and give the sensor timer time to poll the
    # embedded controller, so the capture shows live values rather than dashes.
    [void][GuiCapture]::SetForegroundWindow($handle)
    Start-Sleep -Milliseconds $SettleMilliseconds

    # --- Measure ------------------------------------------------------------
    $rect = New-Object GuiCapture+RECT
    if (-not [GuiCapture]::GetWindowRect($handle, [ref] $rect)) { throw 'GetWindowRect failed.' }

    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    Write-Log "rect       : $($rect.Left),$($rect.Top) ${width}x${height}"
    if ($width -le 0 -or $height -le 0) { throw "Empty window rectangle: ${width}x${height}" }

    # Optional wheel scrolling, so a capture can show content below the fold.
    # The pointer is placed over the middle of the window first, because a wheel
    # message goes to whatever window is under the cursor.
    if ($ScrollFineSteps -gt 0) {
        [System.Windows.Forms.Cursor]::Position =
            New-Object System.Drawing.Point (($rect.Left + [int]($width / 2)),
                                             ($rect.Top + [int]($height / 2)))
        Start-Sleep -Milliseconds 200
        Write-Log "fine scroll : $ScrollFineSteps steps of $ScrollFineDelta units"
        for ($i = 0; $i -lt $ScrollFineSteps; $i++) {
            [GuiCapture]::ScrollBy($ScrollFineDelta)
            Start-Sleep -Milliseconds $ScrollDelayMilliseconds
        }
        Start-Sleep -Milliseconds 600
    }

    if ($ScrollNotches -ne 0) {
        [System.Windows.Forms.Cursor]::Position =
            New-Object System.Drawing.Point (($rect.Left + [int]($width / 2)),
                                             ($rect.Top + [int]($height / 2)))
        Start-Sleep -Milliseconds 200
        for ($i = 0; $i -lt [Math]::Abs($ScrollNotches); $i++) {
            if ($ScrollNotches -gt 0) { [GuiCapture]::ScrollDown() } else { [GuiCapture]::ScrollUp() }
            Start-Sleep -Milliseconds $ScrollDelayMilliseconds
        }
        Start-Sleep -Milliseconds 600
        Write-Log "scrolled   : $ScrollNotches notch(es)"
    }

    # Optional click at logical client coordinates, for driving a button before
    # the capture. Coordinates are given in the same logical pixels the layout
    # uses, so they do not change with display scaling.
    if ($ClickAt) {
        $parts = $ClickAt -split ','
        if ($parts.Count -ne 2) { throw "-ClickAt expects 'x,y' in logical client pixels." }
        $scale = [GuiCapture]::GetDpiForWindow($handle) / 96.0
        $logicalX = [int]$parts[0]
        $logicalY = [int]$parts[1]
        $screenX = $rect.Left + [int]($logicalX * $scale)
        $screenY = $rect.Top + [int]($logicalY * $scale)
        Write-Log "click      : client $logicalX,$logicalY -> screen $screenX,$screenY (scale $scale)"
        [GuiCapture]::ClickAt($screenX, $screenY)
        Start-Sleep -Milliseconds 900
    }

    # --- Capture ------------------------------------------------------------
    $bitmap = $null
    $succeeded = $false
    try {
        $bitmap = [GuiCapture]::Render($handle, $width, $height, [ref] $succeeded)
        Write-Log "printwindow: $succeeded"
        if (-not $succeeded) {
            # Fall back to a screen copy, which is correct at 100 percent scaling.
            Write-Log 'fallback   : CopyFromScreen'
            $bitmap.Dispose()
            $bitmap = New-Object System.Drawing.Bitmap $width, $height
            $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
            $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
            $graphics.Dispose()
        }

        $directory = [System.IO.Path]::GetDirectoryName($fullOutput)
        if ($directory -and -not (Test-Path $directory)) {
            New-Item -ItemType Directory -Force -Path $directory | Out-Null
        }
        $bitmap.Save($fullOutput, [System.Drawing.Imaging.ImageFormat]::Png)
    } finally {
        if ($bitmap) { $bitmap.Dispose() }
    }

    $size = (Get-Item $fullOutput).Length
    Write-Log "saved      : $fullOutput ($size bytes)"
    Write-Log 'result     : OK'
    $exitCode = 0
} catch {
    Write-Log "error      : $($_.Exception.Message)"
    Write-Log 'result     : FAILED'
    $exitCode = 1
} finally {
    if ($process -and -not $KeepRunning) {
        try {
            $process.Refresh()
            if (-not $process.HasExited) {
                $process.Kill()
                Write-Log "closed     : pid $($process.Id)"
            }
        } catch {
            Write-Log "close note : $($_.Exception.Message)"
        }
    }
    Save-Log
}

exit $exitCode
