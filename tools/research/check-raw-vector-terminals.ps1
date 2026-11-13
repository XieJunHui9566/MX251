param(
 [Parameter(Mandatory=$true)][string]$Baseline,
 [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$module=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../core/build/qualified'))
. (Join-Path $module 'FunctionGc.ps1')
. (Join-Path $module 'FormatRuntime.ps1')
function Hash-File([string]$Path){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
$config=Get-Content -LiteralPath (Join-Path $Baseline 'project.snapshot.json') -Raw|ConvertFrom-Json -AsHashtable
$unit=@($config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.asm'})[0]
if(Test-Path -LiteralPath $Output){throw 'Use a fresh test output directory'}
$file=Join-Path $Output $unit.file
[IO.Directory]::CreateDirectory((Split-Path -Parent $file))|Out-Null
Copy-Item -LiteralPath (Join-Path $Baseline ($unit.name+'.rel')) -Destination $Output
$homeArea=@{base=[long]0xff0000;size=481}
$slots=[Collections.Generic.HashSet[int]]::new();$null=$slots.Add(4)
$script:results=@()
function Expect([string]$Name,[scriptblock]$Action,[string]$Expected=''){
 $reason='';$value=$null
 try{$value=& $Action}catch{$reason=$_.Exception.Message}
 if($reason -cne $Expected){throw "$Name expected '$Expected', got '$reason'"}
 $script:results+=@{case=$Name;status='PASS';reason=$reason}
 return $value
}
function Parse([string]$Body){
 [IO.File]::WriteAllText($file,".module vectors`n.area VECT (ABS,CODE)`n$Body`n.area HOME (CODE)`n.ds 481`n")
 return Get-Mx251FormatRawVectors $Output $unit $homeArea $slots
}
$c=Expect 'registered-c-chain' {Parse ".org 0xff012b`nljmp 0xff01de`n.org 0xff01de`nljmp 0xff0023"}
if($c.jumps.Count -ne 2 -or @($c.jumps|Where-Object{$_.slot -ne 4 -or $_.terminalKind -cne 'c-vector'}).Count){throw 'C chain resolution'}
$reti=Expect 'shared-reti-chain' {Parse ".org 0xff012b`nljmp 0xff0133`n.org 0xff0133`nljmp 0xff01de`n.org 0xff01de`nreti"}
if($reti.jumps.Count -ne 2 -or $reti.returns.Count -ne 1 -or @($reti.jumps|Where-Object{$_.target -ne 0xff01de -or $_.terminalKind -cne 'reti'}).Count){throw 'RETI chain resolution'}
$standalone=Expect 'standalone-reti' {Parse ".org 0xff012b`nreti"}
$null=Expect 'unregistered-original-target' {Parse ".org 0xff012b`nljmp 0xff006b"} 'format-raw-jump-outside-registered-vector'
$null=Expect 'cycle' {Parse ".org 0xff012b`nljmp 0xff0133`n.org 0xff0133`nljmp 0xff012b"} 'format-raw-vector-cycle'
$null=Expect 'return-outside-home' {Parse ".org 0xff01e1`nreti"} 'format-raw-return-outside-home'
$null=Expect 'jump-interior' {Parse ".org 0xff012b`nljmp 0xff012c"} 'format-raw-jump-outside-registered-vector'
$null=Expect 'overlapping-jumps' {Parse ".org 0xff012b`nljmp 0xff0023`n.org 0xff012c`nljmp 0xff0023"} 'format-raw-overlapping-jump'
$null=Expect 'return-overlaps-jump' {Parse ".org 0xff012b`nljmp 0xff0023`n.org 0xff012c`nreti"} 'format-raw-overlapping-return'
$null=Expect 'duplicate-return' {Parse ".org 0xff012b`nreti`n.org 0xff012b`nreti"} 'format-raw-overlapping-return'
$null=Expect 'ret-is-not-reti' {Parse ".org 0xff012b`nret"} 'format-unknown-raw-instruction'
function Make-Image($Raw){
 $img=@{}
 foreach($jump in $Raw.jumps){$img[$jump.source]=2;$img[($jump.source+1)]=($jump.encodedTarget -shr 8) -band 255;$img[($jump.source+2)]=$jump.encodedTarget -band 255
  if($jump.terminalKind -ceq 'c-vector'){$img[$jump.target]=138}
 }
 foreach($at in $Raw.returns){$img[$at]=50}
 return $img
}
foreach($raw in @($c,$reti,$standalone)){
 $img=Make-Image $raw
 $null=Expect 'unchanged-linked-instructions' {Assert-Mx251FormatRawVectorImages @($raw) $img $img}
}
$img=Make-Image $reti;$changed=$img.Clone();$changed[[long]0xff01de]=34
$null=Expect 'changed-reti-format' {Assert-Mx251FormatRawVectorImages @($reti) $img $changed} 'format-raw-reti-bytes-changed'
$null=Expect 'changed-reti-startup' {Assert-Mx251FormatRawVectorImages @($reti) $img $changed 'startup'} 'startup-raw-reti-bytes-changed'
$changed=$img.Clone();$changed.Remove([long]0xff01de)
$null=Expect 'missing-reti' {Assert-Mx251FormatRawVectorImages @($reti) $img $changed} 'format-raw-reti-bytes-changed'
$changed=$img.Clone();$changed[[long]0xff012d]=0
$null=Expect 'retargeted-jump' {Assert-Mx251FormatRawVectorImages @($reti) $img $changed} 'format-raw-jump-bytes-changed'
$img=Make-Image $c;$changed=$img.Clone();$changed[[long]0xff0023]=50
$null=Expect 'changed-c-vector' {Assert-Mx251FormatRawVectorImages @($c) $img $changed} 'format-raw-target-not-c-vector'
[IO.File]::WriteAllText((Join-Path $Output 'result.json'),(ConvertTo-Json @{status='PASS';checks=$results;baseline=$Baseline} -Depth 10))
Write-Host ('PASS: '+$results.Count+' raw-vector terminal checks')
