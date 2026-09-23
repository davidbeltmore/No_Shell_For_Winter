function Test-ExactEquipmentAutomationReport {
    param([Parameter(Mandatory)]$Report, [Parameter(Mandatory)][string[]]$Expected)
    $tests = @($Report.Tests)
    $names = @($tests | ForEach-Object { [string]$_.FullTestPath })
    return ($Expected.Count -gt 0 -and $names.Count -eq $Expected.Count -and
        @($Expected | Sort-Object -Unique).Count -eq $Expected.Count -and
        @(Compare-Object -CaseSensitive $Expected $names).Count -eq 0 -and
        @($tests | Where-Object { $_.State -cne 'Success' }).Count -eq 0 -and
        $Report.Succeeded -eq $Expected.Count -and $Report.SucceededWithWarnings -eq 0 -and
        $Report.Failed -eq 0 -and $Report.NotRun -eq 0 -and $Report.InProcess -eq 0)
}
