$ErrorActionPreference = 'Stop'
Set-StrictMode -Version Latest
. (Join-Path $PSScriptRoot 'EquipmentAutomationReport.ps1')
foreach ($mode in @('Controller', 'Equipment')) {
    $expected = @(if ($mode -eq 'Controller') { 'NoShellForWinter.CalystoDungeon.Director.DormantController.AuditedNpcProfilePreflight' }
        else { 'EF.Equipment.Native.Vertical'; 'EF.Equipment.Policy.Contracts' })
    $source = @{
        Tests = @($expected | ForEach-Object { @{ FullTestPath=$_; State='Success' } })
        Succeeded=$expected.Count; SucceededWithWarnings=0; Failed=0; NotRun=0; InProcess=0
    } | ConvertTo-Json -Depth 5
    $good = $source | ConvertFrom-Json
    if (-not (Test-ExactEquipmentAutomationReport $good $expected)) { throw 'Exact success rejected.' }
    foreach ($case in @('extra','omitted','renamed','case','duplicate','failed','warning','notrun','inprocess','count')) {
        $bad = $source | ConvertFrom-Json
        switch ($case) {
            'extra' { $bad.Tests += [pscustomobject]@{FullTestPath='Unexpected';State='Success'}; $bad.Succeeded++ }
            'omitted' { $bad.Tests = @($bad.Tests | Select-Object -Skip 1); $bad.Succeeded-- }
            'renamed' { $bad.Tests[0].FullTestPath = 'N' }
            'case' { $bad.Tests[0].FullTestPath = $bad.Tests[0].FullTestPath.ToLowerInvariant() }
            'duplicate' { $bad.Tests += $bad.Tests[0]; $bad.Succeeded++ }
            'failed' { $bad.Tests[0].State = 'Fail' }
            'warning' { $bad.SucceededWithWarnings = 1 }
            'notrun' { $bad.NotRun = 1 }
            'inprocess' { $bad.InProcess = 1 }
            'count' { $bad.Succeeded++ }
        }
        if (Test-ExactEquipmentAutomationReport $bad $expected) { throw "Invalid report accepted: $case" }
    }
}
Write-Host 'Exact Automation report regression: PASS (22 cases, including singleton controller inventory).'
