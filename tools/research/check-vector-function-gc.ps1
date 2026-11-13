param(
 [Parameter(Mandatory=$true)][string]$Core,
 [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$module=Join-Path $Core 'build/qualified'
. (Join-Path $module 'FunctionGc.ps1')
. (Join-Path $module 'InterruptVectors.ps1')
function Hash-File([string]$Path){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
if(Test-Path -LiteralPath $Output){throw 'Use a fresh test output directory'}
$null=New-Item -ItemType Directory -Path $Output
$source=@'
.module fixture
.area HOME (CODE)
ecall _main
; mx251-irq-module schema=2 hasMain=1 vectorCount=0
; mx251-irq-definition schema=2 symbol=_irq slot=13 private=0
.area CSEG (CODE)
; mx251-function begin schema=1 symbol=_main isr=0 addressTaken=0 opaque=0 indirect=0
_main:
ecall _live
eret
; mx251-function end schema=1 symbol=_main
; mx251-function begin schema=1 symbol=_irq isr=1 addressTaken=0 opaque=0 indirect=0
_irq:
ecall _irq_helper
reti
; mx251-function end schema=1 symbol=_irq
; mx251-function begin schema=1 symbol=_live isr=0 addressTaken=0 opaque=0 indirect=0
_live:
eret
; mx251-function end schema=1 symbol=_live
; mx251-function begin schema=1 symbol=_irq_helper isr=0 addressTaken=0 opaque=0 indirect=0
_irq_helper:
eret
; mx251-function end schema=1 symbol=_irq_helper
; mx251-function begin schema=1 symbol=_dead isr=0 addressTaken=0 opaque=0 indirect=0
_dead:
ecall _dead_helper
eret
; mx251-function end schema=1 symbol=_dead
; mx251-function begin schema=1 symbol=_dead_helper isr=0 addressTaken=0 opaque=0 indirect=0
_dead_helper:
eret
; mx251-function end schema=1 symbol=_dead_helper
'@
$config=@{units=@(@{name='u0';file='fixture.c'});archives=@();link=@('u0','@startup')}
$cases=@(
 @{name='registered-vector';operand='0xff006b';base=[long]0xff0000;resolved=$true},
 @{name='decimal-vector';operand='16711787';base=[long]0xff0000;resolved=$true},
 @{name='relocated-code-base';operand='0xfe006b';base=[long]0xfe0000;resolved=$true},
 @{name='vector-interior';operand='0xff006c';base=[long]0xff0000;resolved=$false},
 @{name='unregistered-slot';operand='0xff0073';base=[long]0xff0000;resolved=$false},
 @{name='missing-plan';operand='0xff006b';base=[long]0xff0000;resolved=$false},
 @{name='archive-selection-pending';operand='0xff006b';base=[long]0xff0000;resolved=$false},
 @{name='wrong-code-base';operand='0xff006b';base=[long]0xfe0000;resolved=$false},
 @{name='not-an-isr';operand='0xff006b';base=[long]0xff0000;resolved=$false}
)
$results=@()
foreach($case in $cases){
 $root=Join-Path $Output $case.name;$null=New-Item -ItemType Directory -Path $root
 $asm=Join-Path $root 'u0.asm';$obj=Join-Path $root 'u0.rel'
 $text=$source
 if($case.name -eq 'not-an-isr'){$text=$text.Replace('symbol=_irq isr=1','symbol=_irq isr=0')}
 [IO.File]::WriteAllText($asm,$text)
 & (Join-Path $Core 'toolchain/bin/sdas251.exe') -plosgff $obj $asm
 if($LASTEXITCODE){throw 'Fixture assembly failed'}
 $raw=Join-Path $root 'vectors.asm'
 [IO.File]::WriteAllText($raw,".module vectors`n.area VECT (ABS,CODE)`n.org 0xff01d3`nljmp $($case.operand)`n")
 $plan=Get-Mx251InterruptSelection $root $config 0
 if($case.name -eq 'missing-plan'){$plan=$null}
 if($case.name -eq 'archive-selection-pending'){$plan=@{needsDiscovery=$true}}
 $result=Invoke-Mx251FunctionGc -Root $root -Units @(@{name='u0';assembly=$asm;object=$obj;language='c'},@{name='raw';assembly=$raw;language='asm'}) -InterruptPlan $plan -CodeBase $case.base
 $manifest=Get-Content -LiteralPath $result.Manifest -Raw|ConvertFrom-Json -AsHashtable
 if($result.Fallback -eq $case.resolved){throw "$($case.name): unexpected GC status"}
 if($case.resolved){
  if(($result.RemovedFunctions|Sort-Object) -join ',' -cne '_dead,_dead_helper'){throw 'Wrong functions removed'}
  if($manifest.resolvedAbsoluteTargets.Count -ne 1 -or $manifest.resolvedAbsoluteTargets[0].symbol -cne '_irq'){throw 'Missing absolute vector binding'}
 }elseif($result.RemovedFunctions.Count -or 'ABSOLUTE_CONTROL_TARGET' -notin $manifest.issues.code){throw 'Unresolved target changed collection'}
 $results+=@{case=$case.name;status='PASS';resolved=$case.resolved;removed=@($result.RemovedFunctions)}
}
[IO.File]::WriteAllText((Join-Path $Output 'result.json'),(ConvertTo-Json @{status='PASS';cases=$results} -Depth 10))
Write-Output ('PASS: '+$results.Count+' absolute-vector collection cases')
