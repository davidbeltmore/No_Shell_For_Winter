[CmdletBinding()]
param(
    [string]$ProjectPath = 'D:\Projects UE5\NoShellForWinter\NoShellForWinter.uproject',
    [string]$EditorPath = 'D:\Unreal Engine 5\Library\UE_5.8\Engine\Binaries\Win64\UnrealEditor.exe',
    [string]$MapFile = 'D:\Projects UE5\NoShellForWinter\Content\_Game\Hub\HUB.umap',
    [string]$OutputRoot = 'D:\Projects UE5\NoShellForWinter\Saved\TattooShopQA\CharacterCreationVisible58',
    [double]$TattooTabX = 0.5833,
    [double]$TattooTabY = 0.1952,
    [double]$AddX = 0.5860,
    [double]$AddY = 0.2279,
    [double]$ThumbnailX = 0.6995,
    [double]$ThumbnailY = 0.3067,
    [double]$AcceptX = 0.5781,
    [double]$AcceptY = 0.8750,
    [int]$EditorReadyTimeoutSeconds = 240,
    [int]$PIEReadyTimeoutSeconds = 45,
    [switch]$CharacterCreationInfoOnly,
    [switch]$KeepPIE,
    [switch]$DryRun
)

$ErrorActionPreference = 'Stop'
$ProjectPath = [System.IO.Path]::GetFullPath($ProjectPath)
$ProjectRoot = Split-Path -Parent $ProjectPath
$EditorPath = [System.IO.Path]::GetFullPath($EditorPath)
$MapFile = [System.IO.Path]::GetFullPath($MapFile)
$OutputRoot = [System.IO.Path]::GetFullPath($OutputRoot)
$stamp = Get-Date -Format 'yyyyMMdd_HHmmss'
$outDir = Join-Path $OutputRoot $stamp
New-Item -ItemType Directory -Path $outDir -Force | Out-Null

$report = [ordered]@{
    schema_version = 1
    started_at = (Get-Date).ToString('o')
    status = 'PENDING'
    project_path = $ProjectPath
    engine_path = $EditorPath
    expected_engine = '5.8'
    expected_map_file = $MapFile
    output_dir = $outDir
    automation_backend = 'WIN32_REAL_INPUT_AND_SCREEN_CAPTURE'
    uecp_used = $false
    visual_review_required = $true
    visual_pass = $null
    flow = @('HUB', 'PIE', 'CharacterCreation.Period', 'TattooTab', 'ADD', 'Thumbnail', 'Accept')
    clicks = @()
    key_events = @()
    screenshots = @()
    blocking_windows_dismissed = @()
    preflight = @()
    errors = @()
}

function Save-Report {
    $report.finished_at = (Get-Date).ToString('o')
    $report | ConvertTo-Json -Depth 30 | Set-Content -LiteralPath (Join-Path $outDir 'report.json') -Encoding UTF8
}

function Assert-File {
    param([string]$Path, [string]$Contract)
    $present = Test-Path -LiteralPath $Path -PathType Leaf
    $report.preflight += [ordered]@{ contract = $Contract; path = $Path; present = $present }
    if (-not $present) { throw "Missing $Contract`: $Path" }
}

Add-Type -AssemblyName System.Windows.Forms
Add-Type -AssemblyName System.Drawing
if (-not ('TattooShopCCVisualQA58.Native' -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
namespace TattooShopCCVisualQA58 {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
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
        [DllImport("user32.dll")] public static extern bool PostMessage(IntPtr hWnd, uint message, UIntPtr wParam, IntPtr lParam);
        [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
        [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
        [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
    }
}
'@
}

function Get-UnrealWindows {
    $items = [System.Collections.Generic.List[object]]::new()
    $callback = [TattooShopCCVisualQA58.EnumWindowsProc]{
        param([IntPtr]$handle, [IntPtr]$unused)
        if ([TattooShopCCVisualQA58.Native]::IsWindowVisible($handle)) {
            [uint32]$processId = 0
            [void][TattooShopCCVisualQA58.Native]::GetWindowThreadProcessId($handle, [ref]$processId)
            try { $processName = [System.Diagnostics.Process]::GetProcessById([int]$processId).ProcessName }
            catch { $processName = '' }
            if ($processName -ne 'UnrealEditor') { return $true }
            $titleBuilder = [System.Text.StringBuilder]::new(512)
            [void][TattooShopCCVisualQA58.Native]::GetWindowText($handle, $titleBuilder, $titleBuilder.Capacity)
            $title = $titleBuilder.ToString()
            if ($title -match 'NoShellForWinter|Unreal Editor|Preview|Play In Editor|Ultimate Engine Copilot') {
                $rect = [TattooShopCCVisualQA58.RECT]::new()
                [void][TattooShopCCVisualQA58.Native]::GetWindowRect($handle, [ref]$rect)
                $width = $rect.Right - $rect.Left
                $height = $rect.Bottom - $rect.Top
                if ($width -gt 500 -and $height -gt 350) {
                    $items.Add([pscustomobject]@{
                        Handle = $handle; Title = $title; ProcessId = $processId; ProcessName = $processName
                        Left = $rect.Left; Top = $rect.Top; Right = $rect.Right; Bottom = $rect.Bottom
                        Width = $width; Height = $height
                    })
                }
            }
        }
        return $true
    }
    [void][TattooShopCCVisualQA58.Native]::EnumWindows($callback, [IntPtr]::Zero)
    return @($items)
}

function Find-EditorWindow {
    return @(Get-UnrealWindows | Where-Object { $_.Title -match 'NoShellForWinter|Unreal Editor' } | Sort-Object Width -Descending)[0]
}

function Find-PIEWindow {
    $windows = @(Get-UnrealWindows)
    $report.window_candidates = @($windows | Select-Object Title, Left, Top, Right, Bottom, Width, Height)
    $preview = @($windows | Where-Object { $_.Title -match 'Preview|Play In Editor' } | Sort-Object Width -Descending)[0]
    if ($preview) { return $preview }
    return @($windows | Where-Object { $_.Title -match 'NoShellForWinter|Unreal Editor' } | Sort-Object Width -Descending)[0]
}

function Wait-Window {
    param([scriptblock]$Finder, [int]$TimeoutSeconds, [string]$Label)
    $deadline = (Get-Date).AddSeconds($TimeoutSeconds)
    while ((Get-Date) -lt $deadline) {
        $window = & $Finder
        if ($window) { return $window }
        Start-Sleep -Milliseconds 750
    }
    throw "Timed out waiting for $Label window."
}

function Activate-Window {
    param($Window)
    # Keep the editor maximized so relative click geometry cannot change after
    # activation (SW_RESTORE moves the window and invalidates recorded bounds).
    [void][TattooShopCCVisualQA58.Native]::ShowWindow($Window.Handle, 3)
    [TattooShopCCVisualQA58.Native]::SwitchToThisWindow($Window.Handle, $true)
    [TattooShopCCVisualQA58.Native]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    [TattooShopCCVisualQA58.Native]::keybd_event(0x12, 0, 0x0002, [UIntPtr]::Zero)
    [void][TattooShopCCVisualQA58.Native]::BringWindowToTop($Window.Handle)
    [void][TattooShopCCVisualQA58.Native]::SetForegroundWindow($Window.Handle)
    for ($attempt = 0; $attempt -lt 15; $attempt++) {
        if ([TattooShopCCVisualQA58.Native]::GetForegroundWindow() -eq $Window.Handle) { return }
        Start-Sleep -Milliseconds 100
        [void][TattooShopCCVisualQA58.Native]::SetForegroundWindow($Window.Handle)
    }
    throw "Could not bring Unreal window to the foreground: $($Window.Title)"
}

function Get-CurrentWindowGeometry {
    param($Window)
    $rect = [TattooShopCCVisualQA58.RECT]::new()
    if (-not [TattooShopCCVisualQA58.Native]::GetWindowRect($Window.Handle, [ref]$rect)) {
        throw "Could not read Unreal window geometry: $($Window.Title)"
    }
    return [pscustomobject]@{
        Handle = $Window.Handle; Title = $Window.Title
        Left = $rect.Left; Top = $rect.Top; Right = $rect.Right; Bottom = $rect.Bottom
        Width = $rect.Right - $rect.Left; Height = $rect.Bottom - $rect.Top
    }
}

function Dismiss-BlockingUnrealWindows {
    $blocking = @(Get-UnrealWindows | Where-Object { $_.Title -match 'Ultimate Engine Copilot' })
    foreach ($window in $blocking) {
        [void][TattooShopCCVisualQA58.Native]::PostMessage($window.Handle, 0x0010, [UIntPtr]::Zero, [IntPtr]::Zero)
        $report.blocking_windows_dismissed += [ordered]@{ title = $window.Title; process_id = $window.ProcessId }
        Start-Sleep -Milliseconds 800
    }
}

function Send-VirtualKey {
    param($Window, [byte]$VirtualKey, [string]$Label, [int]$HoldMs = 80)
    Activate-Window $Window
    [TattooShopCCVisualQA58.Native]::keybd_event($VirtualKey, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds $HoldMs
    [TattooShopCCVisualQA58.Native]::keybd_event($VirtualKey, 0, 0x0002, [UIntPtr]::Zero)
    $report.key_events += [ordered]@{ label = $Label; virtual_key = ('0x{0:X2}' -f $VirtualKey); hold_ms = $HoldMs }
}

function Send-AltP {
    param($Window, [string]$Label)
    Activate-Window $Window
    [TattooShopCCVisualQA58.Native]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    [TattooShopCCVisualQA58.Native]::keybd_event(0x50, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 100
    [TattooShopCCVisualQA58.Native]::keybd_event(0x50, 0, 0x0002, [UIntPtr]::Zero)
    [TattooShopCCVisualQA58.Native]::keybd_event(0x12, 0, 0x0002, [UIntPtr]::Zero)
    $report.key_events += [ordered]@{ label = $Label; keys = 'Alt+P' }
}

function Click-Relative {
    param($Window, [double]$XRatio, [double]$YRatio, [string]$Label)
    if ($XRatio -lt 0 -or $XRatio -gt 1 -or $YRatio -lt 0 -or $YRatio -gt 1) { throw "Invalid click ratio for $Label" }
    Activate-Window $Window
    $geometry = Get-CurrentWindowGeometry $Window
    $x = [int]($geometry.Left + ($geometry.Width * $XRatio))
    $y = [int]($geometry.Top + ($geometry.Height * $YRatio))
    [void][TattooShopCCVisualQA58.Native]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 180
    [TattooShopCCVisualQA58.Native]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 90
    [TattooShopCCVisualQA58.Native]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    $report.clicks += [ordered]@{ label = $Label; x = $x; y = $y; x_ratio = $XRatio; y_ratio = $YRatio }
    Start-Sleep -Milliseconds 900
}

function Capture-Window {
    param($Window, [string]$Name)
    $Window = Get-CurrentWindowGeometry $Window
    $path = Join-Path $outDir $Name
    $bitmap = [System.Drawing.Bitmap]::new($Window.Width, $Window.Height)
    $graphics = [System.Drawing.Graphics]::FromImage($bitmap)
    $graphics.CopyFromScreen($Window.Left, $Window.Top, 0, 0, [System.Drawing.Size]::new($Window.Width, $Window.Height))
    $bitmap.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $graphics.Dispose(); $bitmap.Dispose()
    $report.screenshots += $path
}

function Capture-ChestCrop {
    param($Window, [string]$Name)
    $Window = Get-CurrentWindowGeometry $Window
    $sourceX = [int]($Window.Left + ($Window.Width * 0.12))
    $sourceY = [int]($Window.Top + ($Window.Height * 0.16))
    $sourceW = [Math]::Max(1, [int]($Window.Width * 0.52))
    $sourceH = [Math]::Max(1, [int]($Window.Height * 0.72))
    $path = Join-Path $outDir $Name
    $source = [System.Drawing.Bitmap]::new($sourceW, $sourceH)
    $graphics = [System.Drawing.Graphics]::FromImage($source)
    $graphics.CopyFromScreen($sourceX, $sourceY, 0, 0, [System.Drawing.Size]::new($sourceW, $sourceH))
    $scaled = [System.Drawing.Bitmap]::new($sourceW * 2, $sourceH * 2)
    $scaledGraphics = [System.Drawing.Graphics]::FromImage($scaled)
    $scaledGraphics.InterpolationMode = [System.Drawing.Drawing2D.InterpolationMode]::HighQualityBicubic
    $scaledGraphics.DrawImage($source, 0, 0, $scaled.Width, $scaled.Height)
    $scaled.Save($path, [System.Drawing.Imaging.ImageFormat]::Png)
    $scaledGraphics.Dispose(); $scaled.Dispose(); $graphics.Dispose(); $source.Dispose()
    $report.screenshots += $path
    $report.chest_crop = [ordered]@{ x_ratio = 0.12; y_ratio = 0.16; width_ratio = 0.52; height_ratio = 0.72; scale = 2 }
}

try {
    Assert-File $ProjectPath 'NoShellForWinter project'
    Assert-File $EditorPath 'Unreal Editor 5.8 executable'
    Assert-File $MapFile 'HUB map'
    if (-not $CharacterCreationInfoOnly) {
        foreach ($relative in @(
            'Content\TattooShop\Blueprints\Widget\WBP_TattooShop.uasset',
            'Content\TattooShop\Blueprints\Widget\WBP_TattooCustomization.uasset',
            'Content\TattooShop\Blueprints\Widget\WBP_TattooCard.uasset',
            'Content\TattooShop\Texture\T_Heart.uasset',
            'Content\_Game\AutomaticTattoo\DT_AutomaticTattoos.uasset'
        )) { Assert-File (Join-Path $ProjectRoot $relative) $relative }
    }

    $report.coordinates = [ordered]@{
        tattoo_tab = @($TattooTabX, $TattooTabY); add = @($AddX, $AddY)
        thumbnail = @($ThumbnailX, $ThumbnailY); accept = @($AcceptX, $AcceptY)
    }
    if ($DryRun) {
        $report.status = 'DRY_RUN_PASS'
        Save-Report
        Write-Host "Dry-run report: $(Join-Path $outDir 'report.json')"
        exit 0
    }

    $editorWindow = Find-EditorWindow
    if (-not $editorWindow) {
        $arguments = @('"' + $ProjectPath + '"', '"' + $MapFile + '"', '-NoSplash')
        $process = Start-Process -FilePath $EditorPath -ArgumentList $arguments -PassThru
        $report.editor_launched = $true
        $report.editor_process_id = $process.Id
        $editorWindow = Wait-Window -Finder { Find-EditorWindow } -TimeoutSeconds $EditorReadyTimeoutSeconds -Label 'NoShellForWinter editor'
        Start-Sleep -Seconds 10
    } else {
        $report.editor_launched = $false
    }
    Dismiss-BlockingUnrealWindows
    $editorWindow = Wait-Window -Finder { Find-EditorWindow } -TimeoutSeconds 15 -Label 'unblocked NoShellForWinter editor'
    $report.editor_window = $editorWindow | Select-Object Title, Left, Top, Right, Bottom, Width, Height
    Capture-Window $editorWindow '01_hub_editor_before_pie.png'

    if ($CharacterCreationInfoOnly) {
        Send-AltP $editorWindow 'start_pie_alt_p'
    }
    else {
        # TattooShop's established path keeps the historical toolbar click.
        Click-Relative $editorWindow 0.2865 0.0800 'start_pie_play_toolbar'
    }
    Start-Sleep -Seconds 7
    $pieWindow = Wait-Window -Finder { Find-PIEWindow } -TimeoutSeconds $PIEReadyTimeoutSeconds -Label 'PIE/NoShellForWinter'
    $report.pie_window = $pieWindow | Select-Object Title, Left, Top, Right, Bottom, Width, Height
    Capture-Window $pieWindow '02_hub_pie_started.png'

    Send-VirtualKey $pieWindow 0xBE 'open_character_creation_period'
    Start-Sleep -Seconds 3
    $pieWindow = Find-PIEWindow
    Capture-Window $pieWindow '03_character_creation_open.png'

    if ($CharacterCreationInfoOnly) {
        $report.flow = @('HUB', 'PIE', 'CharacterCreation.Period', 'Info.NameGender')
        $report.automation_completed = $true
        $report.status = 'VISUAL_REVIEW_REQUIRED'
        $report.result_note = 'Review 03. PASS requires the Info tab to show Name plus exactly Male and Female.'
    }
    else {
        Click-Relative $pieWindow $TattooTabX $TattooTabY 'tattoo_tab'
        Capture-Window $pieWindow '04_tattoo_tab_open.png'

        Click-Relative $pieWindow $AddX $AddY 'add'
        Capture-Window $pieWindow '05_after_add.png'

        Click-Relative $pieWindow $ThumbnailX $ThumbnailY 'thumbnail'
        Capture-Window $pieWindow '06_thumbnail_selected.png'

        Click-Relative $pieWindow $AcceptX $AcceptY 'accept'
        Start-Sleep -Seconds 2
        Capture-Window $pieWindow '07_after_accept.png'
        Capture-ChestCrop $pieWindow '08_after_accept_chest_crop.png'

        $report.automation_completed = $true
        $report.status = 'VISUAL_REVIEW_REQUIRED'
        $report.result_note = 'Review 04-08. PASS requires TattooShop controls, a real thumbnail, and the accepted tattoo visible on the Player skin. This runner never applies a material or SkinnedDecal directly.'
    }
}
catch {
    $report.status = 'AUTOMATION_FAIL'
    $report.errors += $_.Exception.Message
    throw
}
finally {
    if (-not $KeepPIE -and -not $DryRun) {
        try {
            $window = Find-PIEWindow
            if ($window) { Send-VirtualKey $window 0x1B 'stop_pie_escape' }
        } catch { $report.errors += ('PIE stop failed: ' + $_.Exception.Message) }
    }
    Save-Report
    Write-Host "TattooShop Character Creation visible QA artifacts: $outDir"
}
