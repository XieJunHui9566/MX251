param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $repo 'core/build/qualified/FormatRuntime.ps1')
. (Join-Path $repo 'core/build/qualified/ExtendedFormat.ps1')
. (Join-Path $repo 'core/build/qualified/IndirectFormat.ps1')
function Hash-File([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
$null=New-Item -ItemType Directory -Path $Output
$wide="mov a,#((_fmt+0)>>16)`npush acc`npushw #(_fmt+0)"
$legacy="mov a,#(_fmt >> 16)`npush acc`npushw #_fmt`nmov a,#_fmt"
foreach($setup in @($wide,$legacy)) {
 $r=Read-Mx251FormatArgument $setup
 if($r.bytes -ne 3 -or $r.constantSymbol -cne '_fmt'){throw 'constant argument recognition failed'}
}
foreach($setup in @($wide.Replace('+0','+1'),$wide.Replace('pushw #(_fmt+0)','pushw #(_other+0)'))) {
 if((Read-Mx251FormatArgument $setup).constantSymbol){throw 'nonliteral address accepted'}
}
$body=@'
.area CSEG
; mx251-function begin schema=1 symbol=_caller isr=0 addressTaken=0 opaque=0 indirect=0
_caller:
; MX251 argument region begin
pushw #0x1234
; MX251 argument region end
; MX251 argument region begin
mov a,#((_fmt+0)>>16)
push acc
pushw #(_fmt+0)
; MX251 argument region end
ecall _printf
sub spx,#5
eret
; mx251-bank-contract schema=1 symbol=_caller abi=2 bank=0 naked=0
; mx251-function end schema=1 symbol=_caller
.area CONST
; mx251-string-literal schema=1 symbol=_fmt bytes=2530347800
'@
$config=@{runtime='standard';explicitAbi=$false;archives=@();units=@(@{file='u0.c';name='u0';stackAuto=$true;backend='ordinary-wide'})}
[IO.File]::WriteAllText((Join-Path $Output 'linked.partial.map'),"HOME 00000000 00000003 = test`n")
[IO.File]::WriteAllText((Join-Path $Output 'u0.asm'),$body)
[IO.File]::WriteAllBytes((Join-Path $Output 'u0.rel'),[byte[]]@(0))
$d=Get-Mx251FormatDemand $Output $config
if($d.calls.Count -ne 1 -or ($d.calls[0].argumentBytes -join ',') -cne '2,3'){throw 'late bank contract or slot recognition failed'}
$rejected=@()
foreach($change in @(@('abi=2 bank=0','abi=4 bank=0'),@('pushw #0x1234','push dr4'),@('_fmt+0','_fmt+1'))) {
 [IO.File]::WriteAllText((Join-Path $Output 'u0.asm'),$body.Replace($change[0],$change[1]))
 try{$null=Get-Mx251FormatDemand $Output $config;$accepted=$true}catch{$accepted=$false;$rejected+=$_.Exception.Message}
 if($accepted){throw 'mismatched ABI, width or literal accepted'}
}
[IO.File]::WriteAllText((Join-Path $Output 'u0.asm'),$body)
@{status='PASS';argumentEncodings=2;lateBankContract=$true;rejected=$rejected}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $Output 'result.json') -Encoding utf8
Write-Output 'PASS: legacy/wide symbolic arguments, late bank contract, and format slot matching'
