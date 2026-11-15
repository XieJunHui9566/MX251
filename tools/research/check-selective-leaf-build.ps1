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
. (Join-Path $module 'FormatRuntime.ps1')
. (Join-Path $module 'SelectiveLeafAbi.ps1')
function Hash-File([string]$Path){return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
$config=Get-Content -LiteralPath (Join-Path $Baseline 'project.snapshot.json') -Raw|ConvertFrom-Json -AsHashtable
if(Test-Path -LiteralPath $Output){throw 'Use a fresh selector-test output directory'}
[IO.Directory]::CreateDirectory($Output)|Out-Null
foreach($u in $config.units){
 Copy-Item -LiteralPath (Join-Path $Baseline ($u.name+'.rel')) -Destination $Output
 $asm=if([IO.Path]::GetExtension($u.file) -ceq '.c'){$u.name+'.asm'}else{$u.file}
 $dest=Join-Path $Output $asm;[IO.Directory]::CreateDirectory((Split-Path -Parent $dest))|Out-Null
 Copy-Item -LiteralPath (Join-Path $Baseline $asm) -Destination $dest
}
Copy-Item -LiteralPath (Join-Path $Baseline 'startup.asm') -Destination $Output
Copy-Item -LiteralPath (Join-Path $Baseline 'linked.partial.map') -Destination $Output
$tool=Join-Path $Core 'toolchain';$profile=Join-Path $Core ('profiles/'+$config.target)
$results=@()
function Check([string]$Name,[int]$Expected,[string[]]$Keep=@()){
 $found=@(Get-Mx251LeafAbiCandidates $Output $config $tool $profile $Keep|Where-Object{$_.symbol -ceq $Symbol})
 if($found.Count -ne $Expected){throw "$Name expected $Expected candidates; got $($found.Count)"}
 $script:results+=@{case=$Name;status='PASS'}
 return $found
}
$chosen=@(Check 'closed-function-and-shared-source-calls' 1)[0]
$null=Check 'external-root' 0 @($Symbol)
$file=Join-Path $Output ($chosen.unit+'.asm');$original=[IO.File]::ReadAllText($file)
[IO.File]::AppendAllText($file,("`n.dw "+$Symbol+"`n"))
$null=Check 'address-reference' 0
[IO.File]::WriteAllText($file,$original)
$escaped=[regex]::Escape($Symbol)
foreach($u in $config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.c'}){
 $p=Join-Path $Output ($u.name+'.asm');$t=[IO.File]::ReadAllText($p)
 $t=[regex]::Replace($t,'(?m)^; mx251-transport-call schema=1 owner=\S+ target='+$escaped+' signature=\S+\s*$', '')
 [IO.File]::WriteAllText($p,$t)
}
$null=Check 'untyped-machine-call' 0
foreach($u in $config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.c'}){Copy-Item -LiteralPath (Join-Path $Baseline ($u.name+'.asm')) -Destination $Output -Force}
[IO.File]::WriteAllText($file,([regex]::Replace($original,'(?m)(^; mx251-function end schema=1 symbol='+$escaped+'\s*$)', ('ecall _unknown'+"`n"+'$1'))))
$null=Check 'non-leaf-with-outgoing-call' 1
[IO.File]::WriteAllText($file,$original)
[IO.File]::AppendAllText($file,("`necall "+$Symbol+"`n"))
$null=Check 'call-outside-typed-function' 0
[IO.File]::WriteAllText($file,$original.Replace('opaque=0','opaque=1'))
$null=Check 'opaque-entry' 0
[IO.File]::WriteAllText($file,$original)
$backend=$config.units[0].backend;$config.units[0].backend='legacy'
$null=Check 'mixed-unqualified-unit' 0
$config.units[0].backend=$backend
foreach($u in $config.units|Where-Object{[IO.Path]::GetExtension($_.file) -cne '.c'}){
 $p=Join-Path $Output $u.file;$raw=[IO.File]::ReadAllText($p)
 [IO.File]::AppendAllText($p,"`nret`n")
 $null=Check 'raw-code-is-not-a-vector' 0
 [IO.File]::WriteAllText($p,([regex]::Replace($raw,'ljmp\s+0x[0-9a-fA-F]+','ljmp 0xff1000')))
 $null=Check 'raw-unregistered-entrance' 0
 [IO.File]::WriteAllText($p,$raw)
 $null=Check 'registered-raw-vector' 1
}
$duplicate=$config.units|Where-Object{$_.name -ceq $chosen.unit}|Select-Object -First 1
$copy=@{};foreach($key in $duplicate.Keys){$copy[$key]=$duplicate[$key]};$copy.name='duplicate'
foreach($ext in @('asm','rel')){Copy-Item -LiteralPath (Join-Path $Output ($chosen.unit+'.'+$ext)) -Destination (Join-Path $Output ('duplicate.'+$ext))}
$config.units+=@($copy)
$null=Check 'duplicate-definition' 0
$result=@{status='PASS';checks=$results;baseline=[IO.Path]::GetFullPath($Baseline);symbol=$Symbol;calls=$chosen.calls}
[IO.File]::WriteAllText((Join-Path $Output 'result.json'),($result|ConvertTo-Json -Depth 10))
$result|ConvertTo-Json -Depth 10
