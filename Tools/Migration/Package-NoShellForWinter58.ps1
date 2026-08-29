param(
    [string]$ProjectRoot = "D:\Projects UE5\NoShellForWinter",
    [string]$EngineRoot = "D:\Unreal Engine 5\Library\UE_5.8",
    [ValidateSet("Development", "Shipping")]
    [string]$Configuration = "Development",
    [string]$ArchiveRoot
)

$ErrorActionPreference = "Stop"
$projectPath = Join-Path $ProjectRoot "NoShellForWinter.uproject"
$runUAT = Join-Path $EngineRoot "Engine\Build\BatchFiles\RunUAT.bat"
$receiptGuard = Join-Path $ProjectRoot "Tools\Migration\Repair-DazPluginReceipt58.ps1"
$clothingCatalogCompiler = Join-Path $ProjectRoot `
    "Tools\ClothingMorphV2\Compile-EFClothingGarmentCatalog58.ps1"
$clothingCatalogCompilerPython = Join-Path $ProjectRoot `
    "Tools\ClothingMorphV2\Compile-EFClothingGarmentCatalog58.py"
$gameTarget = Join-Path $ProjectRoot "Source\NoShellForWinter.Target.cs"
$packagedSmokeHeader = Join-Path $ProjectRoot `
    "Plugins\EFProcedural\Source\EFProceduralACFURuntime\Public\Calysto\EFCalystoPackagedSmokeSubsystem.h"
$packagedSmokeSource = Join-Path $ProjectRoot `
    "Plugins\EFProcedural\Source\EFProceduralACFURuntime\Private\Calysto\EFCalystoPackagedSmokeSubsystem.cpp"
$pcgSubsystemHeader = Join-Path $ProjectRoot `
    "Plugins\EFProcedural\Source\EFProceduralPCGRuntime\Public\EFProceduralPCGSubsystem.h"
$pcgSubsystemSource = Join-Path $ProjectRoot `
    "Plugins\EFProcedural\Source\EFProceduralPCGRuntime\Private\EFProceduralPCGSubsystem.cpp"
$packagedSmokeRunner = Join-Path $ProjectRoot `
    "Tools\Migration\Run-CalystoDungeonDirectorV4PackagedSmoke58.ps1"
$telemetrySchemaVersion = 1

foreach ($requiredPath in @(
        $projectPath,
        $runUAT,
        $receiptGuard,
        $clothingCatalogCompiler,
        $clothingCatalogCompilerPython,
        $gameTarget,
        $packagedSmokeHeader,
        $packagedSmokeSource,
        $pcgSubsystemHeader,
        $pcgSubsystemSource,
        $packagedSmokeRunner)) {
    if (!(Test-Path -LiteralPath $requiredPath)) {
        throw "Required packaging path not found: $requiredPath"
    }
}

$clothingToolsRoot = [System.IO.Path]::GetFullPath(
    (Join-Path $ProjectRoot "Tools\ClothingMorphV2"))
$clothingToolsPrefix = $clothingToolsRoot.TrimEnd('\') + '\'
foreach ($compilerPath in @($clothingCatalogCompiler, $clothingCatalogCompilerPython)) {
    $compilerFullPath = [System.IO.Path]::GetFullPath($compilerPath)
    if (-not $compilerFullPath.StartsWith(
            $clothingToolsPrefix,
            [System.StringComparison]::OrdinalIgnoreCase)) {
        throw "EF Clothing catalog compiler escapes the project-owned tools directory: $compilerFullPath"
    }
    if (-not (Test-Path -LiteralPath $compilerFullPath -PathType Leaf)) {
        throw "Required EF Clothing catalog compiler file is missing: $compilerFullPath"
    }
}

if (@(Get-Process UnrealEditor -ErrorAction SilentlyContinue).Count -ne 0) {
    throw "Close Unreal Editor before a fresh cook/package."
}

$projectDescriptor = Get-Content -LiteralPath $projectPath -Raw | ConvertFrom-Json
foreach ($pluginName in @("DazToUnreal", "EFCharacterCreationDazBridge")) {
    $plugin = $projectDescriptor.Plugins | Where-Object { $_.Name -eq $pluginName }
    if ($null -eq $plugin -or $plugin.Enabled -ne $true) {
        throw "$pluginName must remain explicitly enabled during cook/package."
    }
}

$gameTargetSource = Get-Content -LiteralPath $gameTarget -Raw
if ($gameTargetSource -cmatch 'bUseLoggingInShipping\s*=\s*true\s*;') {
    throw (@(
        "bUseLoggingInShipping=true is incompatible with this installed Engine build."
        "Shipping evidence must use the project-owned Calysto V4 telemetry file."
    ) -join " ")
}

$packagedSmokeHeaderSource = Get-Content -LiteralPath $packagedSmokeHeader -Raw
$packagedSmokeCppSource = Get-Content -LiteralPath $packagedSmokeSource -Raw
$pcgSubsystemCppSource = Get-Content -LiteralPath $pcgSubsystemSource -Raw
$packagedSmokeRunnerSource = Get-Content -LiteralPath $packagedSmokeRunner -Raw
$telemetrySchemaPattern = 'static\s+constexpr\s+int32\s+ProjectTelemetrySchemaVersion\s*=\s*' +
    [regex]::Escape([string]$telemetrySchemaVersion) + '\s*;'
if ($packagedSmokeHeaderSource -cnotmatch $telemetrySchemaPattern) {
    throw "Calysto V4 packaged smoke header does not declare project telemetry schema $telemetrySchemaVersion."
}
foreach ($sourceContract in @(
        [pscustomobject]@{
            Path = $packagedSmokeCppSource
            Tokens = @("PackagedSmokeTelemetry_", "CALYSTO_V4_PROJECT_TELEMETRY")
            Description = "runtime telemetry writer"
        },
        [pscustomobject]@{
            Path = $packagedSmokeRunnerSource
            Tokens = @("PackagedSmokeTelemetry_", "CALYSTO_V4_PROJECT_TELEMETRY", "Runtime.log")
            Description = "packaged smoke telemetry validator"
        },
        [pscustomobject]@{
            Path = $pcgSubsystemCppSource
            Tokens = @("RecordReadinessMilestone", "RecordRuntimeReadinessTrace", "DoorEnabled")
            Description = "real PCG readiness trace"
        })) {
    foreach ($token in $sourceContract.Tokens) {
        if ($sourceContract.Path.IndexOf($token, [StringComparison]::Ordinal) -lt 0) {
            throw "Calysto V4 $($sourceContract.Description) is missing required contract token: $token"
        }
    }
}
Write-Host (@(
    "Calysto V4 packaged telemetry contract: PASS"
    "schema=$telemetrySchemaVersion"
    "authority=project-owned-explicit-file"
) -join " ")

$gameBinaryName = if ($Configuration -eq "Shipping") {
    "NoShellForWinter-Win64-Shipping.exe"
} else {
    "NoShellForWinter.exe"
}
$gameReceiptName = if ($Configuration -eq "Shipping") {
    "NoShellForWinter-Win64-Shipping.target"
} else {
    "NoShellForWinter.target"
}
$gameBinary = Join-Path $ProjectRoot "Binaries\Win64\$gameBinaryName"
$gameReceipt = Join-Path $ProjectRoot "Binaries\Win64\$gameReceiptName"
foreach ($buildOutput in @($gameBinary, $gameReceipt)) {
    if (!(Test-Path -LiteralPath $buildOutput -PathType Leaf)) {
        throw "Fresh $Configuration Game build output is missing: $buildOutput"
    }
    if ((Get-Item -LiteralPath $buildOutput).Length -le 0) {
        throw "Fresh $Configuration Game build output is empty: $buildOutput"
    }
}
$latestRequiredBuildInputUtc = @(
    $gameTarget,
    $packagedSmokeHeader,
    $packagedSmokeSource,
    $pcgSubsystemHeader,
    $pcgSubsystemSource
) | ForEach-Object { (Get-Item -LiteralPath $_).LastWriteTimeUtc } |
    Sort-Object -Descending |
    Select-Object -First 1
foreach ($buildOutput in @($gameBinary, $gameReceipt)) {
    if ((Get-Item -LiteralPath $buildOutput).LastWriteTimeUtc -lt $latestRequiredBuildInputUtc) {
        throw (@(
            "$Configuration Game output predates the explicit V4 telemetry implementation: $buildOutput."
            "Run Build-NoShellForWinterGame58.ps1 for $Configuration before packaging."
        ) -join " ")
    }
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
    -ProjectRoot $ProjectRoot `
    -TargetName NoShellForWinter `
    -Configuration $Configuration `
    -VerifyOnly
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt verification failed before $Configuration cook/package."
}

Write-Host "Refreshing the EF Clothing Morph V26 catalog before $Configuration cook/package..."
& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $clothingCatalogCompiler `
    -ProjectRoot $ProjectRoot `
    -EngineRoot $EngineRoot
$clothingCompilerExitCode = $LASTEXITCODE
if ($clothingCompilerExitCode -ne 0) {
    throw (@(
        "EF Clothing Morph V26 catalog compilation failed with exit code $clothingCompilerExitCode."
        "BuildCookRun was not started; fix the catalog/compiler error before packaging."
        "Compiler: $clothingCatalogCompiler"
    ) -join " ")
}
Write-Host "EF Clothing Morph V26 pre-package catalog refresh: PASS"

if ([string]::IsNullOrWhiteSpace($ArchiveRoot)) {
    $stamp = Get-Date -Format "yyyyMMdd_HHmmss"
    $ArchiveRoot = Join-Path $ProjectRoot `
        "Saved\Migration\CalystoDungeonDirectorV4\Packages\${Configuration}_$stamp"
}
$archiveFullPath = [System.IO.Path]::GetFullPath($ArchiveRoot)
if (Test-Path -LiteralPath $archiveFullPath) {
    throw "Fresh package archive already exists; choose a new path: $archiveFullPath"
}

& $runUAT BuildCookRun `
    "-project=$projectPath" `
    -noP4 `
    -platform=Win64 `
    "-clientconfig=$Configuration" `
    -target=NoShellForWinter `
    -nocompileeditor `
    -skipbuild `
    -cook `
    -stage `
    -pak `
    -iostore `
    -package `
    -archive `
    "-archivedirectory=$archiveFullPath" `
    -utf8output
$uatExitCode = $LASTEXITCODE
if ($uatExitCode -ne 0) {
    throw "NoShellForWinter $Configuration cook/package failed with exit code $uatExitCode."
}

& powershell.exe -NoProfile -ExecutionPolicy Bypass -File $receiptGuard `
    -ProjectRoot $ProjectRoot `
    -TargetName NoShellForWinter `
    -Configuration $Configuration `
    -VerifyOnly
if ($LASTEXITCODE -ne 0) {
    throw "Daz receipt verification failed after $Configuration cook/package."
}

$contractReceiptPath = Join-Path $archiveFullPath "CalystoV4PackagingContract.json"
$contractReceipt = [ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString("o")
    target = "NoShellForWinter"
    configuration = $Configuration
    telemetry_authority = "project-owned-explicit-file"
    telemetry_schema_version = $telemetrySchemaVersion
    runtime_artifact_pattern = `
        "Saved/CalystoDungeonDirectorV4/PackagedSmokeTelemetry_<Configuration>_<Scenario>_<RunTag>.log"
    runtime_artifact_header = "CALYSTO_V4_PROJECT_TELEMETRY schema=$telemetrySchemaVersion"
    engine_logging_override = $false
    daz_receipt_verified_before_and_after = $true
    source_evidence = [ordered]@{
        target_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $gameTarget).Hash
        subsystem_header_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $packagedSmokeHeader).Hash
        subsystem_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $packagedSmokeSource).Hash
        pcg_subsystem_header_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $pcgSubsystemHeader).Hash
        pcg_subsystem_source_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $pcgSubsystemSource).Hash
        runner_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $packagedSmokeRunner).Hash
        game_binary_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $gameBinary).Hash
        game_receipt_sha256 = (Get-FileHash -Algorithm SHA256 -LiteralPath $gameReceipt).Hash
    }
}
$contractJson = $contractReceipt | ConvertTo-Json -Depth 5
[IO.File]::WriteAllText(
    $contractReceiptPath,
    $contractJson + [Environment]::NewLine,
    [Text.UTF8Encoding]::new($false))

Write-Host "NoShellForWinter $Configuration fresh cook/package: PASS"
Write-Host "Calysto V4 packaging contract: $contractReceiptPath"
Write-Output $archiveFullPath
