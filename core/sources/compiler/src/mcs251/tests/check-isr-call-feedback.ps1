param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
$null=New-Item -ItemType Directory -Path $OutputDirectory
$rel="XH3`nS _irq Def000000`nS _helper Def000010`nA REG_BANK_0 size 8 flags 4 addr 0`n"
$asm=@'
.globl _irq
.globl _helper
; mx251-function begin schema=1 symbol=_irq isr=1 addressTaken=0 opaque=0 indirect=0
; mx251-bank-contract schema=1 symbol=_irq abi=2 bank=0 naked=0
_irq:
 push psw
 mov psw,#0
 ecall _helper
 pop psw
 reti
; mx251-function end schema=1 symbol=_irq
; mx251-function begin schema=1 symbol=_helper isr=0 addressTaken=0 opaque=0 indirect=0
; mx251-bank-contract schema=1 symbol=_helper abi=2 bank=0 naked=0
_helper:
 mov r7,dpl
 eret
; mx251-function end schema=1 symbol=_helper
'@
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'isr.rel'),$rel)
[IO.File]::WriteAllText((Join-Path $OutputDirectory 'isr.asm'),$asm+"`n")
$scan=Get-Mx251BankFunctions $OutputDirectory @{units=@(@{name='isr';file='isr.c'})}
$irq=$scan.symbols['_irq'][0];$helper=$scan.symbols['_helper'][0]
if('_helper' -cnotin $irq.calls){throw 'The ISR call must participate in feedback discovery'}
if($irq.abi -ne 0 -or $irq.writes -ne 4294967295L){throw 'An ISR must not become a normal callee contract'}
if($helper.abi -ne 2 -or $helper.writes -ne 128){throw 'The ordinary helper must retain its actual register effects'}
@{status='PASS';calls=$irq.calls;isrAbi=$irq.abi;isrWrites=$irq.writes;helperWrites=$helper.writes} |
 ConvertTo-Json | Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
Write-Output 'PASS: ISR call discovery without exporting an ISR callee contract'
