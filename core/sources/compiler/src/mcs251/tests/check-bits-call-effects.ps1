param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
$null=New-Item -ItemType Directory -Path $OutputDirectory
$rows=@()
$objects=@{_ready=@{space='bit';size=1};_byte=@{space='data';size=1}}
foreach($case in @(
 @('setb b0',1),@('clr b7',1),@('mov bits,a',1),@('pop bits',1),
 @('mov c,b0',0),@('push bits',0),@('setb _ready',0),@('mov _byte,a',0),
 @('setb 0x00',1),@('clr 0x7f',1),@('mov 0x20,a',1),@('mov 0x2f,a',1),
 @('mov 0x1f,wr0',1),@('mov 0x30,a',0),@('setb 0x98.1',0),
 @('mov @dr0,a',1),@('mystery a',1))) {
 $e=Get-Mx251BankBody $case[0] @{} $objects
 if($e.bitsWrites -ne $case[1]){throw ($case[0]+': '+($e|ConvertTo-Json -Compress))}
 $rows+=@{body=$case[0];effect=$e}
}
function Body([string]$Name,[string]$Code) {
 return ".globl $Name`n; mx251-function begin schema=1 symbol=$Name isr=0 addressTaken=0 opaque=0 indirect=0`n; mx251-bank-contract schema=1 symbol=$Name abi=2 bank=0 naked=0`n${Name}:`n$Code`neret`n; mx251-function end schema=1 symbol=$Name`n"
}
$text=(Body _plain 'mov r7,#1')+(Body _bits 'setb b3')+(Body _chain 'ecall _bits')+
 (Body _recursive "ecall _recursive`necall _plain")+(Body _unknown 'ecall _external')
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'u0.asm'),$text)
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'u0.rel'),"XH3`nA REG_BANK_0 size 8 flags 4 addr 0`n")
$scan=Get-Mx251BankFunctions $OutputDirectory @{units=@(@{name='u0';file='u0.c'})}
foreach($case in @(@('_plain',0),@('_bits',1),@('_chain',1),@('_recursive',0),@('_unknown',1))) {
 if($scan.symbols[$case[0]][0].bitsWrites -ne $case[1]){throw ('closure '+$case[0])}
}
@{status='PASS';instructionChecks=$rows.Count;closureChecks=5;rows=$rows}|ConvertTo-Json -Depth 10|
 Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
Write-Output "PASS: $($rows.Count) instruction and 5 closure checks"
