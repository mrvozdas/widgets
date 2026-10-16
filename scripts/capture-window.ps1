param(
    [Parameter(Mandatory = $true)][ValidateRange(1, 2147483647)][int]$ProcessId,
    [Parameter(Mandatory = $true)][string]$OutputPng
)

$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
if ([Environment]::OSVersion.Platform -ne [PlatformID]::Win32NT) {
    throw 'Window capture requires Windows.'
}
$outputPath = $ExecutionContext.SessionState.Path.GetUnresolvedProviderPathFromPSPath($OutputPng)
if ([IO.Path]::GetExtension($outputPath) -ne '.png') { throw 'Window capture output must use a .png extension.' }
[IO.Directory]::CreateDirectory([IO.Path]::GetDirectoryName($outputPath)) | Out-Null

if (-not ('W11WidgetWindowCapture' -as [type])) {
    Add-Type -TypeDefinition @'
using System;
using System.ComponentModel;
using System.Runtime.InteropServices;

public sealed class W11WidgetCaptureResult {
    public string path;
    public int processId;
    public string windowHandle;
    public int left;
    public int top;
    public int width;
    public int height;
    public string capturedAtUtc;
    public IntPtr bitmapHandle;
}

public static class W11WidgetWindowCapture {
    [StructLayout(LayoutKind.Sequential)] private struct Rect { public int Left, Top, Right, Bottom; }
    private delegate bool EnumWindowProc(IntPtr window, IntPtr parameter);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool EnumWindows(EnumWindowProc callback, IntPtr parameter);
    [DllImport("user32.dll")] private static extern bool IsWindowVisible(IntPtr window);
    [DllImport("user32.dll")] private static extern bool IsIconic(IntPtr window);
    [DllImport("user32.dll")] private static extern uint GetWindowThreadProcessId(IntPtr window, out uint processId);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool GetWindowRect(IntPtr window, out Rect bounds);
    [DllImport("user32.dll", SetLastError = true)] private static extern bool SetWindowPos(IntPtr window, IntPtr insertAfter, int x, int y, int width, int height, uint flags);
    [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr SendMessageTimeoutW(IntPtr window, uint message, UIntPtr wParam, IntPtr lParam, uint flags, uint timeoutMilliseconds, out UIntPtr result);
    [DllImport("user32.dll", SetLastError = true)] private static extern IntPtr GetDC(IntPtr window);
    [DllImport("user32.dll")] private static extern int ReleaseDC(IntPtr window, IntPtr dc);
    [DllImport("user32.dll")] private static extern IntPtr SetThreadDpiAwarenessContext(IntPtr context);
    [DllImport("dwmapi.dll")] private static extern int DwmGetWindowAttribute(IntPtr window, int attribute, out int value, int size);
    [DllImport("dwmapi.dll")] private static extern int DwmFlush();
    [DllImport("gdi32.dll", SetLastError = true)] private static extern IntPtr CreateCompatibleDC(IntPtr dc);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern IntPtr CreateCompatibleBitmap(IntPtr dc, int width, int height);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern IntPtr SelectObject(IntPtr dc, IntPtr item);
    [DllImport("gdi32.dll")] private static extern bool DeleteObject(IntPtr item);
    [DllImport("gdi32.dll")] private static extern bool DeleteDC(IntPtr dc);
    [DllImport("gdi32.dll", SetLastError = true)] private static extern bool BitBlt(IntPtr destination, int x, int y, int width, int height, IntPtr source, int sourceX, int sourceY, uint operation);

    public static W11WidgetCaptureResult Capture(int processId, string path) {
        IntPtr previousDpi = SetThreadDpiAwarenessContext(new IntPtr(-4));
        IntPtr screen = IntPtr.Zero, memory = IntPtr.Zero, image = IntPtr.Zero, previousImage = IntPtr.Zero;
        bool transferredBitmap = false;
        try {
            IntPtr selected = IntPtr.Zero;
            long largestArea = 0;
            EnumWindowProc find = delegate(IntPtr window, IntPtr unused) {
                uint owner;
                GetWindowThreadProcessId(window, out owner);
                if (owner != processId || !IsWindowVisible(window) || IsIconic(window)) return true;
                int cloaked;
                if (DwmGetWindowAttribute(window, 14, out cloaked, sizeof(int)) == 0 && cloaked != 0) return true;
                Rect candidate;
                if (!GetWindowRect(window, out candidate)) return true;
                long width = (long)candidate.Right - candidate.Left;
                long height = (long)candidate.Bottom - candidate.Top;
                if (width > 0 && height > 0 && width * height > largestArea) {
                    selected = window;
                    largestArea = width * height;
                }
                return true;
            };
            if (!EnumWindows(find, IntPtr.Zero)) throw new Win32Exception(Marshal.GetLastWin32Error());
            if (selected == IntPtr.Zero) throw new InvalidOperationException("The launched process has no visible window to capture.");
            uint selectedOwner;
            GetWindowThreadProcessId(selected, out selectedOwner);
            if (selectedOwner != processId || !IsWindowVisible(selected))
                throw new InvalidOperationException("The selected window disappeared or changed owner.");
            // A hung UI must fail capture instead of blocking the benchmark before cleanup.
            UIntPtr response;
            if (SendMessageTimeoutW(selected, 0, UIntPtr.Zero, IntPtr.Zero, 0x0022, 1000, out response) == IntPtr.Zero)
                throw new InvalidOperationException("The launched process window is unresponsive or closed: WM_NULL did not complete within 1000 ms.");
            GetWindowThreadProcessId(selected, out selectedOwner);
            if (selectedOwner != processId || !IsWindowVisible(selected))
                throw new InvalidOperationException("The selected window disappeared or changed owner.");
            // SWP_ASYNCWINDOWPOS avoids waiting on the target UI thread; preserve size, position and focus.
            if (!SetWindowPos(selected, IntPtr.Zero, 0, 0, 0, 0, 0x4013)) throw new Win32Exception(Marshal.GetLastWin32Error());
            DwmFlush();
            System.Threading.Thread.Sleep(100);
            Rect bounds;
            GetWindowThreadProcessId(selected, out selectedOwner);
            if (selectedOwner != processId || !IsWindowVisible(selected) || !GetWindowRect(selected, out bounds))
                throw new InvalidOperationException("The selected window disappeared or changed owner.");
            int captureWidth = checked(bounds.Right - bounds.Left);
            int captureHeight = checked(bounds.Bottom - bounds.Top);
            if (captureWidth < 1 || captureHeight < 1) throw new InvalidOperationException("The selected window has no drawable area.");
            screen = GetDC(IntPtr.Zero);
            if (screen == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            memory = CreateCompatibleDC(screen);
            image = CreateCompatibleBitmap(screen, captureWidth, captureHeight);
            if (memory == IntPtr.Zero || image == IntPtr.Zero) throw new Win32Exception(Marshal.GetLastWin32Error());
            previousImage = SelectObject(memory, image);
            if (previousImage == IntPtr.Zero || previousImage == new IntPtr(-1)) throw new Win32Exception(Marshal.GetLastWin32Error());
            if (!BitBlt(memory, 0, 0, captureWidth, captureHeight, screen, bounds.Left, bounds.Top, 0x40CC0020))
                throw new Win32Exception(Marshal.GetLastWin32Error());
            transferredBitmap = true;
            return new W11WidgetCaptureResult { path = path, processId = processId,
                windowHandle = "0x" + selected.ToInt64().ToString("x"), left = bounds.Left, top = bounds.Top,
                width = captureWidth, height = captureHeight, capturedAtUtc = DateTime.UtcNow.ToString("o"), bitmapHandle = image };
        } finally {
            if (memory != IntPtr.Zero && previousImage != IntPtr.Zero && previousImage != new IntPtr(-1)) SelectObject(memory, previousImage);
            if (image != IntPtr.Zero && !transferredBitmap) DeleteObject(image);
            if (memory != IntPtr.Zero) DeleteDC(memory);
            if (screen != IntPtr.Zero) ReleaseDC(IntPtr.Zero, screen);
            if (previousDpi != IntPtr.Zero) SetThreadDpiAwarenessContext(previousDpi);
        }
    }
    public static void ReleaseBitmap(IntPtr image) { if (image != IntPtr.Zero) DeleteObject(image); }
}
'@
}

Add-Type -AssemblyName System.Drawing
$result = [W11WidgetWindowCapture]::Capture($ProcessId, $outputPath)
$bitmap = $null
try {
    $bitmap = [Drawing.Bitmap]::FromHbitmap($result.bitmapHandle)
    $bitmap.Save($outputPath, [Drawing.Imaging.ImageFormat]::Png)
} finally {
    if ($null -ne $bitmap) { $bitmap.Dispose() }
    [W11WidgetWindowCapture]::ReleaseBitmap($result.bitmapHandle)
}
[pscustomobject]@{
    path = $result.path
    processId = $result.processId
    windowHandle = $result.windowHandle
    left = $result.left
    top = $result.top
    width = $result.width
    height = $result.height
    capturedAtUtc = $result.capturedAtUtc
}
