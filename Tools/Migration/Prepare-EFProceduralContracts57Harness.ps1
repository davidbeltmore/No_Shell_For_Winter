[CmdletBinding()]
param(
    [string]$SourceRoot = 'D:\Projects UE5\LustAsDeadlySin',
    [string]$TargetRoot = 'D:\Projects UE5\NoShellForWinter'
)

$ErrorActionPreference = 'Stop'

function Assert-True {
    param([bool]$Condition, [string]$Message)
    if (-not $Condition) {
        throw "EFPROCEDURALCONTRACTS57_HARNESS_GATE_FAIL: $Message"
    }
}

function Test-IsUnderRoot {
    param([string]$Path, [string]$Root)
    $fullPath = [System.IO.Path]::GetFullPath($Path)
    $fullRoot = [System.IO.Path]::GetFullPath($Root).TrimEnd('\') + '\'
    return $fullPath.StartsWith(
        $fullRoot,
        [System.StringComparison]::OrdinalIgnoreCase
    )
}

function Get-Sha256 {
    param([string]$Path)
    return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToUpperInvariant()
}

function Get-HashFingerprint {
    param([object[]]$Rows)
    $hashText = @(
        $Rows |
            Sort-Object Package |
            ForEach-Object { ([string]$_.Sha256).ToUpperInvariant() }
    ) -join "`n"
    $algorithm = [System.Security.Cryptography.SHA256]::Create()
    try {
        $bytes = [System.Text.Encoding]::UTF8.GetBytes($hashText)
        return ([System.BitConverter]::ToString(
            $algorithm.ComputeHash($bytes)
        ) -replace '-', '').ToUpperInvariant()
    }
    finally {
        $algorithm.Dispose()
    }
}

function Copy-VerifiedAsset {
    param(
        [string]$Source,
        [string]$Destination,
        [string]$Package,
        [string]$Role,
        [int64]$ExpectedLength,
        [string]$ExpectedSha256,
        [string]$ExpectedClass,
        [string]$ExpectedNativeParent
    )

    Assert-True (Test-Path -LiteralPath $Source -PathType Leaf) "Missing allowlisted source asset: $Source"
    $sourceItem = Get-Item -LiteralPath $Source
    $sourceHash = Get-Sha256 -Path $Source
    Assert-True ($sourceItem.Length -eq $ExpectedLength) "Source length differs for $Package"
    Assert-True ($sourceHash -eq $ExpectedSha256) "Source hash differs for $Package"

    New-Item -ItemType Directory -Path (Split-Path -Parent $Destination) -Force | Out-Null
    Copy-Item -LiteralPath $Source -Destination $Destination
    Assert-True ((Get-Item -LiteralPath $Destination).Length -eq $ExpectedLength) "Staged length differs for $Package"
    Assert-True ((Get-Sha256 -Path $Destination) -eq $ExpectedSha256) "Staged hash differs for $Package"

    return [pscustomobject][ordered]@{
        package = $Package
        source = $Source
        staged = $Destination
        role = $Role
        class = $ExpectedClass
        native_parent = $ExpectedNativeParent
        length = $ExpectedLength
        sha256 = $ExpectedSha256
    }
}

$expectedPackageCount = 20
$expectedSourceBytes = [int64]223283
$expectedFingerprint = '30A8ACBD998EBD41242A3BD850C8CBB3E7E6A19D63D72E632D0BB897917FA006'
$expectedClassCounts = [ordered]@{
    Blueprint = 6
    UserDefinedStruct = 12
    UserDefinedEnum = 2
}
$directContractRoots = @(
    '/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon',
    '/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial',
    '/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme',
    '/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece',
    '/Game/Calysto/Dungeon/Data/Structure/ST_GrammarSetting',
    '/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting',
    '/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize',
    '/Game/Calysto/Shared/Data/Structure/PDA_Spawner',
    '/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple'
)
$startPointPackage = '/Game/Calysto/Dungeon/Blueprint/Utility/BP_StartPoint'

$allowlist = @(
    [pscustomobject]@{ Package = $startPointPackage; Length = [int64]29816; Sha256 = '611B8EF004978597D20A110DF748FFFFBC2CDD3F934BEF4AF52F24B14824EE2B'; Class = 'Blueprint'; NativeParent = '/Script/Engine.Actor'; Role = 'start_point' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Enumerator/Enum_ObjectType'; Length = [int64]2702; Sha256 = '5D754D9D64357006969B7BEF939DF2BD81E5F393C24DF01940CBE44D878FAA6C'; Class = 'UserDefinedEnum'; NativeParent = ''; Role = 'transitive_contract_dependency' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Enumerator/Enum_Rotation'; Length = [int64]2979; Sha256 = '85288DE490FB1CD2474782889E9FD495E9E39B795606F3004610232B983312C6'; Class = 'UserDefinedEnum'; NativeParent = ''; Role = 'transitive_contract_dependency' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Structure/PDA_Dungeon'; Length = [int64]23811; Sha256 = '406B9AE9DEAF2EB24BDD025D0F6E6E3B53282951D0403F9E559F80ED643A27D3'; Class = 'Blueprint'; NativeParent = '/Script/Engine.PrimaryDataAsset'; Role = 'direct_contract_root' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Structure/PDA_DungeonMaterial'; Length = [int64]10930; Sha256 = 'E37494AE166F9A2184A7ACC583EF71BF8899DCCF2E6FB32A1DF9CF6C7BE52153'; Class = 'Blueprint'; NativeParent = '/Script/Engine.PrimaryDataAsset'; Role = 'direct_contract_root' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Structure/PDA_RoomMeshes'; Length = [int64]15245; Sha256 = 'B0138D0A47FD57A95E4C560AFCF7EB12713109D26ED6DA9D3A1D4E2754BE5509'; Class = 'Blueprint'; NativeParent = '/Script/Engine.PrimaryDataAsset'; Role = 'transitive_contract_dependency' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Structure/PDA_RoomTheme'; Length = [int64]8699; Sha256 = '14C512F0BA2CB14497BA0F9D6EE70707F041104DD4E2617B7AF93172E7ABEAE0'; Class = 'Blueprint'; NativeParent = '/Script/Engine.PrimaryDataAsset'; Role = 'direct_contract_root' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Structure/ST_DungeonMaterial'; Length = [int64]4006; Sha256 = 'DC9EF5E3E742E8D3AFB05032F7AC753B482DE4AC4A8F7041818BA9449B48B5D2'; Class = 'UserDefinedStruct'; NativeParent = ''; Role = 'transitive_contract_dependency' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Structure/ST_DungeonPiece'; Length = [int64]9279; Sha256 = 'FC3720E8289DC8DC9E4D44CE3B08747961BB17EE37F79A9EDCD1A36ECBA638A5'; Class = 'UserDefinedStruct'; NativeParent = ''; Role = 'direct_contract_root' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Structure/ST_GrammarSetting'; Length = [int64]8215; Sha256 = 'AF1FD7EB7F6E2EC8BB3A242ADC2F9A7E4E5FAA5588F444965D8F14F83E9BA988'; Class = 'UserDefinedStruct'; NativeParent = ''; Role = 'direct_contract_root' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeon'; Length = [int64]18254; Sha256 = '738510B428C8D56A7E8B942E268403927A6FE33BB0F5E8B0D5C5DE3ECCD735C0'; Class = 'UserDefinedStruct'; NativeParent = ''; Role = 'transitive_contract_dependency' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Structure/ST_ObjectDungeonLight'; Length = [int64]13183; Sha256 = 'CC20452408C32B4FF419829F5005F67BF6DCF3E54A353F3FE81E1E66D085A0A6'; Class = 'UserDefinedStruct'; NativeParent = ''; Role = 'transitive_contract_dependency' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Structure/ST_RoomSetting'; Length = [int64]6593; Sha256 = '7E38AAB2AA6D7609A4DEA6E1C3801EB8D75D106429B6912848AC706DEC29961D'; Class = 'UserDefinedStruct'; NativeParent = ''; Role = 'direct_contract_root' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Structure/ST_RoomSize'; Length = [int64]4852; Sha256 = '7D33857C66DC6F8704DAA0285E21F26C9C87BA2FE411A35495C26DA973047639'; Class = 'UserDefinedStruct'; NativeParent = ''; Role = 'direct_contract_root' },
    [pscustomobject]@{ Package = '/Game/Calysto/Dungeon/Data/Structure/ST_RoomTheme'; Length = [int64]11412; Sha256 = 'B99C5FDB775A97D50CF8AAD005B93CF7DA4174B17A5F62123ACE66E5805A3B6E'; Class = 'UserDefinedStruct'; NativeParent = ''; Role = 'transitive_contract_dependency' },
    [pscustomobject]@{ Package = '/Game/Calysto/Shared/Data/Structure/PDA_Spawner'; Length = [int64]8487; Sha256 = '39E0D9ED782FC91A3008A4F0EF9F662EBDC72937AB0D47EA8EF4F989A05E8964'; Class = 'Blueprint'; NativeParent = '/Script/Engine.PrimaryDataAsset'; Role = 'direct_contract_root' },
    [pscustomobject]@{ Package = '/Game/Calysto/Shared/Data/Structure/ST_ObjectSimple'; Length = [int64]9550; Sha256 = 'D2CE1AD9093ADF631ED67E2BA3D81FD4142679584FC940DF8EB5EE74F644E007'; Class = 'UserDefinedStruct'; NativeParent = ''; Role = 'direct_contract_root' },
    [pscustomobject]@{ Package = '/Game/Calysto/Shared/Data/Structure/ST_ObjectSimpleActor'; Length = [int64]9614; Sha256 = 'AC76C45646368DCB1AC8DF1E27FA045FD40B823B265505EEFCD3EC5BFB76DD12'; Class = 'UserDefinedStruct'; NativeParent = ''; Role = 'transitive_contract_dependency' },
    [pscustomobject]@{ Package = '/Game/Calysto/Shared/Data/Structure/ST_ObjectWallDoor'; Length = [int64]20663; Sha256 = '27E0F2DB977C590082E9FE578B0C11F2013B27B5E55287B3217A47528DF25A6A'; Class = 'UserDefinedStruct'; NativeParent = ''; Role = 'transitive_contract_dependency' },
    [pscustomobject]@{ Package = '/Game/Calysto/Shared/Data/Structure/ST_Spawner'; Length = [int64]4993; Sha256 = '9C679CA42FC903128DD41C15675D5094C2FA07F516F4FB113B82A3202F1AD411'; Class = 'UserDefinedStruct'; NativeParent = ''; Role = 'transitive_contract_dependency' }
)

Assert-True ($allowlist.Count -eq $expectedPackageCount) "Expected $expectedPackageCount allowlisted packages"
Assert-True ((@($allowlist.Package | Sort-Object -Unique)).Count -eq $expectedPackageCount) 'Allowlist package names are not unique.'
Assert-True ((($allowlist | Measure-Object Length -Sum).Sum) -eq $expectedSourceBytes) 'Allowlist byte total differs from the frozen baseline.'
Assert-True ((Get-HashFingerprint -Rows $allowlist) -eq $expectedFingerprint) 'Allowlist fingerprint differs from the frozen baseline.'
Assert-True ((@($allowlist | Where-Object Role -eq 'direct_contract_root')).Count -eq 9) 'Expected nine direct contract roots.'
Assert-True ((@($allowlist | Where-Object Role -eq 'transitive_contract_dependency')).Count -eq 10) 'Expected ten transitive contract dependencies.'
Assert-True ((@($allowlist | Where-Object Role -eq 'start_point')).Count -eq 1) 'Expected one StartPoint package.'

$source = (Resolve-Path -LiteralPath $SourceRoot).Path.TrimEnd('\')
$target = (Resolve-Path -LiteralPath $TargetRoot).Path.TrimEnd('\')
$sourceContent = (Resolve-Path -LiteralPath (Join-Path $source 'Content')).Path.TrimEnd('\')
$targetContent = (Resolve-Path -LiteralPath (Join-Path $target 'Content')).Path.TrimEnd('\')
$manifestPath = (Resolve-Path -LiteralPath (Join-Path $target 'Docs\Migration\04_Content_Migration_Manifest.csv')).Path
$phaseRoot = Join-Path $target 'Saved\Migration\Phase4'
$harnessRoot = Join-Path $phaseRoot 'EFProceduralContracts57Harness'
$harnessContent = Join-Path $harnessRoot 'Content'
$harnessProject = Join-Path $harnessRoot 'EFProceduralContracts57Harness.uproject'
$receiptPath = Join-Path $phaseRoot 'EFProceduralContracts57HarnessReceipt.json'

Assert-True (-not $source.Equals($target, [System.StringComparison]::OrdinalIgnoreCase)) 'Source and target roots are identical.'
Assert-True (Test-Path -LiteralPath (Join-Path $source 'ACFSample.uproject') -PathType Leaf) 'Source descriptor is absent.'
Assert-True (Test-Path -LiteralPath (Join-Path $target 'NoShellForWinter.uproject') -PathType Leaf) 'Target descriptor is absent.'
Assert-True (Test-IsUnderRoot -Path $harnessRoot -Root $phaseRoot) 'Harness escapes target Saved/Migration/Phase4.'

if (Test-Path -LiteralPath $harnessRoot) {
    $resolvedHarness = (Resolve-Path -LiteralPath $harnessRoot).Path
    Assert-True (Test-IsUnderRoot -Path $resolvedHarness -Root $phaseRoot) 'Refusing to clean a harness outside Phase4 staging.'
    Assert-True ((Split-Path -Leaf $resolvedHarness) -eq 'EFProceduralContracts57Harness') 'Unexpected harness directory name.'
    Remove-Item -LiteralPath $resolvedHarness -Recurse -Force
}

$manifestRows = @(Import-Csv -LiteralPath $manifestPath)
$manifestLookup = @{}
foreach ($row in $manifestRows) {
    $package = [string]$row.PackageName
    Assert-True (-not $manifestLookup.ContainsKey($package)) "Duplicate manifest package: $package"
    $manifestLookup[$package] = $row
}

$stagedAssets = @()
foreach ($expected in @($allowlist | Sort-Object Package)) {
    $package = [string]$expected.Package
    Assert-True ($manifestLookup.ContainsKey($package)) "Package is absent from the Phase 2 manifest: $package"
    $row = $manifestLookup[$package]
    Assert-True ($row.Presence -eq 'SOURCE_ONLY') "Package is not SOURCE_ONLY: $package ($($row.Presence))"
    Assert-True ([int64]$row.SourceLength -eq $expected.Length) "Manifest length differs for $package"
    Assert-True ([string]$row.SourceSHA256 -eq $expected.Sha256) "Manifest hash differs for $package"

    $relativeAsset = $package.Substring('/Game/'.Length).Replace('/', '\') + '.uasset'
    $canonicalSourceFile = [System.IO.Path]::GetFullPath((Join-Path $sourceContent $relativeAsset))
    $manifestSourceFile = [System.IO.Path]::GetFullPath((Join-Path $source ([string]$row.SourceFile)))
    Assert-True ($canonicalSourceFile.Equals($manifestSourceFile, [System.StringComparison]::OrdinalIgnoreCase)) "Manifest source path is noncanonical for $package"
    Assert-True (Test-IsUnderRoot -Path $canonicalSourceFile -Root $sourceContent) "Source package escapes Content: $package"

    foreach ($extension in @('.uexp', '.ubulk', '.uptnl')) {
        $sidecar = [System.IO.Path]::ChangeExtension($canonicalSourceFile, $extension)
        Assert-True (-not (Test-Path -LiteralPath $sidecar)) "Unexpected sidecar for ${package}: $sidecar"
    }

    $targetFile = Join-Path $targetContent $relativeAsset
    Assert-True (-not (Test-Path -LiteralPath $targetFile)) "Target collision exists before migration: $package"

    $stagedFile = Join-Path $harnessContent $relativeAsset
    Assert-True (Test-IsUnderRoot -Path $stagedFile -Root $harnessContent) "Staged package escapes harness Content: $package"
    $stagedAssets += Copy-VerifiedAsset `
        -Source $canonicalSourceFile `
        -Destination $stagedFile `
        -Package $package `
        -Role ([string]$expected.Role) `
        -ExpectedLength ([int64]$expected.Length) `
        -ExpectedSha256 ([string]$expected.Sha256) `
        -ExpectedClass ([string]$expected.Class) `
        -ExpectedNativeParent ([string]$expected.NativeParent)
}

Assert-True ($stagedAssets.Count -eq $expectedPackageCount) 'Staged package count differs from the exact allowlist.'
Assert-True ((($stagedAssets | Measure-Object Length -Sum).Sum) -eq $expectedSourceBytes) 'Staged byte total differs from the exact allowlist.'
Assert-True ((Get-HashFingerprint -Rows $stagedAssets) -eq $expectedFingerprint) 'Staged fingerprint differs from the exact allowlist.'

New-Item -ItemType Directory -Path $harnessRoot -Force | Out-Null
[ordered]@{
    FileVersion = 3
    EngineAssociation = '5.7'
    Category = 'MigrationTools'
    Description = 'Isolated UE 5.7 harness for exactly nineteen Calysto data contracts and the canonical BP_StartPoint.'
    Plugins = @(
        [ordered]@{ Name = 'PythonScriptPlugin'; Enabled = $true },
        [ordered]@{ Name = 'PCG'; Enabled = $true }
    )
} | ConvertTo-Json -Depth 8 | Set-Content -LiteralPath $harnessProject -Encoding UTF8

$actualClassCounts = [ordered]@{}
foreach ($name in $expectedClassCounts.Keys) {
    $actualClassCounts[$name] = @($stagedAssets | Where-Object class -eq $name).Count
}
Assert-True (($actualClassCounts | ConvertTo-Json -Compress) -eq ($expectedClassCounts | ConvertTo-Json -Compress)) 'Staged class counts differ from the exact allowlist.'

[ordered]@{
    schema_version = 1
    generated_utc = [DateTime]::UtcNow.ToString('o')
    status = 'ISOLATED_PROCEDURAL_CONTRACTS_HARNESS_PASS'
    source_root = $source
    target_root = $target
    harness_root = $harnessRoot
    harness_content = $harnessContent
    harness_project = $harnessProject
    expected_package_count = $expectedPackageCount
    expected_source_bytes = $expectedSourceBytes
    expected_source_fingerprint = $expectedFingerprint
    fingerprint_algorithm = 'SHA256 of uppercase per-file SHA256 values joined with LF after ordinal package-name sort'
    expected_class_counts = $expectedClassCounts
    expected_blueprint_parent_counts = [ordered]@{
        '/Script/Engine.PrimaryDataAsset' = 5
        '/Script/Engine.Actor' = 1
    }
    direct_contract_roots = $directContractRoots
    start_point_package = $startPointPackage
    staged_assets = $stagedAssets
    source_package_loads = 0
    source_package_saves = 0
    policy = 'Exactly twenty frozen-hash source assets are copied into an isolated target/Saved harness. The live source is never mounted, loaded, resaved, or written; target Content remains untouched by this preparation script.'
    excluded = @(
        '/Game/Calysto/Dungeon/Blueprint/BP_MassiveDungeon',
        '/Game/Procedural/Maps/DungeonGeneration',
        '/Game/Procedural/DoorToLevel'
    )
} | ConvertTo-Json -Depth 12 | Set-Content -LiteralPath $receiptPath -Encoding UTF8

Write-Host "EFPROCEDURALCONTRACTS57_HARNESS_PASS: $harnessProject"
Write-Host "Packages: $($stagedAssets.Count); bytes: $expectedSourceBytes; fingerprint: $expectedFingerprint"
Write-Host "Receipt: $receiptPath"
