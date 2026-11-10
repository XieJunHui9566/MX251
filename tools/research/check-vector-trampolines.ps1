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
$homeArea=Get-Mx251FormatHome (Join-Path $Baseline 'linked.partial.map')
$slots=[Collections.Generic.HashSet[int]]::new()
foreach($u in $config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.c'}){
 foreach($m in [regex]::Matches([IO.File]::ReadAllText((Join-Path $Baseline ($u.name+'.asm'))),'(?m)^; mx251-irq-definition schema=2 symbol=\S+ slot=([0-9]+) private=[01]\s*$')){$null=$slots.Add([int]$m.Groups[1].Value)}
}
if(Test-Path -LiteralPath $Output){throw 'Use a fresh test output directory'}
$file=Join-Path $Output $unit.file
[IO.Directory]::CreateDirectory((Split-Path -Parent $file))|Out-Null
Copy-Item -LiteralPath (Join-Path $Baseline ($unit.name+'.rel')) -Destination $Output
$original=[IO.File]::ReadAllText((Join-Path $Baseline $unit.file))
$results=@()
function Check([string]$Name,[string]$Text,[string]$Expected=''){
 [IO.File]::WriteAllText($file,$Text)
 $reason='';$value=$null
 try{$value=Get-Mx251FormatRawVectors $Output $unit $homeArea $slots}catch{$reason=$_.Exception.Message}
 if($reason -cne $Expected){throw "$Name expected '$Expected', got '$reason'"}
 $script:results+=@{case=$Name;status='PASS';reason=$reason}
 return $value
}
$chain=Check 'shared-trampoline' $original
if($chain.jumps.Count -ne 24 -or @($chain.jumps|Where-Object{$_.slot -ne 13}).Count){throw 'Unexpected resolved vector chain'}
$null=Check 'direct-vector' $original.Replace('ljmp 0xff01de','ljmp 0xff006b')
$null=Check 'cycle' $original.Replace('ljmp 0xff006b','ljmp 0xff01de') 'format-raw-vector-cycle'
$null=Check 'unregistered-terminal' $original.Replace('ljmp 0xff006b','ljmp 0xff006c') 'format-raw-jump-outside-registered-vector'
$null=Check 'unknown-code' ($original+"`nnop`n") 'format-unknown-raw-instruction'
$null=Check 'duplicate-source' $original.Replace('.area HOME (CODE)',(".org 0xff012b`nljmp 0xff006b`n.area HOME (CODE)")) 'format-raw-overlapping-jump'
$result=@{status='PASS';checks=$results;baseline=$Baseline;resolvedJumps=$chain.jumps.Count}
[IO.File]::WriteAllText((Join-Path $Output 'result.json'),(ConvertTo-Json $result -Depth 10))
Write-Host ('PASS: '+$results.Count+' vector-chain checks')
