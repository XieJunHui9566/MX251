param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $repo 'core/build/qualified/FormatRuntime.ps1')
. (Join-Path $repo 'core/build/qualified/ExtendedFormat.ps1')
. (Join-Path $repo 'core/build/qualified/IndirectFormat.ps1')
function Hash-File([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
$null=New-Item -ItemType Directory -Path $Output
$constants=Read-Mx251FormatConstants @('.area CONST','; mx251-string-literal schema=1 symbol=_empty bytes=00','; mx251-string-literal schema=1 symbol=_byte bytes=ff00')
if($constants['_empty'] -cne '' -or [int]$constants['_byte'][0] -ne 255){throw 'empty or Latin-1 literal decoding failed'}
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
foreach($runtime in @('native-int-mdu32','native-math-mdu32')) {
 $config.runtime=$runtime
 if((Get-Mx251FormatDemand $Output $config).calls.Count -ne 1){throw 'MDU format demand changed'}
}
$config.runtime='standard'
$rejected=@()
foreach($change in @(@('abi=2 bank=0','abi=4 bank=0'),@('pushw #0x1234','push dr4'),@('_fmt+0','_fmt+1'))) {
 [IO.File]::WriteAllText((Join-Path $Output 'u0.asm'),$body.Replace($change[0],$change[1]))
 try{$null=Get-Mx251FormatDemand $Output $config;$accepted=$true}catch{$accepted=$false;$rejected+=$_.Exception.Message}
 if($accepted){throw 'mismatched ABI, width or literal accepted'}
}
[IO.File]::WriteAllText((Join-Path $Output 'u0.asm'),$body)
 $initializer=@('.module constants','.area DSEG (DATA)','_value::','.ds 2','.area GSINIT (CODE)','mov (_value + 1),#0x12','mov _value,#0x34')
 if(-not (Test-Mx251FormatInitializerUnit $initializer)){throw 'initializer-only unit rejected'}
 foreach($instruction in @('ecall _helper','.db 0x12,0,0','ejmp _elsewhere','mov _value,r7')){
  if(Test-Mx251FormatInitializerUnit ($initializer+@($instruction))){throw 'nonliteral initializer accepted'}
 }
 [IO.File]::WriteAllText((Join-Path $Output 'u1.asm'),($initializer -join "`n"))
 [IO.File]::WriteAllBytes((Join-Path $Output 'u1.rel'),[byte[]]@(0))
 $config.units+=@(@{file='u1.c';name='u1';stackAuto=$true;backend='ordinary-wide'})
 if((Get-Mx251FormatDemand $Output $config).calls.Count -ne 1){throw 'initializer-only unit changed format demand'}
 $edata=@('.module constants','.area ESEG (DATA)','_value::','.ds 2',
  '.area GSINIT (CODE)','mov a,#0x12','mov dptr,#(_value + 1)',
  'mov dpxl,#((_value + 1) >> 16)','mov @dpx,a',
  'mov a,#0x34','mov dptr,#_value','mov dpxl,#(_value >> 16)','mov @dpx,a',
  '.area GSINIT3 (CODE)','mov dptr,#_value','mov dpxl,#0','mov dr12,#2','clr a',
  '__mcs251_edata_group_clear:','mov @dpx,a','inc dpx','dec dr12','jne __mcs251_edata_group_clear')
 if(-not (Test-Mx251FormatInitializerUnit $edata)){throw 'EDATA initializer-only unit rejected'}
 [IO.File]::WriteAllText((Join-Path $Output 'u1.asm'),($edata -join "`n"))
 if((Get-Mx251FormatDemand $Output $config).calls.Count -ne 1){throw 'EDATA initialization changed format demand'}
 $initializerRejected=0
 foreach($change in @(@('jne __mcs251_edata_group_clear','jne _elsewhere'),
  @('mov @dpx,a','ecall _helper'),@('mov dpxl,#(_value >> 16)','mov dpxl,#(_other >> 16)'),
  @('.area GSINIT (CODE)','.area CSEG (CODE)'),@('dec dr12','.db 0x12,0,0'))){
  $changed=($edata -join "`n").Replace($change[0],$change[1]) -split "`n"
  if(Test-Mx251FormatInitializerUnit $changed){throw 'non-initialization sequence accepted'}
  $initializerRejected++
 }
@{status='PASS';argumentEncodings=2;lateBankContract=$true;edataInitializer=$true;initializerRejected=$initializerRejected;rejected=$rejected}|ConvertTo-Json -Depth 4|Set-Content -LiteralPath (Join-Path $Output 'result.json') -Encoding utf8
Write-Output 'PASS: legacy/wide symbolic arguments, late bank contract, and format slot matching'
