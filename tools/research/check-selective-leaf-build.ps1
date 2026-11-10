# Exercise the production selector against actual compiled assembly/REL inputs.
param(
 [Parameter(Mandatory=$true)][string]$Baseline,
 [Parameter(Mandatory=$true)][string]$Core,
 [Parameter(Mandatory=$true)][string]$Output,
 [Parameter(Mandatory=$true)][string]$Symbol
)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$module=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../core/build/qualified'))
. (Join-Path $module 'FunctionGc.ps1')
. (Join-Path $module 'SelectiveLeafAbi.ps1')
$config=Get-Content -LiteralPath (Join-Path $Baseline 'project.snapshot.json') -Raw|ConvertFrom-Json -AsHashtable
if(Test-Path -LiteralPath $Output){throw 'Use a fresh selector-test output directory'}
[IO.Directory]::CreateDirectory($Output)|Out-Null
foreach($u in $config.units){foreach($ext in @('asm','rel')){Copy-Item -LiteralPath (Join-Path $Baseline ($u.name+'.'+$ext)) -Destination $Output}}
Copy-Item -LiteralPath (Join-Path $Baseline 'startup.asm') -Destination $Output
$tool=Join-Path $Core 'toolchain';$profile=Join-Path $Core ('profiles/'+$config.target)
$results=@()
function Check([string]$Name,[int]$Expected,[string[]]$Keep=@()){
 $found=@(Get-Mx251LeafAbiCandidates $Output $config $tool $profile $Keep|Where-Object{$_.symbol -ceq $Symbol})
 if($found.Count -ne $Expected){throw "$Name expected $Expected candidates; got $($found.Count)"}
 $script:results+=@{case=$Name;status='PASS'}
 return $found
}
$chosen=@(Check 'closed-leaf-and-shared-source-calls' 1)[0]
$null=Check 'external-root' 0 @($Symbol)
$file=Join-Path $Output ($chosen.unit+'.asm');$original=[IO.File]::ReadAllText($file)
[IO.File]::AppendAllText($file,("`n.dw "+$Symbol+"`n"))
$null=Check 'address-reference' 0
[IO.File]::WriteAllText($file,$original)
$escaped=[regex]::Escape($Symbol)
foreach($u in $config.units){
 $p=Join-Path $Output ($u.name+'.asm');$t=[IO.File]::ReadAllText($p)
 $t=[regex]::Replace($t,'(?m)^; mx251-scalar-call schema=1 owner=\S+ target='+$escaped+' signature=\S+ values=\S+\s*$', '')
 [IO.File]::WriteAllText($p,$t)
}
$null=Check 'untyped-machine-call' 0
foreach($u in $config.units){Copy-Item -LiteralPath (Join-Path $Baseline ($u.name+'.asm')) -Destination $Output -Force}
[IO.File]::WriteAllText($file,([regex]::Replace($original,'(?m)(^; mx251-function end schema=1 symbol='+$escaped+'\s*$)', ('ecall _unknown'+"`n"+'$1'))))
$null=Check 'non-leaf' 0
[IO.File]::WriteAllText($file,$original)
[IO.File]::AppendAllText($file,("`necall "+$Symbol+"`n"))
$null=Check 'call-outside-typed-function' 0
[IO.File]::WriteAllText($file,$original.Replace('opaque=0','opaque=1'))
$null=Check 'opaque-entry' 0
[IO.File]::WriteAllText($file,$original)
$backend=$config.units[0].backend;$config.units[0].backend='legacy'
$null=Check 'mixed-unqualified-unit' 0
$config.units[0].backend=$backend
$duplicate=$config.units|Where-Object{$_.name -ceq $chosen.unit}|Select-Object -First 1
$copy=@{};foreach($key in $duplicate.Keys){$copy[$key]=$duplicate[$key]};$copy.name='duplicate'
foreach($ext in @('asm','rel')){Copy-Item -LiteralPath (Join-Path $Output ($chosen.unit+'.'+$ext)) -Destination (Join-Path $Output ('duplicate.'+$ext))}
$config.units+=@($copy)
$null=Check 'duplicate-definition' 0
$result=@{status='PASS';checks=$results;baseline=[IO.Path]::GetFullPath($Baseline);symbol=$Symbol;calls=$chosen.calls}
[IO.File]::WriteAllText((Join-Path $Output 'result.json'),($result|ConvertTo-Json -Depth 10))
$result|ConvertTo-Json -Depth 10
