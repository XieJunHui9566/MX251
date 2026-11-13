param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
$null=New-Item -ItemType Directory -Path $OutputDirectory
$script:testRoot=[IO.Path]::GetFullPath($OutputDirectory)
function Hash-File([string]$Path){(Get-FileHash -LiteralPath $Path).Hash.ToLowerInvariant()}
function Flags {return @()}
function Check-Assembly {}
$compilerExe=Join-Path $testRoot 'compiler.identity'
$lockPath=Join-Path $testRoot 'profile.identity'
[IO.File]::WriteAllText($compilerExe,'synthetic compiler identity')
[IO.File]::WriteAllText($lockPath,'synthetic profile identity')
$script:helper=@'
.globl _helper
; mx251-function begin schema=1 symbol=_helper isr=0 addressTaken=0 opaque=0 indirect=0
; mx251-bank-contract schema=1 symbol=_helper abi=2 bank=0 naked=0
_helper:
 mov r7,#1
 eret
; mx251-function end schema=1 symbol=_helper
'@
$script:irq=@'
.globl _irq
; mx251-function begin schema=1 symbol=_irq isr=1 addressTaken=0 opaque=0 indirect=0
; mx251-bank-contract schema=1 symbol=_irq abi=2 bank=0 naked=0
_irq:
 ecall _helper
 reti
; mx251-function end schema=1 symbol=_irq
'@
[IO.File]::WriteAllText((Join-Path $testRoot 'u0.asm'),$irq+"`n")
[IO.File]::WriteAllText((Join-Path $testRoot 'u1.asm'),$helper+"`n")
[IO.File]::WriteAllText((Join-Path $testRoot 'u0.rel'),"XH3`nS _irq Def000000`nA REG_BANK_0 size 8 flags 4 addr 0`n")
[IO.File]::WriteAllText((Join-Path $testRoot 'u1.rel'),"XH3`nS _helper Def000000`nA REG_BANK_0 size 8 flags 4 addr 0`n")
$script:rebuilds=0
function Run-Tool {
 ++$script:rebuilds
 $facts=[IO.File]::ReadAllText((Join-Path $testRoot 'call-clobbers-u0.txt'))
 $m=[regex]::Match($facts,'global _helper 2 [0-9a-f]{8} ([0-3])')
 if(-not $m.Success){throw 'missing v3 scoped feedback'}
 $omitted=3 -bxor [int]$m.Groups[1].Value
 # Simulate a later backend choice that introduces a DPXL write while
 # leaving the GPR set unchanged. Actual-use validation must discover it.
 [IO.File]::WriteAllText((Join-Path $testRoot 'u1.asm'),$helper.Replace(' mov r7,#1'," mov r7,#1`n mov dpxl,#0x12")+"`n")
 $body=$irq.Replace('_irq:',"_irq:`n; mx251-isr-call callee=_helper omitted=00000000`n; mx251-isr-dpx-call callee=_helper omitted=$omitted")
 [IO.File]::WriteAllText((Join-Path $testRoot 'u0.asm'),$body+"`n")
}
$config=@{archives=@();units=@(
 @{name='u0';file='u0.c';backend='ordinary-wide';stackAuto=$true;defines=@()},
 @{name='u1';file='u1.c';backend='ordinary-wide';stackAuto=$true;defines=@()})}
$record=Invoke-Mx251CallClobberFeedback $testRoot $config @{} (Hash-File $compilerExe) (Hash-File $lockPath) -ScopedLocals
if(-not $record.finalValidated -or $record.fallback -or $rebuilds -ne 2){throw ($record|ConvertTo-Json -Depth 12)}
if($record.attempts[0].violations.Count -ne 1 -or $record.attempts[1].violations.Count){throw 'DPXL bound expansion was not repaired'}
$use=@($record.actualUses|Where-Object kind -eq 'isr-dpx')
if($use.Count -ne 1 -or $use[0].removedDpx -ne 1 -or $use[0].finalCalleeDpxWrites -ne 2){throw 'wrong final DPX preservation'}
$record|ConvertTo-Json -Depth 15|Set-Content (Join-Path $testRoot 'result.json') -Encoding utf8
Write-Output 'PASS: introduced DPXL write triggers one repair; unaffected DPTR saving survives'
