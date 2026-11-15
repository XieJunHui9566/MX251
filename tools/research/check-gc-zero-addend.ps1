param(
 [Parameter(Mandatory=$true)][string]$Core,
 [Parameter(Mandatory=$true)][string]$Output,
 [string]$Module=(Join-Path $PSScriptRoot '../../core/build/qualified/FunctionGc.ps1')
)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
. $Module
if(Test-Path -LiteralPath $Output){throw 'Use a fresh test output directory'}
$null=New-Item -ItemType Directory -Path $Output
$results=@()
foreach($case in @(
 @{name='entry';expression='_callback';collect=$true},
 @{name='zero-low';expression='(_callback+0)';collect=$true},
 @{name='zero-high';expression='((_callback+0)>>16)';collect=$true},
 @{name='nonzero';expression='(_callback+1)';collect=$false},
 @{name='nonzero-hex';expression='(_callback+0x10)';collect=$false},
 @{name='nested-nonzero';expression='((_callback+0)+1)';collect=$false}
)){
 $root=Join-Path $Output $case.name;$null=New-Item -ItemType Directory -Path $root
 $asm=Join-Path $root 'u0.asm';$obj=Join-Path $root 'u0.rel'
 $text=@"
.module fixture
.area HOME (CODE)
ecall _main
.area CSEG (CODE)
; mx251-function begin schema=1 symbol=_main isr=0 addressTaken=0 opaque=0 indirect=0
_main:
mov wr18,#$($case.expression)
eret
; mx251-function end schema=1 symbol=_main
; mx251-function begin schema=1 symbol=_callback isr=0 addressTaken=0 opaque=0 indirect=0
_callback:
mov wr6,#0x1234
eret
; mx251-function end schema=1 symbol=_callback
; mx251-function begin schema=1 symbol=_dead isr=0 addressTaken=0 opaque=0 indirect=0
_dead:
mov wr6,#0x5678
eret
; mx251-function end schema=1 symbol=_dead
"@
 [IO.File]::WriteAllText($asm,$text)
 & (Join-Path $Core 'toolchain/bin/sdas251.exe') -plosgff $obj $asm
 if($LASTEXITCODE){throw 'Fixture assembly failed'}
 $result=Invoke-Mx251FunctionGc -Root $root -Units @(@{name='u0';assembly=$asm;object=$obj;language='c'})
 $manifest=Get-Content -LiteralPath $result.Manifest -Raw|ConvertFrom-Json -AsHashtable
 if($result.Fallback -eq $case.collect){throw "$($case.name): unexpected collection status"}
 if($case.collect){
  if(($result.RemovedFunctions -join ',') -cne '_dead'){throw 'Callback reachability changed'}
  $candidate=[IO.File]::ReadAllText($result.ChangedUnits[0].candidateAssembly)
  if(-not $candidate.Contains("mov wr18,#$($case.expression)") -or -not $candidate.Contains('_callback:')){throw 'Live relocation or callback was changed'}
 }elseif($result.RemovedFunctions.Count -or -not @($manifest.issues|Where-Object{$_.code -eq 'FUNCTION_ADDRESS_ARITHMETIC'}).Count){throw 'Nonzero addend was accepted'}
 $results+=@{case=$case.name;status='PASS';removed=@($result.RemovedFunctions)}
}
[IO.File]::WriteAllText((Join-Path $Output 'report.json'),(ConvertTo-Json -Depth 20 -InputObject @{status='PASS';cases=$results}))
$results|ConvertTo-Json -Depth 10
