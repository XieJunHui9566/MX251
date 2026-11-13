param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
$null=New-Item -ItemType Directory -Path $OutputDirectory
$objects=@{_array=@{space='xdata';size=16384;addressLow=65536L;addressHigh=77824L}}
$body=@'
mov a,#((_array+0)>>16)
movz wr16,a
mov wr18,#(_array+0)
mov wr22,wr14
sub wr20,wr20
add dr16,dr20
anl wr16,#0x00ff
mov @dr16,r9
'@
$rows=@()
function Check($Name,$Body,$Known,$Objects=$objects) {
 $e=Get-Mx251BankBody $Body @{} $Objects -SfrLayout sph85-v1
 if(($null -eq $e.reason) -ne $Known){throw "$Name : $($e|ConvertTo-Json -Compress)"}
 $script:rows+=@{name=$Name;known=$Known;effect=$e}
}
Check 'allocated symbol plus unsigned16 index' $body $true
Check 'unbound declaration' $body $false @{}
Check 'full unknown32 index' ($body.Replace('sub wr20,wr20','mov wr20,wr0')) $false
Check 'negative index can reach registers' ($body.Replace('add dr16,dr20','sub dr16,dr20')) $false
Check 'byte overlap destroys address upper zero' ($body.Replace('mov @dr16,r9',"mov r16,r1`nmov @dr16,r9")) $false
Check 'call destroys address facts' ($body.Replace('mov @dr16,r9',"ecall _helper`nmov @dr16,r9")) $false
Check 'branch keeps identical range' ($body.Replace('mov @dr16,r9',"jz L`nnop`nL:`nmov @dr16,r9")) $true
Check 'one branch destroys range' ($body.Replace('mov @dr16,r9',"jz L`nmov dr16,dr0`nL:`nmov @dr16,r9")) $false
Check 'loop invariant pointer' ($body.Replace('mov @dr16,r9',"L:`nmov @dr16,r9`ndjnz r9,L")) $true
Check 'loop changed pointer' ($body.Replace('mov @dr16,r9',"L:`nmov @dr16,r9`ninc dr16,#1`ndjnz r9,L")) $false
Check '24bit truncation may wrap into core' ($body.Replace('mov @dr16,r9',"add dr16,#0x00ff0000`nanl wr16,#0x00ff`nmov @dr16,r9")) $false
Check 'last legal byte' "mov dr16,#0xffffff`nL:`nmov @dr16,r9" $true
Check 'word at last byte' "mov dr16,#0xffffff`nL:`nmov @dr16,wr8" $false
# Check arithmetic endpoints and wrap transfer against concrete executions.
$arithmetic=0
foreach($width in @(1,2,4)) {
 $mod=[long]1 -shl (8*$width)
 $values=@(0L,1L,127L,255L,($mod-2),($mod-1))|Select-Object -Unique
 foreach($op in @('add','sub')){foreach($left in $values){foreach($right in $values){
  $register=switch($width){1{'r0'};2{'wr0'};4{'dr0'}}
  [int[]]$before=@(-1)*36
  $bytes=Get-Mx251RangeBytes $left $left $width
  for($i=0;$i -lt $width;$i++){$before[$i]=$bytes[$i]}
  $node=@{op=$op;operand="$register,#$right";args=@($register,"#$right")}
  $after=Invoke-Mx251FactInstruction $node $before @{} @{}
  $expect=if($op -eq 'add'){($left+$right)%$mod}else{(($left-$right)%$mod+$mod)%$mod}
  $got=Get-Mx251FactRange $after[0..($width-1)]
  if($got[0] -ne $expect -or $got[1] -ne $expect){throw "Arithmetic $op/$width/$left/$right"}
  $arithmetic++
 }}}
}
# Exact allocations and exported ownership govern whether a symbol can enter the proof.
$asm=@'
.globl _array
.area XSEG (XDATA)
_array::
 .ds 16384
; mx251-object schema=1 symbol=_array space=xdata size=16384
'@
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'u0.asm'),$asm)
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'u0.rel'),"XH3`nA XSEG size 4000 flags 40 addr 0`nS _array Def000000`n")
$config=@{units=@(@{name='u0';file='u0.c'})};$layout=@{xramBase=65536L;xramBytes=28672L}
$bound=Get-Mx251BankObjects $OutputDirectory $config $layout
if($bound.u0._array.addressHigh -ne 77824){throw 'Object allocation window'}
if((Get-Mx251BankObjects $OutputDirectory $config).u0.ContainsKey('_array')){throw 'Unbound memory window admitted'}
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'u1.rel'),"XH3`nS _array Def000000`n")
$config.units+=@(@{name='u1';file='extra.asm'})
if((Get-Mx251BankObjects $OutputDirectory $config $layout).u0.ContainsKey('_array')){throw 'Duplicate definition admitted'}
@{status='PASS';addressCases=$rows.Count;arithmeticCases=$arithmetic;allocationCases=3;rows=$rows}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $OutputDirectory 'result.json')
Write-Output "PASS: $($rows.Count) address, $arithmetic arithmetic, 3 allocation cases"
