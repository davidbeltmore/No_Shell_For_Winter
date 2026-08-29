param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$EngineRoot = "D:\Unreal Engine 5\Library\UE_5.8",
    [string]$AdditionalArguments = "",
    [string]$PythonScript = "",
    [ValidateRange(30, 300)]
    [int]$PythonTimeoutSeconds = 300,
    [switch]$Wait
)

$ErrorActionPreference = "Stop"
$projectPath = Join-Path $ProjectRoot "NoShellForWinter.uproject"
$editorExe = Join-Path $EngineRoot "Engine\Binaries\Win64\UnrealEditor.exe"
$receiptGuard = Join-Path $ProjectRoot "Tools\Migration\Repair-DazPluginReceipt58.ps1"

foreach ($requiredPath in @($projectPath, $editorExe, $receiptGuard)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required path not found: $requiredPath"
    }
}

& powershell -NoProfile -ExecutionPolicy Bypass -File $receiptGuard -ProjectRoot $ProjectRoot
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt guard failed with exit code $LASTEXITCODE"
}

if (-not [string]::IsNullOrWhiteSpace($PythonScript)) {
    $resolvedProjectRoot = [System.IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\') + '\'
    $resolvedPythonScript = [System.IO.Path]::GetFullPath($PythonScript)
    if (-not $resolvedPythonScript.StartsWith(
            $resolvedProjectRoot,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "Python automation must remain inside the target project: $resolvedPythonScript"
    }
    if (!(Test-Path -LiteralPath $resolvedPythonScript -PathType Leaf)) {
        throw "Python automation script not found: $resolvedPythonScript"
    }
    $existingEditors = @(Get-Process UnrealEditor -ErrorAction SilentlyContinue)
    if ($existingEditors.Count -ne 0) {
        throw "Python automation requires zero pre-existing UnrealEditor processes."
    }

    $launchStarted = Get-Date
    & $editorExe `
        $projectPath `
        "-EnablePlugin=DazToUnreal" `
        "-EnablePlugin=EFCharacterCreationDazBridge" `
        "-run=pythonscript" `
        "-script=$resolvedPythonScript" `
        "-unattended" `
        "-nop4" `
        "-nosplash" `
        "-NullRHI" `
        "-stdout" `
        "-FullStdOutLogOutput"

    $startDeadline = (Get-Date).AddSeconds(15)
    $process = $null
    do {
        Start-Sleep -Milliseconds 250
        $process = Get-Process UnrealEditor -ErrorAction SilentlyContinue |
            Where-Object { $_.StartTime -ge $launchStarted.AddSeconds(-1) } |
            Sort-Object StartTime -Descending |
            Select-Object -First 1
    } while (-not $process -and (Get-Date) -lt $startDeadline)
    if (-not $process) {
        throw "NoShellForWinter Python automation did not start an UnrealEditor process."
    }

    $exitDeadline = (Get-Date).AddSeconds($PythonTimeoutSeconds)
    while (-not $process.HasExited -and (Get-Date) -lt $exitDeadline) {
        Start-Sleep -Milliseconds 500
        $process.Refresh()
    }
    if (-not $process.HasExited) {
        Stop-Process -Id $process.Id -Force
        throw "NoShellForWinter Python automation exceeded $PythonTimeoutSeconds seconds."
    }
    # PowerShell can lose ExitCode for a GUI process discovered through
    # Get-Process after direct invocation.  A concrete non-zero value is an
    # error; the automation receipt remains the fail-closed authority when the
    # GUI API reports no code.
    if ($null -ne $process.ExitCode -and $process.ExitCode -ne 0) {
        throw "NoShellForWinter Python automation exited with code $($process.ExitCode)."
    }
    Write-Host "NoShellForWinter Python automation completed with Daz plugins enabled."
    return
}

$editorArguments = '"' + $projectPath + '" -EnablePlugin=DazToUnreal -EnablePlugin=EFCharacterCreationDazBridge'
if (-not [string]::IsNullOrWhiteSpace($AdditionalArguments)) {
    $editorArguments += ' ' + $AdditionalArguments
}
$process = Start-Process `
    -FilePath $editorExe `
    -ArgumentList $editorArguments `
    -WorkingDirectory $ProjectRoot `
    -PassThru
if ($Wait) {
    $process.WaitForExit()
    if ($process.ExitCode -ne 0) {
        throw "NoShellForWinter editor exited with code $($process.ExitCode)."
    }
}
Write-Host "NoShellForWinter editor launch requested with Daz plugins enabled."
