param([Parameter(Mandatory)][string]$ProfileRoot,[Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
. (Join-Path $repo 'core/build/qualified/RuntimeCallEffects.ps1')
function Hash-File([string]$Path){return (Get-FileHash $Path).Hash.ToLowerInvariant()}
function Check($condition,[string]$why){if(-not $condition){throw $why};$script:checks++}
$checks=0;$aliases=New-Mx251OrdinalMap
foreach($op in @('mul','div')){
 foreach($width in @(1,2)){
  $limit=if($width -eq 1){16}else{32};$prefix=if($width -eq 1){'r'}else{'wr'}
  for($r=0;$r -lt $limit;$r+=$width){
   $first=$r-$r%(2*$width);$mask=(([long]1 -shl (2*$width))-1) -shl $first
   $effect=Get-Mx251BankBody "$op $prefix$r,${prefix}0" $aliases
   Check ($effect.writes -eq $mask) "$op $prefix$r writes its enclosing register"
  }
 }
 Check ((Get-Mx251BankBody "$op ab" $aliases).writes -eq 3072) "$op AB aliases"
 Check ((Get-Mx251BankBody "$op dr4,dr12" $aliases).writes -eq 4294967295L) 'no dword instruction'
}
$body="mov @dr4,r7`necall _later`nejmp _tail`n"
$effect=Get-Mx251BankBody $body $aliases
Check ($effect.writes -eq 4294967295L -and '_later' -cin $effect.calls -and '_tail' -cin $effect.calls) 'unknown caller writes do not truncate call discovery'
$record=Get-Mx251RuntimeCallEffects $ProfileRoot @('runtime/libnativeint.lib')
Check ($record.functions.Count -eq 5) 'five bound native runtime members'
Assert-Mx251RuntimeCallEffects $record
$null=New-Item -ItemType Directory -Path $OutputDirectory
# A real closure must include an application override of a library dependency.
$rel="XH3`nS ___m251r4__divulong Def000000`nA REG_BANK_0 size 8 flags 4 addr 0`n"
$asm=@'
.globl ___m251r4__divulong
; mx251-function begin schema=1 symbol=___m251r4__divulong isr=0 addressTaken=0 opaque=0 indirect=0
; mx251-bank-contract schema=1 symbol=___m251r4__divulong abi=4 bank=0 naked=0
___m251r4__divulong:
 mov dr28,#1
 eret
; mx251-function end schema=1 symbol=___m251r4__divulong
'@
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'override.rel'),$rel)
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'override.asm'),$asm+"`n")
$config=@{units=@(@{name='override';file='override.c'})}
$functions=@($record.functions|Where-Object{$_.symbol -cne '___m251r4__divulong'})
$scan=Get-Mx251BankFunctions $OutputDirectory $config $functions
Check (($scan.symbols['___m251r4__divslong'][0].writes -band 4026531840L) -eq 4026531840L) 'library-to-application dependency uses actual override effects'
$scan2=Get-Mx251BankFunctions $OutputDirectory @{units=@()} $record.functions
Check ($scan2.symbols['___m251r4__divslong'][0].writes -eq 1047807L) 'signed division includes unsigned implementation writes'
Check ($scan2.symbols['___m251r4__modslong'][0].writes -eq 1047807L) 'signed remainder includes its complete runtime dependency chain'
if('___m251r4__divulong' -cin $scan2.symbols['___m251r4__modulong'][0].calls){
 Check (($scan.symbols['___m251r4__modslong'][0].writes -band 4026531840L) -eq 4026531840L) 'shared remainder chain reaches an application override'
}
@{status='PASS';checks=$checks;contractSha256=$record.contractSha256}|ConvertTo-Json|Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
Write-Output "PASS: $checks register, archive and call-closure checks"
