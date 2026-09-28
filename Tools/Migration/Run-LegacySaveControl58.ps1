param([switch]$PracticeEnabled)
$ErrorActionPreference='Stop'
$root='D:\Projects UE5\NoShellForWinter'
$out=Join-Path $root ('Saved/Analysis/PracticeProgression/LegacySaveControl_'+(Get-Date -Format yyyyMMdd_HHmmss))
[void][IO.Directory]::CreateDirectory($out)
. (Join-Path $root 'Tools/Migration/Progression-SaveGuard.ps1')
Invoke-ProgressionSaveGuard -ProjectRoot $root -EvidenceDirectory ($out+'/SaveGuard') -Action {
 $arguments='-ExecutePythonScript="'+$root+'/Tools/Migration/Validate-LegacySaveControl58.py" -NoSound -ABSLOG="'+$out+'/editor.log"'
 if ($PracticeEnabled) { $arguments+=' -PracticeControlEnabled' }
 & (Join-Path $root 'Tools/Migration/Launch-NoShellForWinterEditor58.ps1') -ProjectRoot $root -AdditionalArguments $arguments -Wait
}
