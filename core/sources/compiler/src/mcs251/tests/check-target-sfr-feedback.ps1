param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
$null=New-Item -ItemType Directory -Path $OutputDirectory
$rows=@()
foreach($layout in @('legacy-v1','sph85-v1')) {
 foreach($address in @(129,132,133,190)) {
  foreach($body in @('mov _alias,a',('mov 0x'+$address.ToString('x')+',a'),"mov dr0,#$address`nmov @dr0,a")) {
   $effect=Get-Mx251BankBody $body @{_alias=$address} -SfrLayout $layout
   $stack=$address -eq 129 -or $address -eq $(if($layout -ceq 'sph85-v1'){133}else{190})
   if(($null -ne $effect.reason) -ne $stack){throw "Wrong stack effect: $layout $address $body"}
   if(-not $stack -and ($effect.dpxWrites -ne $(if($address -in @(132,133)){2}else{0}))){throw 'Wrong DPX effect'}
   $rows+=@{layout=$layout;address=$address;body=$body;effect=$effect}
  }
 }
}
# The emitted record, rather than a chip or symbol name, selects each unit's layout.
foreach($layout in @('legacy-v1','sph85-v1')) {
 $name=if($layout -ceq 'legacy-v1'){'u0'}else{'u1'}
 $asm="; mx251-sfr-layout schema=1 contract=$layout`n.globl _${name}`n; mx251-function begin schema=1 symbol=_$name isr=0 addressTaken=0 opaque=0 indirect=0`n; mx251-bank-contract schema=1 symbol=_$name abi=2 bank=0 naked=0`n_${name}:`nmov 0xbe,a`neret`n; mx251-function end schema=1 symbol=_$name`n"
 [IO.File]::WriteAllText((Join-Path $OutputDirectory "$name.asm"),$asm)
 [IO.File]::WriteAllText((Join-Path $OutputDirectory "$name.rel"),"XH3`nA REG_BANK_0 size 8 flags 4 addr 0`n")
}
$scan=Get-Mx251BankFunctions $OutputDirectory @{units=@(@{name='u0';file='u0.c'},@{name='u1';file='u1.c'})}
if(-not $scan.symbols['_u0'][0].reason -or $scan.symbols['_u1'][0].reason){throw 'Per-unit layout binding failed'}
@{status='PASS';addressChecks=$rows.Count;unitChecks=2;rows=$rows}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $OutputDirectory 'result.json')
Write-Output "PASS: $($rows.Count) address and 2 per-unit layout checks"
