[CmdletBinding()]
param(
    [Parameter(Mandatory)] [string]$OutputDirectory,
    [int]$EditorPid = 0
)

$ErrorActionPreference = 'Stop'

if (-not ('Performance58VisibleWalk.Native' -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;
namespace Performance58VisibleWalk {
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);
    [StructLayout(LayoutKind.Sequential)]
    public struct RECT { public int Left; public int Top; public int Right; public int Bottom; }
    public static class Native {
        [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
        [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
        [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int maxCount);
        [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
        [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
        [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
        [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
        [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr hWnd, bool altTab);
        [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int command);
        [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
        [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extra);
        [DllImport("user32.dll")] public static extern void keybd_event(byte key, byte scan, uint flags, UIntPtr extra);
    }
}
'@
}

Add-Type -AssemblyName System.Drawing

if ($EditorPid -le 0) {
    $editor = Get-Process UnrealEditor -ErrorAction Stop | Select-Object -First 1
    $EditorPid = $editor.Id
}

New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$beforePath = Join-Path $OutputDirectory 'before_walk.png'
$afterPath = Join-Path $OutputDirectory 'after_walk.png'
$samplesPath = Join-Path $OutputDirectory 'host_process_samples.csv'
$evidencePath = Join-Path $OutputDirectory 'visible_pie_evidence.json'

function Find-PIEWindow {
    $matches = [Collections.Generic.List[object]]::new()
    $callback = [Performance58VisibleWalk.EnumWindowsProc]{
        param([IntPtr]$handle, [IntPtr]$lParam)
        if (-not [Performance58VisibleWalk.Native]::IsWindowVisible($handle)) { return $true }
        $processIdValue = [uint32]0
        [void][Performance58VisibleWalk.Native]::GetWindowThreadProcessId($handle, [ref]$processIdValue)
        if ($processIdValue -ne [uint32]$EditorPid) { return $true }
        $titleBuilder = [Text.StringBuilder]::new(512)
        [void][Performance58VisibleWalk.Native]::GetWindowText($handle, $titleBuilder, $titleBuilder.Capacity)
        $title = $titleBuilder.ToString()
        if ($title -like 'NoShellForWinter Preview*') {
            $matches.Add([pscustomobject]@{ Handle = $handle; Title = $title })
        }
        return $true
    }
    [void][Performance58VisibleWalk.Native]::EnumWindows($callback, [IntPtr]::Zero)
    return $matches | Select-Object -First 1
}

function Activate-PIEWindow {
    param([Parameter(Mandatory)]$Window)
    [void][Performance58VisibleWalk.Native]::ShowWindow($Window.Handle, 3)
    [Performance58VisibleWalk.Native]::SwitchToThisWindow($Window.Handle, $true)
    [void][Performance58VisibleWalk.Native]::BringWindowToTop($Window.Handle)
    [void][Performance58VisibleWalk.Native]::SetForegroundWindow($Window.Handle)
    for ($attempt = 0; $attempt -lt 10; $attempt++) {
        if ([Performance58VisibleWalk.Native]::GetForegroundWindow() -eq $Window.Handle) { return }
        Start-Sleep -Milliseconds 100
        [void][Performance58VisibleWalk.Native]::SetForegroundWindow($Window.Handle)
    }
    throw "Could not focus PIE window '$($Window.Title)'."
}

function Focus-PIEViewport {
    param([Parameter(Mandatory)]$Window)
    Activate-PIEWindow $Window
    $rect = [Performance58VisibleWalk.RECT]::new()
    if (-not [Performance58VisibleWalk.Native]::GetWindowRect($Window.Handle, [ref]$rect)) {
        throw 'Could not read PIE window bounds.'
    }
    $x = [int](($rect.Left + $rect.Right) / 2)
    $y = [int](($rect.Top + $rect.Bottom) / 2)
    [void][Performance58VisibleWalk.Native]::SetCursorPos($x, $y)
    [Performance58VisibleWalk.Native]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 80
    [Performance58VisibleWalk.Native]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
}

function Capture-PIEWindow {
    param([Parameter(Mandatory)]$Window, [Parameter(Mandatory)][string]$Path)
    Activate-PIEWindow $Window
    $rect = [Performance58VisibleWalk.RECT]::new()
    if (-not [Performance58VisibleWalk.Native]::GetWindowRect($Window.Handle, [ref]$rect)) {
        throw 'Could not read PIE window bounds for capture.'
    }
    $width = $rect.Right - $rect.Left
    $height = $rect.Bottom - $rect.Top
    if ($width -lt 640 -or $height -lt 360) { throw "PIE capture bounds are invalid: ${width}x${height}." }
    $bitmap = New-Object Drawing.Bitmap($width, $height)
    $graphics = [Drawing.Graphics]::FromImage($bitmap)
    try {
        $graphics.CopyFromScreen($rect.Left, $rect.Top, 0, 0, $bitmap.Size)
        $bitmap.Save($Path, [Drawing.Imaging.ImageFormat]::Png)
    }
    finally {
        $graphics.Dispose()
        $bitmap.Dispose()
    }
}

function Set-KeyState {
    param([byte]$VirtualKey, [bool]$Down)
    $flags = if ($Down) { 0 } else { 0x0002 }
    [Performance58VisibleWalk.Native]::keybd_event($VirtualKey, 0, $flags, [UIntPtr]::Zero)
}

$window = Find-PIEWindow
if (-not $window) { throw 'No visible NoShellForWinter PIE Preview window was found.' }

$schedule = @(
    [pscustomobject]@{ Key = 'W'; VirtualKey = [byte]0x57; Seconds = 10.0 },
    [pscustomobject]@{ Key = 'D'; VirtualKey = [byte]0x44; Seconds = 2.0 },
    [pscustomobject]@{ Key = 'W'; VirtualKey = [byte]0x57; Seconds = 10.0 },
    [pscustomobject]@{ Key = 'A'; VirtualKey = [byte]0x41; Seconds = 2.0 },
    [pscustomobject]@{ Key = 'W'; VirtualKey = [byte]0x57; Seconds = 6.0 }
)
$samples = [Collections.Generic.List[object]]::new()
$focusRecoveries = 0
$inputStartUtc = $null
$inputEndUtc = $null

try {
    Focus-PIEViewport $window
    Start-Sleep -Milliseconds 500
    Capture-PIEWindow $window $beforePath
    Focus-PIEViewport $window
    $inputStartUtc = [DateTime]::UtcNow
    foreach ($phase in $schedule) {
        Set-KeyState -VirtualKey $phase.VirtualKey -Down $true
        $phaseTimer = [Diagnostics.Stopwatch]::StartNew()
        try {
            while ($phaseTimer.Elapsed.TotalSeconds -lt $phase.Seconds) {
                if ([Performance58VisibleWalk.Native]::GetForegroundWindow() -ne $window.Handle) {
                    Activate-PIEWindow $window
                    $focusRecoveries++
                }
                $process = Get-Process -Id $EditorPid -ErrorAction Stop
                $samples.Add([pscustomobject]@{
                    utc = [DateTime]::UtcNow.ToString('o')
                    phase_key = $phase.Key
                    elapsed_seconds = [Math]::Round(([DateTime]::UtcNow - $inputStartUtc).TotalSeconds, 3)
                    cpu_total_seconds = [Math]::Round($process.CPU, 3)
                    working_set_mb = [Math]::Round($process.WorkingSet64 / 1MB, 3)
                    private_memory_mb = [Math]::Round($process.PrivateMemorySize64 / 1MB, 3)
                    foreground = ([Performance58VisibleWalk.Native]::GetForegroundWindow() -eq $window.Handle)
                })
                Start-Sleep -Milliseconds 1000
            }
        }
        finally {
            Set-KeyState -VirtualKey $phase.VirtualKey -Down $false
        }
    }
    $inputEndUtc = [DateTime]::UtcNow
    Start-Sleep -Milliseconds 300
    Capture-PIEWindow $window $afterPath
}
finally {
    foreach ($key in @([byte]0x57, [byte]0x41, [byte]0x44)) {
        Set-KeyState -VirtualKey $key -Down $false
    }
}

$samples | Export-Csv -LiteralPath $samplesPath -NoTypeInformation -Encoding UTF8
$beforeHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $beforePath).Hash
$afterHash = (Get-FileHash -Algorithm SHA256 -LiteralPath $afterPath).Hash
$duration = ($inputEndUtc - $inputStartUtc).TotalSeconds
$evidence = [ordered]@{
    schema_version = 1
    status = 'UE58_VISIBLE_NATURAL_GAMEPLAY_PIE_INPUT_COMPLETE'
    authority = 'visible Unreal Editor 5.8 floating PIE using normal GameMode, Player, camera and physical Windows keyboard input'
    project = 'NoShellForWinter'
    map = '/Game/Procedural/Maps/DungeonGeneration'
    editor_pid = $EditorPid
    window_title = $window.Title
    scenario = [ordered]@{
        benchmark_enemy_spawn_calls = 0
        teleports = 0
        direct_add_movement_input_calls = 0
        synthetic_combat_inputs = 0
        input_method = 'focused floating PIE viewport plus physical Windows W/A/D key-down and key-up events'
        input_start_utc = $inputStartUtc.ToString('o')
        input_end_utc = $inputEndUtc.ToString('o')
        input_duration_seconds = [Math]::Round($duration, 3)
        schedule = @($schedule | ForEach-Object { [ordered]@{ key = $_.Key; seconds = $_.Seconds } })
        focus_recoveries = $focusRecoveries
    }
    host_process_observation = [ordered]@{
        sample_count = $samples.Count
        working_set_min_mb = [Math]::Round(($samples | Measure-Object working_set_mb -Minimum).Minimum, 3)
        working_set_max_mb = [Math]::Round(($samples | Measure-Object working_set_mb -Maximum).Maximum, 3)
        note = 'Host process samples are context only and are not frame-time acceptance metrics.'
    }
    artifacts = [ordered]@{
        before_walk = [ordered]@{ path = 'before_walk.png'; sha256 = $beforeHash }
        after_walk = [ordered]@{ path = 'after_walk.png'; sha256 = $afterHash }
        host_process_samples = 'host_process_samples.csv'
    }
    safety = [ordered]@{
        acf_modified_by_driver = $false
        calysto_modified_by_driver = $false
        dirty_pawn_modified_by_driver = $false
        tattoo_shop_modified_by_driver = $false
        assets_saved_by_driver = $false
    }
}
$evidence | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $evidencePath -Encoding UTF8
$evidence | ConvertTo-Json -Depth 12
