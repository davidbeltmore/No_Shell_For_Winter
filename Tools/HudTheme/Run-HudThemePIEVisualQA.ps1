[CmdletBinding()]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet('Auto', 'Red', 'Blue', 'Purple', 'Green', 'Black')]
    [string]$Theme,

    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',

    [string]$OutputRoot = '',

    [switch]$FullCoverage,

    [switch]$RecordFlicker,

    [switch]$IncludeCharacterCreator,

    [switch]$CommaBrightnessGate,

    [switch]$Altar
)

$ErrorActionPreference = 'Stop'

$root = (Resolve-Path -LiteralPath $ProjectRoot).Path
if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $root 'Saved\HudThemeRework\PIE'
}

$runDirectory = Join-Path $OutputRoot $Theme
New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null

$report = [ordered]@{
    schema_version = 1
    theme = $Theme
    started_local = (Get-Date).ToString('o')
    project_root = $root
    output_directory = $runDirectory
    window = $null
    key_events = @()
    screenshots = @()
    flicker_capture = $null
    brightness_gate = $null
    result = 'PENDING'
}

Add-Type -AssemblyName System.Drawing
if (-not ('HudThemePIEVisualQA.Native' -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
namespace HudThemePIEVisualQA {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }
    public static class Native {
        [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
        [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int count);
        [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
        [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
        [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
        [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
        [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int command);
        [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
        [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr hWnd, bool altTab);
        [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
        [DllImport("user32.dll")] public static extern int GetSystemMetrics(int index);
        [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
        [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    }
}
'@
}

function Get-PIEWindow {
    $items = [System.Collections.Generic.List[object]]::new()
    $callback = [HudThemePIEVisualQA.EnumWindowsProc]{
        param([IntPtr]$handle, [IntPtr]$unused)

        if (-not [HudThemePIEVisualQA.Native]::IsWindowVisible($handle)) {
            return $true
        }

        [uint32]$processIdValue = 0
        [void][HudThemePIEVisualQA.Native]::GetWindowThreadProcessId($handle, [ref]$processIdValue)
        try {
            $process = [System.Diagnostics.Process]::GetProcessById([int]$processIdValue)
        }
        catch {
            return $true
        }

        if ($process.ProcessName -ne 'UnrealEditor') {
            return $true
        }

        $titleBuilder = [System.Text.StringBuilder]::new(512)
        [void][HudThemePIEVisualQA.Native]::GetWindowText($handle, $titleBuilder, $titleBuilder.Capacity)
        $title = $titleBuilder.ToString()
        if ($title -notmatch 'NoShellForWinter Preview|Play In Editor|NoShellForWinter - Unreal Editor') {
            return $true
        }

        $rect = [HudThemePIEVisualQA.RECT]::new()
        [void][HudThemePIEVisualQA.Native]::GetWindowRect($handle, [ref]$rect)
        $items.Add([pscustomobject]@{
            Handle = $handle
            Title = $title
            ProcessId = $processIdValue
            Left = $rect.Left
            Top = $rect.Top
            Right = $rect.Right
            Bottom = $rect.Bottom
            Width = $rect.Right - $rect.Left
            Height = $rect.Bottom - $rect.Top
            IsPreview = $title -match 'NoShellForWinter Preview|Play In Editor'
        })
        return $true
    }

    [void][HudThemePIEVisualQA.Native]::EnumWindows($callback, [IntPtr]::Zero)
    $preview = @($items |
        Where-Object { $_.IsPreview } |
        Sort-Object Width -Descending |
        Select-Object -First 1)
    if ($preview.Count -gt 0) {
        return $preview
    }

    return @($items | Sort-Object Width -Descending | Select-Object -First 1)
}

function Get-CurrentWindowGeometry {
    param($Window)

    $rect = [HudThemePIEVisualQA.RECT]::new()
    if (-not [HudThemePIEVisualQA.Native]::GetWindowRect($Window.Handle, [ref]$rect)) {
        throw "Could not read PIE window geometry: $($Window.Title)"
    }

    return [pscustomobject]@{
        Handle = $Window.Handle
        Title = $Window.Title
        ProcessId = $Window.ProcessId
        Left = $rect.Left
        Top = $rect.Top
        Right = $rect.Right
        Bottom = $rect.Bottom
        Width = $rect.Right - $rect.Left
        Height = $rect.Bottom - $rect.Top
        IsPreview = $Window.Title -match 'NoShellForWinter Preview|Play In Editor'
    }
}

function Assert-PIEWindowAlive {
    param(
        $ExpectedWindow,
        [string]$Stage
    )

    $current = Get-PIEWindow
    if (-not $current -or $current.Handle -ne $ExpectedWindow.Handle) {
        throw "PIE ended before '$Stage'; no screenshot from this stage is valid."
    }

    if ($ExpectedWindow.IsPreview -and -not $current.IsPreview) {
        throw "PIE preview ended before '$Stage'; the editor fallback is not valid evidence."
    }

    return Get-CurrentWindowGeometry $current
}

function Activate-PIEWindow {
    param($Window)

    # Keep a stable maximized rectangle for comparable captures.
    [void][HudThemePIEVisualQA.Native]::ShowWindow($Window.Handle, 3)
    [HudThemePIEVisualQA.Native]::SwitchToThisWindow($Window.Handle, $true)
    [HudThemePIEVisualQA.Native]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    [HudThemePIEVisualQA.Native]::keybd_event(0x12, 0, 0x0002, [UIntPtr]::Zero)
    [void][HudThemePIEVisualQA.Native]::BringWindowToTop($Window.Handle)
    [void][HudThemePIEVisualQA.Native]::SetForegroundWindow($Window.Handle)

    for ($attempt = 0; $attempt -lt 15; $attempt++) {
        if ([HudThemePIEVisualQA.Native]::GetForegroundWindow() -eq $Window.Handle) {
            return
        }
        Start-Sleep -Milliseconds 100
        [void][HudThemePIEVisualQA.Native]::SetForegroundWindow($Window.Handle)
    }

    throw "Could not bring PIE window to the foreground: $($Window.Title)"
}

function Focus-EmbeddedPIEViewport {
    param($Window)

    if ($Window.IsPreview) {
        return
    }

    Activate-PIEWindow $Window
    $geometry = Get-CurrentWindowGeometry $Window
    # The main level viewport occupies the left/center editor area. This point
    # avoids the toolbar and Outliner while remaining inside the game viewport.
    $x = [int]($geometry.Left + ($geometry.Width * 0.34))
    $y = [int]($geometry.Top + ($geometry.Height * 0.48))
    [void][HudThemePIEVisualQA.Native]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 180
    [HudThemePIEVisualQA.Native]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 90
    [HudThemePIEVisualQA.Native]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 650
}

function Send-VirtualKey {
    param(
        $Window,
        [byte]$VirtualKey,
        [string]$Label,
        [int]$HoldMilliseconds = 80,
        [int]$SettleMilliseconds = 420
    )

    Activate-PIEWindow $Window
    [HudThemePIEVisualQA.Native]::keybd_event($VirtualKey, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $HoldMilliseconds
    [HudThemePIEVisualQA.Native]::keybd_event($VirtualKey, 0, 0x0002, [UIntPtr]::Zero)
    $report.key_events += [ordered]@{
        label = $Label
        virtual_key = ('0x{0:X2}' -f $VirtualKey)
        hold_ms = $HoldMilliseconds
        settle_ms = $SettleMilliseconds
    }
    Start-Sleep -Milliseconds $SettleMilliseconds
}

function Capture-PIEWindow {
    param(
        $Window,
        [string]$FileName
    )

    Activate-PIEWindow $Window
    Start-Sleep -Milliseconds 250
    $geometry = Get-CurrentWindowGeometry $Window
    $path = Join-Path $runDirectory $FileName
    $bitmap = [System.Drawing.Bitmap]::new($geometry.Width, $geometry.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen(
            $geometry.Left,
            $geometry.Top,
            0,
            0,
            [System.Drawing.Size]::new($geometry.Width, $geometry.Height))
        $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
    $report.screenshots += $path
    return $path
}

function Get-CentralWorldMeanLuminance {
    param([string]$ImagePath)

    $bitmap = [System.Drawing.Bitmap]::FromFile($ImagePath)
    try {
        # This central world region avoids the left Inner State/Doctrine stack,
        # the right Chronicle panel, and the top day/quest chrome.
        $left = [int]($bitmap.Width * 0.34)
        $right = [int]($bitmap.Width * 0.66)
        $top = [int]($bitmap.Height * 0.31)
        $bottom = [int]($bitmap.Height * 0.62)
        [double]$sum = 0.0
        [int64]$samples = 0
        for ($y = $top; $y -lt $bottom; $y += 4) {
            for ($x = $left; $x -lt $right; $x += 4) {
                $pixel = $bitmap.GetPixel($x, $y)
                $sum += (
                    (0.2126 * $pixel.R) +
                    (0.7152 * $pixel.G) +
                    (0.0722 * $pixel.B))
                $samples++
            }
        }
        if ($samples -le 0) {
            throw "Brightness ROI produced no samples: $ImagePath"
        }
        return $sum / $samples
    }
    finally {
        $bitmap.Dispose()
    }
}

function Open-ThemeSelectorAndApply {
    param(
        $Window,
        [string]$Preset
    )

    $presetIndex = [ordered]@{
        Auto = 0
        Red = 1
        Blue = 2
        Purple = 3
        Green = 4
        Black = 5
    }

    Send-VirtualKey $Window 0x4C 'Open gameplay debug menu (L)' -SettleMilliseconds 650
    Send-VirtualKey $Window 0x28 'Select Test' -SettleMilliseconds 180
    Send-VirtualKey $Window 0x28 'Select Appearance' -SettleMilliseconds 180
    Send-VirtualKey $Window 0x0D 'Open Appearance' -SettleMilliseconds 450
    Send-VirtualKey $Window 0x0D 'Open HUD Theme' -SettleMilliseconds 450

    for ($index = 0; $index -lt [int]$presetIndex[$Preset]; $index++) {
        Send-VirtualKey $Window 0x28 "Move to $Preset ($($index + 1))" -SettleMilliseconds 150
    }

    Send-VirtualKey $Window 0x0D "Apply HUD Theme $Preset" -SettleMilliseconds 1500
}

$window = Get-PIEWindow
if (-not $window) {
    throw 'No active NoShellForWinter PIE preview window was found.'
}
if ($CommaBrightnessGate -and -not $window.IsPreview) {
    throw 'Comma brightness gate requires a live floating PIE Preview window; editor fallback is rejected.'
}

Activate-PIEWindow $window
Start-Sleep -Milliseconds 500
$window = Get-CurrentWindowGeometry $window
Focus-EmbeddedPIEViewport $window
$report.window = [ordered]@{
    title = $window.Title
    process_id = $window.ProcessId
    handle = ('0x{0:X}' -f $window.Handle.ToInt64())
    left = $window.Left
    top = $window.Top
    width = $window.Width
    height = $window.Height
}

if ($CommaBrightnessGate) {
    $window = Assert-PIEWindowAlive $window 'Comma brightness base capture'
    $basePath = Capture-PIEWindow $window '07_Comma_Brightness_Base.png'
    Send-VirtualKey $window 0xBC 'Open Needs and Status HUD (Comma)' -SettleMilliseconds 450
    $window = Assert-PIEWindowAlive $window 'Comma brightness open capture'
    $openPath = Capture-PIEWindow $window '08_Comma_Brightness_Open.png'
    $baseLuminance = Get-CentralWorldMeanLuminance $basePath
    $openLuminance = Get-CentralWorldMeanLuminance $openPath
    $minimumRatio = 0.90
    $ratio = if ($baseLuminance -gt 0.001) {
        $openLuminance / $baseLuminance
    }
    else {
        0.0
    }
    $report.brightness_gate = [ordered]@{
        region = [ordered]@{
            left_ratio = 0.34
            right_ratio = 0.66
            top_ratio = 0.31
            bottom_ratio = 0.62
            sample_stride_pixels = 4
        }
        base_mean_luminance = $baseLuminance
        open_mean_luminance = $openLuminance
        open_to_base_ratio = $ratio
        minimum_allowed_ratio = $minimumRatio
        result = if ($ratio -ge $minimumRatio) { 'PASS' } else { 'FAIL' }
    }
    if ($ratio -lt $minimumRatio) {
        throw (
            "Comma HUD brightness gate failed: central world luminance ratio " +
            ('{0:N4}' -f $ratio) +
            " is below " +
            ('{0:N2}' -f $minimumRatio))
    }
    Send-VirtualKey $window 0xBC 'Close Needs and Status HUD (Comma)' -SettleMilliseconds 150
}
else {
    Open-ThemeSelectorAndApply $window $Theme
    $window = Assert-PIEWindowAlive $window 'HUD theme selection capture'
    Capture-PIEWindow $window '01_L_HUD_Theme_Selected.png' | Out-Null
    Send-VirtualKey $window 0x4C 'Close gameplay debug menu (L)' -SettleMilliseconds 650
    $window = Assert-PIEWindowAlive $window 'HUD base capture'
    Capture-PIEWindow $window '02_HUD_Base.png' | Out-Null

    if ($Altar) {
        Send-VirtualKey $window 0x45 'Open Inner Doctrine altar (E)' -SettleMilliseconds 1600
        $window = Assert-PIEWindowAlive $window 'altar exchange menu capture'
        Capture-PIEWindow $window '07_Altar_InnerDoctrine_DXP.png' | Out-Null
        Send-VirtualKey $window 0x51 'Close altar exchange menu (Q)' -SettleMilliseconds 450
    }
}

if ($FullCoverage -and -not $CommaBrightnessGate -and -not $Altar) {
    Send-VirtualKey $window 0xBC 'Open Needs and Status HUD (Comma)' -SettleMilliseconds 1200
    Send-VirtualKey $window 0x4A 'Expand Chronicle (J)' -SettleMilliseconds 1200
    Capture-PIEWindow $window '03_Comma_Needs_Status_Chronicle.png' | Out-Null

    if ($RecordFlicker) {
        $ffmpegCandidates = @(
            'C:\Program Files\Streamlabs OBS\resources\app.asar.unpacked\node_modules\obs-studio-node\ffmpeg.exe',
            'ffmpeg.exe'
        )
        $ffmpeg = $ffmpegCandidates | Where-Object { $_ -eq 'ffmpeg.exe' -or (Test-Path -LiteralPath $_ -PathType Leaf) } | Select-Object -First 1
        if (-not $ffmpeg) {
            throw 'ffmpeg was not found for the anti-flicker capture.'
        }

        Activate-PIEWindow $window
        Start-Sleep -Milliseconds 1500
        $captureGeometry = Get-CurrentWindowGeometry $window
        $primaryWidth = [HudThemePIEVisualQA.Native]::GetSystemMetrics(0)
        $primaryHeight = [HudThemePIEVisualQA.Native]::GetSystemMetrics(1)
        $captureLeft = [Math]::Max(0, $captureGeometry.Left)
        $captureTop = [Math]::Max(0, $captureGeometry.Top)
        $captureRight = [Math]::Min($primaryWidth, $captureGeometry.Right)
        $captureBottom = [Math]::Min($primaryHeight, $captureGeometry.Bottom)
        $captureWidth = $captureRight - $captureLeft
        $captureHeight = $captureBottom - $captureTop
        if ($captureWidth -lt 640 -or $captureHeight -lt 360) {
            throw "PIE capture region is invalid: ${captureWidth}x${captureHeight} at ${captureLeft},${captureTop}"
        }
        $videoPath = Join-Path $runDirectory '04_Stable_HUD_60fps_3.5s.mp4'
        & $ffmpeg `
            -y `
            -hide_banner `
            -loglevel warning `
            -f gdigrab `
            -framerate 60 `
            -draw_mouse 0 `
            -offset_x $captureLeft `
            -offset_y $captureTop `
            -video_size "${captureWidth}x${captureHeight}" `
            -i desktop `
            -t 3.5 `
            -c:v libx264 `
            -preset ultrafast `
            -crf 12 `
            -pix_fmt yuv420p `
            $videoPath
        if ($LASTEXITCODE -ne 0 -or -not (Test-Path -LiteralPath $videoPath -PathType Leaf)) {
            throw "Anti-flicker capture failed: $videoPath"
        }

        $report.flicker_capture = [ordered]@{
            path = $videoPath
            requested_fps = 60
            requested_duration_seconds = 3.5
            codec = 'libx264'
            crf = 12
            offset_x = $captureLeft
            offset_y = $captureTop
            width = $captureWidth
            height = $captureHeight
        }
    }

    Send-VirtualKey $window 0x4A 'Collapse Chronicle (J)' -SettleMilliseconds 650
    Send-VirtualKey $window 0xBC 'Close Needs and Status HUD (Comma)' -SettleMilliseconds 650

    Send-VirtualKey $window 0x59 'Open actions and emotes menu (Y)' -SettleMilliseconds 1200
    Capture-PIEWindow $window '05_Y_Actions_Emotes.png' | Out-Null
    Send-VirtualKey $window 0x59 'Close actions and emotes menu (Y)' -SettleMilliseconds 650

    if ($IncludeCharacterCreator) {
        Send-VirtualKey $window 0xBE 'Open character creator (Period)' -SettleMilliseconds 1600
        Capture-PIEWindow $window '06_Character_Creator.png' | Out-Null
        Send-VirtualKey $window 0xBE 'Close character creator (Period)' -SettleMilliseconds 800
    }
}

$report.result = 'PASS_CAPTURED'
$report.finished_local = (Get-Date).ToString('o')
$reportPath = Join-Path $runDirectory 'PIEVisualQA.json'
$report | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $reportPath -Encoding utf8

[pscustomobject]@{
    Theme = $Theme
    Result = $report.result
    Screenshots = @($report.screenshots).Count
    FlickerCapture = if ($report.flicker_capture) { $report.flicker_capture.path } else { '' }
    Report = $reportPath
} | Format-List
