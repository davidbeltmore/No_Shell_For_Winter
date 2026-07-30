[CmdletBinding(SupportsShouldProcess)]
param(
    [string]$ProjectRoot = 'D:\Projects UE5\NoShellForWinter',
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$HarnessRoot = 'D:\Projects UE5\NoShellForWinter\Saved\Migration\Phase4\BulkProjectContent\BULKREST_20260713_2015\Harness57',
    [string]$ManifestPath = 'D:\Projects UE5\NoShellForWinter\Docs\Migration\04_Content_Migration_Manifest.csv'
)

Set-StrictMode -Version Latest
$ErrorActionPreference = 'Stop'

# Load order is intentional. The first ten packages define the enum/struct
# types used by the five PrimaryDataAsset Blueprint classes at the end.
$supportPackages = @(
    '/Game/Calysto/Dungeon/Data/Enumerator/Enum_ObjectType',
    '/Game/Calysto/Dungeon/Data/Enumerator/Enum_Rotation',
    '/Game/Calysto/Dungeon/Data/Structure/ST_DungeonMaterial',
    '/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeon',
    '/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeonLight',
    '/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple',
    '/Game/Calysto/Shared/Data/Structure/ST_ObjectSimpleActor',
    '/Game/Calysto/Shared/Data/Structure/ST_ObjectWallDoor',
    '/Game/Calysto/Shared/Data/Structure/ST_Spawner',
    '/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes',
    '/Game/Calysto/Dungeon/Data/Structure/ST_RoomTheme',
    '/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon',
    '/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial',
    '/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme',
    '/Game/Calysto/Shared/Data/Structure/PDA_Spawner',
    '/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece',
    '/Game/Calysto/Dungeon/Data/Structure/ST_GrammarSetting',
    '/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting',
    '/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize',
    '/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint'
)

$projectRootPath = [IO.Path]::GetFullPath($ProjectRoot).TrimEnd('\')
$sourceRootPath = [IO.Path]::GetFullPath($SourceRoot).TrimEnd('\')
$harnessRootPath = [IO.Path]::GetFullPath($HarnessRoot).TrimEnd('\')
$manifestFullPath = [IO.Path]::GetFullPath($ManifestPath)
$allowedHarnessParent = Join-Path $projectRootPath 'Saved\Migration\Phase4\BulkProjectContent'
$targetContent = Join-Path $projectRootPath 'Content'
$harnessContent = Join-Path $harnessRootPath 'Content'

if (-not (Test-Path -LiteralPath (Join-Path $projectRootPath 'NoShellForWinter.uproject') -PathType Leaf)) {
    throw "ProjectRoot is not NoShellForWinter: $projectRootPath"
}
if (-not (Test-Path -LiteralPath (Join-Path $sourceRootPath 'ACFSample.uproject') -PathType Leaf)) {
    throw "SourceRoot is not LustAsDeadlySin: $sourceRootPath"
}
if (-not $harnessRootPath.StartsWith($allowedHarnessParent + '\', [StringComparison]::OrdinalIgnoreCase)) {
    throw "HarnessRoot escapes the audited BulkProjectContent staging area: $harnessRootPath"
}
if ($harnessContent.StartsWith($targetContent + '\', [StringComparison]::OrdinalIgnoreCase) -or
    $harnessContent.Equals($targetContent, [StringComparison]::OrdinalIgnoreCase)) {
    throw 'Refusing to stage support packages into live target Content.'
}
if (-not (Test-Path -LiteralPath (Join-Path $harnessRootPath 'BulkProjectContent57Harness.uproject') -PathType Leaf)) {
    throw "Detached UE 5.7 harness is absent: $harnessRootPath"
}
if (-not (Test-Path -LiteralPath $manifestFullPath -PathType Leaf)) {
    throw "Manifest is absent: $manifestFullPath"
}

$runningHarness = Get-CimInstance Win32_Process | Where-Object {
    $_.Name -like 'UnrealEditor*' -and
    $_.CommandLine -and
    $_.CommandLine.IndexOf($harnessRootPath, [StringComparison]::OrdinalIgnoreCase) -ge 0
}
if ($runningHarness) {
    $ids = ($runningHarness.ProcessId | Sort-Object) -join ', '
    throw "UE is using Harness57 (PID $ids). Wait for it to exit before staging."
}

$manifestRows = Import-Csv -LiteralPath $manifestFullPath
$manifestByPackage = @{}
foreach ($row in $manifestRows) {
    $manifestByPackage[$row.PackageName] = $row
}

$copyPlan = foreach ($package in $supportPackages) {
    if (-not $manifestByPackage.ContainsKey($package)) {
        throw "Support package is absent from manifest: $package"
    }
    $row = $manifestByPackage[$package]
    $relative = $package.Substring('/Game/'.Length).Replace('/', '\') + '.uasset'
    $sourceFile = Join-Path (Join-Path $sourceRootPath 'Content') $relative
    $stagedFile = Join-Path $harnessContent $relative
    if (-not (Test-Path -LiteralPath $sourceFile -PathType Leaf)) {
        throw "Source support package is absent: $sourceFile"
    }
    $sourceInfo = Get-Item -LiteralPath $sourceFile
    $sourceHash = (Get-FileHash -LiteralPath $sourceFile -Algorithm SHA256).Hash.ToUpperInvariant()
    if ([long]$row.SourceLength -ne $sourceInfo.Length -or
        $row.SourceSHA256.ToUpperInvariant() -ne $sourceHash) {
        throw "Source bytes differ from the audited manifest: $package"
    }
    [pscustomobject]@{
        Package = $package
        Source = $sourceFile
        Destination = $stagedFile
        Length = $sourceInfo.Length
        SHA256 = $sourceHash
    }
}

foreach ($item in $copyPlan) {
    $destinationDirectory = Split-Path -Parent $item.Destination
    if ($PSCmdlet.ShouldProcess($item.Destination, "Stage audited UE 5.7 support package $($item.Package)")) {
        New-Item -ItemType Directory -Path $destinationDirectory -Force | Out-Null
        Copy-Item -LiteralPath $item.Source -Destination $item.Destination -Force
        $stagedInfo = Get-Item -LiteralPath $item.Destination
        $stagedHash = (Get-FileHash -LiteralPath $item.Destination -Algorithm SHA256).Hash.ToUpperInvariant()
        if ($stagedInfo.Length -ne $item.Length -or $stagedHash -ne $item.SHA256) {
            throw "Staged bytes failed verification: $($item.Package)"
        }
    }
}

$copyPlan | Select-Object Package, Length, SHA256, Destination
Write-Host "CALYSTO_DATA_ASSET_SUPPORT57_STAGED: packages=$($copyPlan.Count) destination=$harnessContent"
