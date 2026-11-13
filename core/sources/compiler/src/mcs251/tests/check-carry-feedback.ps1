param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
$null=New-Item -ItemType Directory -Path $OutputDirectory
$rows=@()
$aliases=@{_renamedCarry=215;_status=208;_bank=211;_wholePsw=208}
foreach($operand in @('c','cy','0xd7','_renamedCarry','0xd0.7','_status.7')) {
 foreach($op in @('clr','setb','cpl','mov')) {
  $body="mov r7,dpl`nmov a,r7`nadd a,r7`n$op $operand"+$(if($op -eq 'mov'){',c'}else{''})+"`nmov r7,a`neret"
  $effect=Get-Mx251BankBody $body $aliases
  if($null -ne $effect.reason -or $effect.writes -ne 2176 -or $effect.dpxWrites -ne 0){throw ($effect|ConvertTo-Json -Compress)}
  $rows+=@{operand=$operand;operation=$op;effect=$effect}
 }
}
foreach($body in @('setb 0xd3','clr 0xd4','mov _bank,c','setb 0xd0.3','clr _status.4','mov _wholePsw,#0','mov 0xd7,#0')) {
 $effect=Get-Mx251BankBody $body $aliases
 # Byte D7 is a peripheral SFR, not the bit-address D7: do not interpret it as CY.
 if($body -eq 'mov 0xd7,#0') {
  if($null -ne $effect.reason -or $effect.writes -ne 0){throw $body}
 } elseif($null -eq $effect.reason -or $effect.writes -ne 4294967295L){throw $body}
 $rows+=@{body=$body;effect=$effect}
}
@{status='PASS';checks=$rows.Count;rows=$rows} | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
Write-Output "PASS: $($rows.Count) CY aliases, bank bits and whole-byte access cases"
