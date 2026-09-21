<#
.SYNOPSIS
    Captures physical-key evidence for the NoShellForWinter HUB input contract.

.DESCRIPTION
    This is a disposable, project-owned observer.  It never starts or stops the
    editor/PIE, invokes no Unreal console commands, and never saves assets or
    configuration.  An operator must already have a visible NoShellForWinter
    PIE session running on /Game/_Game/Hub/HUB.

    The script focuses the existing PIE window, uses Windows key-down/key-up
    events (rather than an Unreal input shortcut), captures a before/after
    image for each exercised input, and writes a JSON receipt under Saved\QA.
    Screenshot deltas prove only that the captured pixels changed; they do not
    prove the requested gameplay behavior.  Consequently every contract
    behavior remains PENDING until a suitable live runtime assertion or a
    human visual review promotes it.

    H, T chords, Plus, and Minus can change the current runtime session (for
    example status cycling or an eligible Intimacy route).  They are opt-in via
    -IncludeStatefulInteractions and remain PENDING even when dispatched.

.EXAMPLE
    # With HUB already in a floating PIE Preview window:
    powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\QA\Run-HubInputContractPIE58.ps1

.EXAMPLE
    # Explicitly exercise H, T+Y, T+Hyphen, T+Subtract, Plus, and Minus in a
    # disposable prepared session.  The script does not infer eligibility.
    powershell -NoProfile -ExecutionPolicy Bypass -File .\Tools\QA\Run-HubInputContractPIE58.ps1 -IncludeStatefulInteractions -HubMapConfirmed

.NOTES
    The default requires a floating PIE Preview because it is the only window
    shape this runner can focus and crop without accidentally sending keys to
    editor chrome.  -AllowEmbeddedPIE is an explicit lower-confidence fallback.
#>

[CmdletBinding()]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',

    [string]$OutputRoot = '',

    [ValidateRange(40, 2000)]
    [int]$HoldMilliseconds = 80,

    [ValidateRange(200, 10000)]
    [int]$SettleMilliseconds = 900,

    [string[]]$ScenarioIds = @(),

    [switch]$AllowEmbeddedPIE,

    [switch]$IncludeStatefulInteractions,

    [switch]$HubMapConfirmed
)

$ErrorActionPreference = 'Stop'

if (-not ('NoShellForWinterHubInputQA.Native' -as [type])) {
    Add-Type @'
using System;
using System.Text;
using System.Runtime.InteropServices;

namespace NoShellForWinterHubInputQA
{
    public delegate bool EnumWindowsProc(IntPtr hWnd, IntPtr lParam);

    [StructLayout(LayoutKind.Sequential)]
    public struct RECT
    {
        public int Left;
        public int Top;
        public int Right;
        public int Bottom;
    }

    public static class Native
    {
        [DllImport("user32.dll")] public static extern bool EnumWindows(EnumWindowsProc callback, IntPtr lParam);
        [DllImport("user32.dll")] public static extern bool IsWindowVisible(IntPtr hWnd);
        [DllImport("user32.dll")] public static extern int GetWindowText(IntPtr hWnd, StringBuilder text, int maxCount);
        [DllImport("user32.dll")] public static extern uint GetWindowThreadProcessId(IntPtr hWnd, out uint processId);
        [DllImport("user32.dll")] public static extern bool GetWindowRect(IntPtr hWnd, out RECT rect);
        [DllImport("user32.dll")] public static extern bool SetForegroundWindow(IntPtr hWnd);
        [DllImport("user32.dll")] public static extern IntPtr GetForegroundWindow();
        [DllImport("user32.dll")] public static extern bool BringWindowToTop(IntPtr hWnd);
        [DllImport("user32.dll")] public static extern bool ShowWindow(IntPtr hWnd, int command);
        [DllImport("user32.dll")] public static extern void SwitchToThisWindow(IntPtr hWnd, bool altTab);
        [DllImport("user32.dll")] public static extern bool SetCursorPos(int x, int y);
        [DllImport("user32.dll")] public static extern void mouse_event(uint flags, uint dx, uint dy, uint data, UIntPtr extraInfo);
        [DllImport("user32.dll")] public static extern void keybd_event(byte virtualKey, byte scanCode, uint flags, UIntPtr extraInfo);
    }
}
'@
}

Add-Type -AssemblyName System.Drawing

function Get-PieWindow {
    param([switch]$IncludeEmbedded)

    $windows = [System.Collections.Generic.List[object]]::new()
    $callback = [NoShellForWinterHubInputQA.EnumWindowsProc]{
        param([IntPtr]$Handle, [IntPtr]$Unused)

        if (-not [NoShellForWinterHubInputQA.Native]::IsWindowVisible($Handle)) {
            return $true
        }

        [uint32]$processId = 0
        [void][NoShellForWinterHubInputQA.Native]::GetWindowThreadProcessId($Handle, [ref]$processId)
        try {
            $process = [System.Diagnostics.Process]::GetProcessById([int]$processId)
        }
        catch {
            return $true
        }

        if ($process.ProcessName -ne 'UnrealEditor') {
            return $true
        }

        $titleBuilder = [System.Text.StringBuilder]::new(512)
        [void][NoShellForWinterHubInputQA.Native]::GetWindowText($Handle, $titleBuilder, $titleBuilder.Capacity)
        $title = $titleBuilder.ToString()
        $isPreview = $title -match 'NoShellForWinter Preview|Play In Editor'
        $isEmbeddedEditor = $title -match 'NoShellForWinter - Unreal Editor'
        if (-not $isPreview -and (-not $IncludeEmbedded -or -not $isEmbeddedEditor)) {
            return $true
        }

        $rect = [NoShellForWinterHubInputQA.RECT]::new()
        if (-not [NoShellForWinterHubInputQA.Native]::GetWindowRect($Handle, [ref]$rect)) {
            return $true
        }

        $windows.Add([pscustomobject]@{
            Handle = $Handle
            Title = $title
            ProcessId = [int]$processId
            Left = $rect.Left
            Top = $rect.Top
            Right = $rect.Right
            Bottom = $rect.Bottom
            Width = $rect.Right - $rect.Left
            Height = $rect.Bottom - $rect.Top
            IsPreview = $isPreview
        })
        return $true
    }

    [void][NoShellForWinterHubInputQA.Native]::EnumWindows($callback, [IntPtr]::Zero)
    $previews = @($windows | Where-Object { $_.IsPreview } | Sort-Object Width -Descending)
    if ($previews.Count -gt 0) {
        return $previews[0]
    }

    $embedded = @($windows | Sort-Object Width -Descending)
    if ($embedded.Count -gt 0) {
        return $embedded[0]
    }

    return $null
}

function Get-WindowGeometry {
    param([Parameter(Mandatory)]$Window)

    $rect = [NoShellForWinterHubInputQA.RECT]::new()
    if (-not [NoShellForWinterHubInputQA.Native]::GetWindowRect($Window.Handle, [ref]$rect)) {
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
        IsPreview = $Window.IsPreview
    }
}

function Assert-PieWindowAlive {
    param(
        [Parameter(Mandatory)]$ExpectedWindow,
        [Parameter(Mandatory)][string]$Stage
    )

    $current = Get-PieWindow -IncludeEmbedded:$AllowEmbeddedPIE
    if (-not $current -or $current.Handle -ne $ExpectedWindow.Handle) {
        throw "PIE window disappeared before '$Stage'."
    }
    if ($ExpectedWindow.IsPreview -and -not $current.IsPreview) {
        throw "Floating PIE Preview ended before '$Stage'; the editor fallback is not valid evidence."
    }

    return Get-WindowGeometry -Window $current
}

function Activate-PieWindow {
    param([Parameter(Mandatory)]$Window)

    [void][NoShellForWinterHubInputQA.Native]::ShowWindow($Window.Handle, 3)
    [NoShellForWinterHubInputQA.Native]::SwitchToThisWindow($Window.Handle, $true)
    # Windows permits SetForegroundWindow more reliably after a short Alt tap.
    [NoShellForWinterHubInputQA.Native]::keybd_event(0x12, 0, 0, [UIntPtr]::Zero)
    [NoShellForWinterHubInputQA.Native]::keybd_event(0x12, 0, 0x0002, [UIntPtr]::Zero)
    [void][NoShellForWinterHubInputQA.Native]::BringWindowToTop($Window.Handle)

    for ($attempt = 0; $attempt -lt 15; $attempt++) {
        [void][NoShellForWinterHubInputQA.Native]::SetForegroundWindow($Window.Handle)
        if ([NoShellForWinterHubInputQA.Native]::GetForegroundWindow() -eq $Window.Handle) {
            return
        }
        Start-Sleep -Milliseconds 100
    }

    throw "Could not bring the PIE window to the foreground: $($Window.Title)"
}

function Focus-PieViewport {
    param([Parameter(Mandatory)]$Window)

    Activate-PieWindow -Window $Window
    if ($Window.IsPreview) {
        return
    }

    # Embedded PIE requires a click into the viewport.  This fallback is opt-in
    # because its crop includes editor chrome and therefore has lower evidence quality.
    $geometry = Get-WindowGeometry -Window $Window
    $x = [int]($geometry.Left + ($geometry.Width * 0.34))
    $y = [int]($geometry.Top + ($geometry.Height * 0.48))
    [void][NoShellForWinterHubInputQA.Native]::SetCursorPos($x, $y)
    Start-Sleep -Milliseconds 120
    [NoShellForWinterHubInputQA.Native]::mouse_event(0x0002, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 60
    [NoShellForWinterHubInputQA.Native]::mouse_event(0x0004, 0, 0, 0, [UIntPtr]::Zero)
    Start-Sleep -Milliseconds 500

    if ([NoShellForWinterHubInputQA.Native]::GetForegroundWindow() -ne $Window.Handle) {
        throw 'Embedded PIE viewport did not retain foreground focus after the focus click.'
    }
}

function Invoke-PhysicalKeyChord {
    param(
        [Parameter(Mandatory)]$Window,
        [byte[]]$ModifierVirtualKeys = @(),
        [Parameter(Mandatory)][byte]$VirtualKey,
        [Parameter(Mandatory)][string]$Label,
        [Parameter(Mandatory)][int]$HoldMs,
        [Parameter(Mandatory)][int]$SettleMs
    )

    Assert-PieWindowAlive -ExpectedWindow $Window -Stage "$Label pre-dispatch" | Out-Null
    Activate-PieWindow -Window $Window
    if ([NoShellForWinterHubInputQA.Native]::GetForegroundWindow() -ne $Window.Handle) {
        throw "Refusing to dispatch '$Label' because the PIE window is not foreground."
    }

    $downModifiers = [System.Collections.Generic.List[string]]::new()
    try {
        foreach ($modifier in $ModifierVirtualKeys) {
            [NoShellForWinterHubInputQA.Native]::keybd_event($modifier, 0, 0, [UIntPtr]::Zero)
            $downModifiers.Add(('0x{0:X2}' -f $modifier))
        }
        if ($ModifierVirtualKeys.Count -gt 0) {
            Start-Sleep -Milliseconds 40
        }

        [NoShellForWinterHubInputQA.Native]::keybd_event($VirtualKey, 0, 0, [UIntPtr]::Zero)
        Start-Sleep -Milliseconds $HoldMs
        [NoShellForWinterHubInputQA.Native]::keybd_event($VirtualKey, 0, 0x0002, [UIntPtr]::Zero)
    }
    finally {
        for ($index = $ModifierVirtualKeys.Count - 1; $index -ge 0; $index--) {
            [NoShellForWinterHubInputQA.Native]::keybd_event($ModifierVirtualKeys[$index], 0, 0x0002, [UIntPtr]::Zero)
        }
    }

    Start-Sleep -Milliseconds $SettleMs
    return [ordered]@{
        dispatch = 'DISPATCHED_TO_FOREGROUND_PIE'
        label = $Label
        virtual_key = ('0x{0:X2}' -f $VirtualKey)
        modifiers = @($downModifiers)
        hold_ms = $HoldMs
        settle_ms = $SettleMs
        foreground_after_dispatch = ([NoShellForWinterHubInputQA.Native]::GetForegroundWindow() -eq $Window.Handle)
    }
}

function Capture-PieWindow {
    param(
        [Parameter(Mandatory)]$Window,
        [Parameter(Mandatory)][string]$FileName
    )

    Assert-PieWindowAlive -ExpectedWindow $Window -Stage "capture $FileName" | Out-Null
    Activate-PieWindow -Window $Window
    Start-Sleep -Milliseconds 150
    $geometry = Get-WindowGeometry -Window $Window
    if ($geometry.Width -lt 320 -or $geometry.Height -lt 180) {
        throw "PIE capture region is too small: $($geometry.Width)x$($geometry.Height)."
    }

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

    $capture = [ordered]@{
        path = $path
        sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $path).Hash
        width = $geometry.Width
        height = $geometry.Height
        utc = [DateTime]::UtcNow.ToString('o')
    }
    $report.screenshots.Add($capture)
    return $capture
}

function Measure-VisualDelta {
    param(
        [Parameter(Mandatory)][string]$BeforePath,
        [Parameter(Mandatory)][string]$AfterPath
    )

    $before = [System.Drawing.Bitmap]::FromFile($BeforePath)
    $after = [System.Drawing.Bitmap]::FromFile($AfterPath)
    try {
        if ($before.Width -ne $after.Width -or $before.Height -ne $after.Height) {
            return [ordered]@{
                status = 'PENDING_CAPTURE_DIMENSIONS_DIFFER'
                note = 'A dimension mismatch is evidence of a capture change, not proof of gameplay behavior.'
            }
        }

        $sampleStride = 12
        [double]$absoluteChannelDelta = 0.0
        [int64]$samples = 0
        [int64]$changedSamples = 0
        for ($y = 0; $y -lt $before.Height; $y += $sampleStride) {
            for ($x = 0; $x -lt $before.Width; $x += $sampleStride) {
                $beforePixel = $before.GetPixel($x, $y)
                $afterPixel = $after.GetPixel($x, $y)
                $delta = [Math]::Abs($beforePixel.R - $afterPixel.R) +
                    [Math]::Abs($beforePixel.G - $afterPixel.G) +
                    [Math]::Abs($beforePixel.B - $afterPixel.B)
                $absoluteChannelDelta += $delta
                if ($delta -ge 24) {
                    $changedSamples++
                }
                $samples++
            }
        }

        $meanAbsoluteChannelDelta = if ($samples -gt 0) {
            [Math]::Round($absoluteChannelDelta / ($samples * 3.0), 4)
        }
        else {
            0.0
        }
        $changedFraction = if ($samples -gt 0) {
            [Math]::Round($changedSamples / [double]$samples, 6)
        }
        else {
            0.0
        }

        return [ordered]@{
            status = if ($changedSamples -gt 0) { 'VISUAL_DELTA_CAPTURED_PENDING_REVIEW' } else { 'NO_VISUAL_DELTA_CAPTURED_PENDING_REVIEW' }
            sample_stride_pixels = $sampleStride
            sample_count = $samples
            changed_sample_count = $changedSamples
            changed_sample_fraction = $changedFraction
            mean_absolute_channel_delta = $meanAbsoluteChannelDelta
            note = 'Pixel deltas can be caused by world animation, exposure, cursor, or UI. They are never a functional PASS.'
        }
    }
    finally {
        $after.Dispose()
        $before.Dispose()
    }
}

function Get-KeyName {
    param([byte]$VirtualKey)

    switch ($VirtualKey) {
        0x4F { return 'O' }
        0xBE { return 'Period' }
        0x4C { return 'L' }
        0xBC { return 'Comma' }
        0x4E { return 'N' }
        0x43 { return 'C' }
        0x59 { return 'Y' }
        0x4A { return 'J' }
        0x48 { return 'H' }
        0x54 { return 'T' }
        0xBD { return 'Hyphen/Minus' }
        0x6D { return 'Numpad Subtract' }
        0xBB { return 'OEM Plus/Equals' }
        0xA0 { return 'Left Shift' }
        default { return ('0x{0:X2}' -f $VirtualKey) }
    }
}

function Get-KeyDescription {
    param([byte[]]$Modifiers, [byte]$VirtualKey)

    $parts = [System.Collections.Generic.List[string]]::new()
    foreach ($modifier in $Modifiers) {
        $parts.Add((Get-KeyName -VirtualKey $modifier))
    }
    $parts.Add((Get-KeyName -VirtualKey $VirtualKey))
    return $parts -join ' + '
}

function Invoke-ContractScenario {
    param(
        [Parameter(Mandatory)]$Window,
        [Parameter(Mandatory)]$Scenario,
        [Parameter(Mandatory)][int]$SequenceNumber
    )

    $record = [ordered]@{
        id = $Scenario.id
        expected_behavior = $Scenario.expected_behavior
        source_binding = $Scenario.source_binding
        physical_input = (Get-KeyDescription -Modifiers $Scenario.modifiers -VirtualKey $Scenario.virtual_key)
        virtual_key = ('0x{0:X2}' -f [byte]$Scenario.virtual_key)
        modifier_virtual_keys = @($Scenario.modifiers | ForEach-Object { '0x{0:X2}' -f [byte]$_ })
        stateful = [bool]$Scenario.stateful
        reversible_toggle = [bool]$Scenario.reversible_toggle
        before_capture = $null
        dispatch = $null
        after_capture = $null
        visual_delta = $null
        restoration_dispatch = $null
        restoration_capture = $null
        status = 'PENDING_NOT_RUN'
        rationale = $Scenario.rationale
    }

    if ($Scenario.stateful -and -not $IncludeStatefulInteractions) {
        $record.status = 'PENDING_SKIPPED_STATEFUL_BY_DEFAULT'
        $record.rationale = "$($Scenario.rationale) Use -IncludeStatefulInteractions only with a disposable prepared runtime session."
        return $record
    }

    $prefix = '{0:D2}_{1}' -f $SequenceNumber, $Scenario.id
    try {
        $record.before_capture = Capture-PieWindow -Window $Window -FileName ("${prefix}_Before.png")
        $record.dispatch = Invoke-PhysicalKeyChord `
            -Window $Window `
            -ModifierVirtualKeys $Scenario.modifiers `
            -VirtualKey ([byte]$Scenario.virtual_key) `
            -Label $record.physical_input `
            -HoldMs $HoldMilliseconds `
            -SettleMs $SettleMilliseconds
        $record.after_capture = Capture-PieWindow -Window $Window -FileName ("${prefix}_After.png")
        $record.visual_delta = Measure-VisualDelta `
            -BeforePath $record.before_capture.path `
            -AfterPath $record.after_capture.path

        if ($Scenario.reversible_toggle) {
            $record.restoration_dispatch = Invoke-PhysicalKeyChord `
                -Window $Window `
                -ModifierVirtualKeys $Scenario.modifiers `
                -VirtualKey ([byte]$Scenario.virtual_key) `
                -Label "$($record.physical_input) restoration attempt" `
                -HoldMs $HoldMilliseconds `
                -SettleMs $SettleMilliseconds
            $record.restoration_capture = Capture-PieWindow -Window $Window -FileName ("${prefix}_RestorationAttempt.png")
        }

        $record.status = 'PENDING_VISUAL_AND_RUNTIME_REVIEW'
        return $record
    }
    catch {
        $record.status = 'PENDING_DISPATCH_OR_CAPTURE_ERROR'
        $record.error = $_.Exception.Message
        return $record
    }
}

$root = (Resolve-Path -LiteralPath $ProjectRoot -ErrorAction Stop).Path
$projectFile = Join-Path $root 'NoShellForWinter.uproject'
if (-not (Test-Path -LiteralPath $projectFile -PathType Leaf)) {
    throw "Expected target project file is missing: $projectFile"
}

if ([string]::IsNullOrWhiteSpace($OutputRoot)) {
    $OutputRoot = Join-Path $root 'Saved\QA\HubInputContract'
}

$runId = [DateTime]::UtcNow.ToString('yyyyMMdd_HHmmss')
$runDirectory = Join-Path $OutputRoot $runId
New-Item -ItemType Directory -Force -Path $runDirectory | Out-Null
$reportPath = Join-Path $runDirectory 'HubInputContractPIE58.json'

$report = [ordered]@{
    schema_version = 1
    test = 'NoShellForWinter HUB physical input contract QA'
    started_utc = [DateTime]::UtcNow.ToString('o')
    project_root = $root
    expected_map = '/Game/_Game/Hub/HUB'
    hub_map_confirmation = if ($HubMapConfirmed) { 'OPERATOR_ASSERTED_NOT_MACHINE_VERIFIED' } else { 'PENDING_OPERATOR_CONFIRMATION_REQUIRED' }
    runner_constraints = [ordered]@{
        launches_editor_or_pie = $false
        stops_editor_or_pie = $false
        saves_assets_or_config = $false
        unreal_console_commands = $false
        dispatch_method = 'Windows user32 keybd_event key-down/key-up after foreground-focus verification'
        evidence_policy = 'Every gameplay behavior remains PENDING. Screenshots and pixel deltas are evidence only, never a functional PASS.'
        stateful_interactions_included = [bool]$IncludeStatefulInteractions
        embedded_pie_allowed = [bool]$AllowEmbeddedPIE
    }
    window = $null
    screenshots = [System.Collections.Generic.List[object]]::new()
    scenarios = [System.Collections.Generic.List[object]]::new()
    errors = [System.Collections.Generic.List[string]]::new()
    result = 'PENDING_NOT_STARTED'
}

$scenarioPlan = @(
    [ordered]@{
        id = 'O_FreeCamera'
        expected_behavior = 'Toggle gameplay free camera.'
        source_binding = 'Plugins/EFProjectSystems/Source/EFProjectSystemsCore/EFProjectInputSettings.cpp:11; ProjectGameplayFreeCameraSubsystem.cpp:309.'
        modifiers = @()
        virtual_key = [byte]0x4F
        stateful = $false
        reversible_toggle = $true
        rationale = 'A screenshot does not expose the active view target or free-camera input ownership.'
    },
    [ordered]@{
        id = 'Period_CharacterCreator'
        expected_behavior = 'Toggle character creator where its map/UI route allows it.'
        source_binding = 'Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/CharacterBackground/UI/ProjectCharacterBackgroundCreationWidget.cpp:1113.'
        modifiers = @()
        virtual_key = [byte]0xBE
        stateful = $false
        reversible_toggle = $true
        rationale = 'HUB eligibility and UI focus cannot be inferred from window pixels alone.'
    },
    [ordered]@{
        id = 'L_GameplayDebug'
        expected_behavior = 'Toggle the non-Shipping gameplay debug menu.'
        source_binding = 'Plugins/EFProjectSystems/Source/EFProjectSystemsCore/EFProjectInputSettings.cpp:10; ProjectGameplayDebugSubsystem.cpp:383.'
        modifiers = @()
        virtual_key = [byte]0x4C
        stateful = $false
        reversible_toggle = $true
        rationale = 'A capture can show a UI change but cannot establish menu ownership, focus, or Shipping behavior.'
    },
    [ordered]@{
        id = 'Comma_NeedsStatus'
        expected_behavior = 'Toggle the full Needs & Status HUD.'
        source_binding = 'Plugins/EFProjectSystems/Source/EFProjectSystemsCore/EFProjectInputSettings.cpp:8; ProjectSurvivalNeedsSubsystem.cpp:370.'
        modifiers = @()
        virtual_key = [byte]0xBC
        stateful = $false
        reversible_toggle = $true
        rationale = 'Pixel evidence cannot prove the full widget tree, status values, or input consumption.'
    },
    [ordered]@{
        id = 'N_CustomWalk'
        expected_behavior = 'Toggle custom walk.'
        source_binding = 'Plugins/EFProjectSystems/Source/EFProjectSystemsCore/EFProjectInputSettings.cpp:5; ProjectLocomotionOverrideSubsystem.cpp:130.'
        modifiers = @()
        virtual_key = [byte]0x4E
        stateful = $false
        reversible_toggle = $true
        rationale = 'A static capture cannot prove gait state, movement speed, or animation playback.'
    },
    [ordered]@{
        id = 'C_Crawl'
        expected_behavior = 'Toggle crawl.'
        source_binding = 'Plugins/EFProjectSystems/Source/EFProjectSystemsCore/EFProjectInputSettings.cpp:6; ProjectLocomotionOverrideSubsystem.cpp:131.'
        modifiers = @()
        virtual_key = [byte]0x43
        stateful = $false
        reversible_toggle = $true
        rationale = 'A static capture cannot prove crawl transition, collision, or locomotion restoration.'
    },
    [ordered]@{
        id = 'Y_ActionsEmotes'
        expected_behavior = 'Toggle Actions, emotes, and interactions.'
        source_binding = 'Plugins/EFProjectSystems/Source/EFProjectSystemsCore/EFProjectInputSettings.cpp:7; ProjectEmoteSubsystem.cpp:560.'
        modifiers = @()
        virtual_key = [byte]0x59
        stateful = $false
        reversible_toggle = $true
        rationale = 'A screenshot cannot prove selected action, focus, input capture, or a safe cancel path.'
    },
    [ordered]@{
        id = 'J_Chronicle'
        expected_behavior = 'Expand/collapse the Chronicle.'
        source_binding = 'Plugins/EFProjectSystems/Source/EFProjectSystemsCore/EFProjectInputSettings.cpp:9; ProjectActivityFeedSubsystem.cpp:1109.'
        modifiers = @()
        virtual_key = [byte]0x4A
        stateful = $false
        reversible_toggle = $true
        rationale = 'Pixel change cannot prove Chronicle data binding or expand/collapse state.'
    },
    [ordered]@{
        id = 'H_ConditionalStatusDebug'
        expected_behavior = 'Cycle debug status only when debug cycling is enabled.'
        source_binding = 'Plugins/EFProjectSystems/Source/EFProjectSystemsGameplay/Survival/ProjectSurvivalNeedsSubsystem.cpp:371-373.'
        modifiers = @()
        virtual_key = [byte]0x48
        stateful = $true
        reversible_toggle = $false
        rationale = 'This can change live status state and the conditional setting is not observable from the physical window.'
    },
    [ordered]@{
        id = 'TPlusY_PartnerRoute'
        expected_behavior = 'With T held, route Y through the eligible Partner/Intimacy menu path.'
        source_binding = 'Docs/Migration/05_Config_Merge.md input contract; ProjectIntimacySubsystem input routing.'
        modifiers = @([byte]0x54)
        virtual_key = [byte]0x59
        stateful = $true
        reversible_toggle = $false
        rationale = 'Charisma, target allowlist, location, Streamer Safe, and menu contents require runtime inspection; this runner only records the physical chord.'
    },
    [ordered]@{
        id = 'TPlusHyphen_ImmediateIntimacy'
        expected_behavior = 'With T held, request the Hyphen immediate Intimacy route when eligible.'
        source_binding = 'Docs/Migration/05_Config_Merge.md; ProjectIntimacySettings.cpp:7; ProjectIntimacySubsystem.cpp:1758.'
        modifiers = @([byte]0x54)
        virtual_key = [byte]0xBD
        stateful = $true
        reversible_toggle = $false
        rationale = 'This can start a session. Eligibility and cancellation are intentionally not asserted by screenshot analysis.'
    },
    [ordered]@{
        id = 'TPlusNumpadSubtract_ImmediateIntimacy'
        expected_behavior = 'With T held, request the Numpad Subtract immediate Intimacy route when eligible.'
        source_binding = 'Docs/Migration/05_Config_Merge.md; ProjectIntimacySettings.cpp:8; ProjectIntimacySubsystem.cpp:1759.'
        modifiers = @([byte]0x54)
        virtual_key = [byte]0x6D
        stateful = $true
        reversible_toggle = $false
        rationale = 'This is a separate physical keyboard variant and can start a session; it remains PENDING without an inspected runtime state.'
    },
    [ordered]@{
        id = 'Plus_SourceInteraction'
        expected_behavior = 'Preserve the exact source Plus interaction.'
        source_binding = 'Docs/Migration/05_Config_Merge.md input contract; Docs/Migration/07_Input_Contract.md.'
        modifiers = @([byte]0xA0)
        virtual_key = [byte]0xBB
        stateful = $true
        reversible_toggle = $false
        rationale = 'Sends Shift+VK_OEM_PLUS. The historic Equals console-key collision and the target behavior remain PENDING.'
    },
    [ordered]@{
        id = 'Minus_SourceInteraction'
        expected_behavior = 'Preserve the exact source Minus/Hyphen interaction where eligible.'
        source_binding = 'Docs/Migration/07_Input_Contract.md; ProjectIntimacySettings.cpp:7.'
        modifiers = @()
        virtual_key = [byte]0xBD
        stateful = $true
        reversible_toggle = $false
        rationale = 'Sends top-row VK_OEM_MINUS (Hyphen). It is separate from the Numpad Subtract chord and remains PENDING.'
    }
)

if ($ScenarioIds.Count -gt 0) {
    $knownScenarioIds = @($scenarioPlan | ForEach-Object { [string]$_.id })
    $unknownScenarioIds = @($ScenarioIds | Where-Object { $_ -notin $knownScenarioIds })
    if ($unknownScenarioIds.Count -gt 0) {
        throw "Unknown ScenarioIds: $($unknownScenarioIds -join ', '). Available values: $($knownScenarioIds -join ', ')."
    }

    $filteredPlan = [System.Collections.Generic.List[object]]::new()
    foreach ($scenarioId in $ScenarioIds) {
        $filteredPlan.Add(($scenarioPlan | Where-Object { $_.id -eq $scenarioId } | Select-Object -First 1))
    }
    $scenarioPlan = @($filteredPlan)
}

$fatalError = $null
try {
    $window = Get-PieWindow -IncludeEmbedded:$AllowEmbeddedPIE
    if (-not $window) {
        $fallback = if ($AllowEmbeddedPIE) { 'floating or embedded' } else { 'floating' }
        throw "No active $fallback NoShellForWinter PIE window was found. Start HUB manually, then rerun this observer."
    }
    if (-not $window.IsPreview -and -not $AllowEmbeddedPIE) {
        throw 'A floating NoShellForWinter PIE Preview is required. Use -AllowEmbeddedPIE only when its lower-confidence focus/crop is acceptable.'
    }

    Focus-PieViewport -Window $window
    $window = Get-WindowGeometry -Window $window
    $report.window = [ordered]@{
        title = $window.Title
        process_id = $window.ProcessId
        handle = ('0x{0:X}' -f $window.Handle.ToInt64())
        is_floating_preview = $window.IsPreview
        left = $window.Left
        top = $window.Top
        width = $window.Width
        height = $window.Height
    }

    $baseline = Capture-PieWindow -Window $window -FileName '00_Baseline.png'
    $report.baseline_capture = $baseline

    $sequence = 1
    foreach ($scenario in $scenarioPlan) {
        $scenarioRecord = Invoke-ContractScenario -Window $window -Scenario $scenario -SequenceNumber $sequence
        $report.scenarios.Add($scenarioRecord)
        if ($scenarioRecord.status -eq 'PENDING_DISPATCH_OR_CAPTURE_ERROR') {
            $report.errors.Add("$($scenarioRecord.id): $($scenarioRecord.error)")
        }
        $sequence++
    }

    $report.result = 'PENDING_MANUAL_RUNTIME_REVIEW'
}
catch {
    $fatalError = $_
    $report.errors.Add($_.Exception.Message)
    $report.result = 'BLOCKED_NO_RELIABLE_PIE_EVIDENCE'
}
finally {
    $report.finished_utc = [DateTime]::UtcNow.ToString('o')
    $report | ConvertTo-Json -Depth 16 | Set-Content -LiteralPath $reportPath -Encoding utf8
}

[pscustomobject]@{
    Result = $report.result
    Report = $reportPath
    Screenshots = $report.screenshots.Count
    Scenarios = $report.scenarios.Count
    StatefulInteractionsIncluded = [bool]$IncludeStatefulInteractions
    HubMapConfirmation = $report.hub_map_confirmation
} | Format-List

if ($fatalError) {
    throw $fatalError
}
