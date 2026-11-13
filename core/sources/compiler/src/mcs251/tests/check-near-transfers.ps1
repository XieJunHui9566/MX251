param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/FunctionGc.ps1')
. (Join-Path $repo 'core/build/qualified/NearTransfers.ps1')
$null=[IO.Directory]::CreateDirectory($Output)
$template=@'
.globl _callee
.globl _caller
; mx251-function begin schema=1 symbol=_callee isr=0 addressTaken=0 opaque=0 indirect=0
; mx251-scalar-definition schema=1 symbol=_callee signature=u16(u16)
_callee:
push wr4
mov wr4,@spx-1
ejmp L1
nop
L1:
pop wr4
eret
; mx251-bank-contract schema=1 symbol=_callee abi=2 bank=0 naked=0
; mx251-function end schema=1 symbol=_callee
; mx251-function begin schema=1 symbol=_caller isr=0 addressTaken=0 opaque=0 indirect=0
; mx251-scalar-definition schema=1 symbol=_caller signature=u16(u16,u16)
; mx251-scalar-call schema=1 owner=_caller target=_callee signature=u16(u16) values=?
_caller:
ecall _callee
eret
; mx251-bank-contract schema=1 symbol=_caller abi=2 bank=0 naked=0
; mx251-function end schema=1 symbol=_caller
'@
$cases=@(
 @{name='register-local-frame';text=$template;calls=1;jumps=1},
 @{name='address-taken';text=$template.Replace('addressTaken=0','addressTaken=1');calls=0;jumps=1},
 @{name='opaque';text=$template.Replace('opaque=0','opaque=1');calls=0;jumps=0},
 @{name='stack-parameter';text=$template.Replace('signature=u16(u16)','signature=u16(u16,u8)');calls=0;jumps=1},
 @{name='native-two-registers';text=$template.Replace('scalar-','near-').Replace('signature=u16(u16)','signature=u16(u16,u16)').Replace('abi=2','abi=4');calls=1;jumps=1},
 @{name='library-entry';text=$template;exclude=@('_callee');calls=0;jumps=1},
 @{name='address-in-data';text=$template+"`n.dw _callee";calls=0;jumps=1},
 @{name='tail-call';text=$template.Replace('ejmp L1','ejmp _far');calls=0;jumps=0},
 @{name='jump-table';text=$template.Replace('nop','ejmp @dr16');calls=0;jumps=0},
 @{name='relative-offset';text=$template.Replace('nop','sjmp .+6');calls=0;jumps=0},
 @{name='untyped-caller';text=$template.Replace('mx251-scalar-call','old-call');calls=0;jumps=1},
 @{name='isr-entry';text=$template.Replace('isr=0','isr=1');calls=0;jumps=1},
 @{name='bank-one';text=$template.Replace('bank=0','bank=1');calls=0;jumps=1},
 @{name='far-pointer-reference';text=$template.Replace('ecall _callee','mov dr4,#_callee');calls=0;jumps=1}
)
$result=@()
foreach($case in $cases){
 [IO.File]::WriteAllText((Join-Path $Output 'u0.asm'),$case.text,[Text.UTF8Encoding]::new($false))
 $units=@(Get-Mx251NearUnits $Output @{units=@(@{name='u0';file='main.c'})})
 $exclude=if($case.ContainsKey('exclude')){$case.exclude}else{@()}
 $plan=Get-Mx251NearPlan $units $exclude
 if($plan.calls -ne $case.calls -or $plan.jumps -ne $case.jumps){throw ($case.name+': unexpected calls/jumps '+$plan.calls+'/'+$plan.jumps)}
 $result+=@{name=$case.name;calls=$plan.calls;jumps=$plan.jumps;status='PASS'}
}
[IO.File]::WriteAllText((Join-Path $Output 'result.json'),(ConvertTo-Json -InputObject $result -Depth 5))
Write-Output ('PASS '+$result.Count+' transfer-selection cases')
