#requires -Version 7.0
param(
 [Parameter(Mandatory)][string]$BuildRoot,
 [Parameter(Mandatory)][string]$Project,
 [Parameter(Mandatory)][string]$ToolRoot,
 [Parameter(Mandatory)][string]$Output
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
function Hash-File($Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
$qualified=Join-Path $PSScriptRoot '../../../../../build/qualified'
. (Join-Path $qualified 'FunctionGc.ps1')
. (Join-Path $qualified 'ScalarParameters.ps1')
$config=Get-Content -LiteralPath $Project -Raw|ConvertFrom-Json -AsHashtable
if(Test-Path -LiteralPath $Output){throw 'Use a new output directory'}
$null=New-Item -ItemType Directory -Path $Output
$rows=@()
foreach($case in @('removed','machine-call','typed-call','data-reference','startup-reference','address-taken')){
 $root=Join-Path $Output $case;$null=New-Item -ItemType Directory -Path $root
 # Inspect the actual specialized objects before function GC removes sc_dead.
 foreach($u in $config.units){
  Copy-Item -LiteralPath (Join-Path $BuildRoot ('function-gc/original/'+$u.name+'.asm')) -Destination $root
  Copy-Item -LiteralPath (Join-Path $BuildRoot ($u.name+'.rel')) -Destination $root
 }
 Copy-Item -LiteralPath (Join-Path $BuildRoot 'startup.asm') -Destination $root
 $callee=Join-Path $root 'u1.asm';$text=[IO.File]::ReadAllText($callee)
 switch($case){
  'machine-call' {$text+="`necall _sc_dead`n"}
  'typed-call' {$text+="`n; mx251-scalar-call schema=1 owner=_sc_prune target=_sc_dead signature=u32(u16) values=00000001`necall _sc_dead`n"}
  'data-reference' {$text+="`n.dw _sc_dead`n"}
  'startup-reference' {[IO.File]::AppendAllText((Join-Path $root 'startup.asm'),"`necall _sc_dead`n")}
  'address-taken' {$text=$text.Replace('symbol=_sc_dead isr=0 addressTaken=0','symbol=_sc_dead isr=0 addressTaken=1')}
 }
 [IO.File]::WriteAllText($callee,$text)
 $facts=Get-Mx251ScalarFacts $root $config $ToolRoot
 $unreferenced='_sc_dead' -cin $facts.unreferenced
 if($unreferenced -ne ($case -ceq 'removed')){throw "Wrong entry classification: $case"}
 $rows+=@{case=$case;unreferenced=$unreferenced}
}
@{status='PASS';cases=$rows}|ConvertTo-Json -Depth 10|Set-Content -LiteralPath (Join-Path $Output 'result.json')
Write-Output 'PASS: removed call plus machine, typed, data, startup and address-taken entries'
