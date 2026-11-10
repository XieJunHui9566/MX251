# Replay two recorded, fully linked builds through the production decision.
# This tests publication when the candidate grows; it is not a compiler run.
param(
 [Parameter(Mandatory=$true)][string]$Baseline,
 [Parameter(Mandatory=$true)][string]$LargerCandidate,
 [Parameter(Mandatory=$true)][string]$Core,
 [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$module=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../core/build/qualified'))
. (Join-Path $module 'FunctionGc.ps1')
. (Join-Path $module 'FormatRuntime.ps1')
. (Join-Path $module 'SelectiveLeafAbi.ps1')
function Hash-File([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
if(Test-Path -LiteralPath $Output){throw 'Use a fresh replay output directory'}
Copy-Item -LiteralPath $Baseline -Destination $Output -Recurse
$receipt=Get-Content -LiteralPath (Join-Path $Output 'receipt.json') -Raw|ConvertFrom-Json -AsHashtable
$config=Get-Content -LiteralPath (Join-Path $Output 'project.snapshot.json') -Raw|ConvertFrom-Json -AsHashtable
$candidate=Get-Content -LiteralPath (Join-Path $LargerCandidate 'receipt.json') -Raw|ConvertFrom-Json -AsHashtable
if($candidate.codeBytes -le $receipt.codeBytes){throw 'Replay requires a genuinely larger image'}
foreach($pair in @(@{root=$Output;receipt=$receipt},@{root=$LargerCandidate;receipt=$candidate})){
 if((Hash-File (Join-Path $pair.root 'firmware.hex')) -cne $pair.receipt.firmwareSha256 -or (Get-Mx251FormatHex (Join-Path $pair.root 'firmware.hex')).Count -ne $pair.receipt.codeBytes){throw 'Replay image/receipt mismatch'}
}
$driver=Join-Path $Output 'replay.ps1'
[IO.File]::WriteAllText($driver,@'
param([string]$OutputDirectory,[string]$SelectiveLeafAbi,[string]$ReplaySource)
$ErrorActionPreference='Stop'
Copy-Item -LiteralPath $ReplaySource -Destination $OutputDirectory -Recurse
'@)
$SelectiveLeafAbi='AUTO';$FunctionGcKeep=''
$result=Invoke-Mx251SelectiveLeafAbi $Output $config $receipt (Join-Path $Core 'toolchain') (Join-Path $Core ('profiles/'+$config.target)) $driver @{ReplaySource=[IO.Path]::GetFullPath($LargerCandidate)}
if($result.status -cne 'KEEP_BASELINE' -or $result.applied -or $result.ContainsKey('reason') -or $result.candidateCodeBytes -ne $candidate.codeBytes -or $result.selectedCodeBytes -ne $receipt.codeBytes){throw 'Larger linked image was not rejected by the size decision'}
if((Hash-File (Join-Path $Output 'firmware.hex')) -cne $receipt.firmwareSha256){throw 'Replay changed the published baseline'}
$result.replay=$true
[IO.File]::WriteAllText((Join-Path $Output 'replay-result.json'),($result|ConvertTo-Json -Depth 20))
$result|ConvertTo-Json -Depth 20
