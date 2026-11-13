param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
$null=New-Item -ItemType Directory -Path $OutputDirectory
$rows=@()
function Check([string]$Name,[string]$Body,[int]$Expected,$Aliases=@{}) {
 $e=Get-Mx251BankBody $Body $Aliases
 if($e.dpxWrites -ne $Expected){throw "$Name : $($e|ConvertTo-Json -Compress)"}
 $script:rows+=@{name=$Name;body=$Body;effect=$e}
}
Check 'GPR writes' 'mov dr12,#0x12345678' 0
Check 'DPTR' 'mov dptr,#0x1234' 1
Check 'DPL' 'mov dpl,a' 1
Check 'DPH' 'pop dph' 1
Check 'DPXL' 'mov dpxl,#1' 2
Check 'high word' 'mov wr56,wr0' 2
Check 'low word' 'inc wr58' 1
Check 'full DPX' 'mov dr56,dr0' 3
Check 'DPX alias' 'inc dpx' 3
Check 'direct high alias' 'mov _high,a' 2 @{_high=132}
Check 'word spans low and high' 'mov 0x83,wr0' 3
Check 'bit low alias' 'setb 0x82.0' 1
Check 'bit high alias' 'clr 0x84.1' 2
Check 'high exchange' 'xch a,dpxl' 2
Check 'read only' "mov a,dpxl`npush dptr`nmov dr0,dr56" 0
Check 'unknown pointer' 'mov @dr0,a' 3
Check 'flat alias through DR' "mov dr0,#0x84`nmov @dr0,a" 2
Check 'flat alias through DPX' "mov dptr,#0x84`nmov dpxl,#0`nmov @dpx,a" 3
Check 'ordinary object' 'mov _value,a' 0 @{_value=40}
Check 'unknown instruction' 'undocumented dpx' 3
Check 'local call discovery' "ecall _low`nmov r7,#1" 0

function FunctionText([string]$Name,[string]$Body,[bool]$Export=$true) {
 $g=if($Export){".globl $Name`n"}else{''}
 return $g+"; mx251-function begin schema=1 symbol=$Name isr=0 addressTaken=0 opaque=0 indirect=0`n; mx251-bank-contract schema=1 symbol=$Name abi=2 bank=0 naked=0`n${Name}:`n$Body`neret`n; mx251-function end schema=1 symbol=$Name`n"
}
$asm=(FunctionText _low 'mov dpl,a')+(FunctionText _high 'mov dpxl,a')+
 (FunctionText _chain "ecall _low`necall _high")+(FunctionText _unknown 'ecall _external')+
 (FunctionText _recursive "ecall _recursive`necall _low")+(FunctionText _private 'mov dpxl,a' $false)+
 (FunctionText _local_caller 'ecall _private')
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'u0.asm'),$asm)
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'u0.rel'),"XH3`nA REG_BANK_0 size 8 flags 4 addr 0`n")
$other=(FunctionText _private 'mov dpl,a' $false)+(FunctionText _other_caller 'ecall _private')
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'u1.asm'),$other)
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'u1.rel'),"XH3`nA REG_BANK_0 size 8 flags 4 addr 0`n")
$scan=Get-Mx251BankFunctions $OutputDirectory @{units=@(@{name='u0';file='u0.c'},@{name='u1';file='u1.c'})}
foreach($case in @(@('_chain',3),@('_unknown',3),@('_recursive',1),@('_local_caller',2),@('_other_caller',1))) {
 if($scan.symbols[$case[0]][0].dpxWrites -ne $case[1]){throw "closure $($case[0])"}
}
@{status='PASS';instructionChecks=$rows.Count;closureChecks=5;rows=$rows}|ConvertTo-Json -Depth 10|
 Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
Write-Output "PASS: $($rows.Count) instruction and 5 closure checks"
