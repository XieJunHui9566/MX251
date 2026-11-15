# Exercise the production selector using compiled multi-TU fixture artifacts.
param(
 [Parameter(Mandatory=$true)][string]$Baseline,
 [Parameter(Mandatory=$true)][string]$Core,
 [Parameter(Mandatory=$true)][string]$Output
)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$module=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../core/build/qualified'))
. (Join-Path $module 'FunctionGc.ps1')
. (Join-Path $module 'FormatRuntime.ps1')
$config=Get-Content -LiteralPath (Join-Path $Baseline 'project.snapshot.json') -Raw|ConvertFrom-Json -AsHashtable
$tool=Join-Path $Core 'toolchain';$profile=Join-Path $Core ('profiles/'+$config.target)
function Describe($Candidates){
 @($Candidates|ForEach-Object{$_.unit+'|'+$_.symbol+'|'+$_.scope+'|'+$_.signature+'|'+$_.calls+'|'+$_.rank+'|'+(@($_.units|Sort-Object) -join ',')}|Sort-Object) -join "`n"
}
# The staged selector predates the single-pass token index. Compare actual decisions.
. (Join-Path $Core 'build/qualified/CallTransportGroup.ps1')
$watch=[Diagnostics.Stopwatch]::StartNew()
$staged=@(Get-Mx251LeafAbiCandidates $Baseline $config $tool $profile)
$stagedMs=$watch.ElapsedMilliseconds
. (Join-Path $module 'CallTransportGroup.ps1')
$watch.Restart()
$current=@(Get-Mx251LeafAbiCandidates $Baseline $config $tool $profile)
$currentMs=$watch.ElapsedMilliseconds
if((Describe $staged) -cne (Describe $current)){throw 'indexed selector changed candidate decisions'}
if(Test-Path -LiteralPath $Output){throw 'Use a fresh output directory'}
[IO.Directory]::CreateDirectory($Output)|Out-Null
foreach($u in $config.units){foreach($ext in @('asm','rel')){Copy-Item -LiteralPath (Join-Path $Baseline ($u.name+'.'+$ext)) -Destination $Output}}
foreach($f in @('startup.asm','linked.partial.map')){Copy-Item -LiteralPath (Join-Path $Baseline $f) -Destination $Output}
$results=@(@{case='indexed-selector-equivalence';status='PASS';stagedMs=$stagedMs;indexedMs=$currentMs})
function Check([string]$Name,[string[]]$Expected,[string[]]$Keep=@()){
 $found=@(Get-Mx251LeafAbiCandidates $Output $config $tool $profile $Keep|ForEach-Object{$_.unit+':'+$_.symbol}|Sort-Object)
 if(($found -join ',') -cne (@($Expected|Sort-Object) -join ',')){throw "$Name got $($found -join ',')"}
 $script:results+=@{case=$Name;status='PASS'}
}
$all=@('u1:_take','u2:_take','u1:_take_code','u2:_transport_put','u4:_send')
Check 'static-scopes-code-pointer-global-and-escaped-callback' $all
Check 'external-root' @($all|Where-Object{$_ -cne 'u2:_transport_put'}) @('_transport_put')
$left=Join-Path $Output 'u1.asm';$original=[IO.File]::ReadAllText($left)
[IO.File]::AppendAllText($left,"`n.dw _take`n")
Check 'address-reference-only-excludes-own-static-scope' @($all|Where-Object{$_ -cne 'u1:_take'})
[IO.File]::WriteAllText($left,([regex]::Replace($original,'(?m)^; mx251-transport-call schema=1 owner=\S+ target=_take signature=\S+\s*$', '')))
Check 'missing-call-type-only-excludes-own-static-scope' @($all|Where-Object{$_ -cne 'u1:_take'})
[IO.File]::WriteAllText($left,([regex]::Replace($original,'(?m)(^; mx251-transport-call schema=1 owner=\S+ target=_take signature=)\S+', '${1}u16(pG(cu8),u8)')))
Check 'mismatched-call-prototype' @($all|Where-Object{$_ -cne 'u1:_take'})
[IO.File]::WriteAllText($left,([regex]::Replace($original,'(?m)(^; mx251-function begin schema=1 symbol=_take .*?)opaque=0', '${1}opaque=1')))
Check 'opaque-candidate-does-not-disable-other-functions' @($all|Where-Object{$_ -cne 'u1:_take'})
[IO.File]::WriteAllText($left,$original)
# Duplicate the global definition, retaining an independent scope for its private functions.
$duplicate=@{};foreach($key in $config.units[1].Keys){$duplicate[$key]=$config.units[1][$key]}
$duplicate.name='duplicate';$duplicate.file='duplicate.c';$config.units+=@($duplicate)
foreach($ext in @('asm','rel')){Copy-Item -LiteralPath (Join-Path $Output ('u2.'+$ext)) -Destination (Join-Path $Output ('duplicate.'+$ext))}
Check 'duplicate-global-does-not-conflate-private-functions' (@($all|Where-Object{$_ -cne 'u2:_transport_put'})+@('duplicate:_take'))
$result=@{status='PASS';checks=$results;baseline=[IO.Path]::GetFullPath($Baseline);candidates=$current}
[IO.File]::WriteAllText((Join-Path $Output 'result.json'),($result|ConvertTo-Json -Depth 12))
$result|ConvertTo-Json -Depth 12
