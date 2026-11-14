param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
$null=[IO.Directory]::CreateDirectory($Output)
function Choice($Value,$Allowed,$Label){if($Value -cnotin $Allowed){throw "Unknown $Label"}}
function Keys-Only($Object,$Allowed,$Label){foreach($key in $Object.Keys){if($key -cnotin $Allowed){throw "Unknown $Label field"}}}
function Hash-File($Path){return (Get-FileHash -LiteralPath $Path).Hash.ToLowerInvariant()}
Copy-Item -LiteralPath (Join-Path $repo 'core/build/qualified/TargetProfile.ps1') -Destination $Output
$registry=Get-Content -LiteralPath (Join-Path $repo 'core/build/qualified/TargetContracts.json') -Raw|ConvertFrom-Json -AsHashtable
# A new device can inherit code generation without edits to a compiler switch.
$registry.targets['Independent251']=$registry.targets.STC32G12K128.Clone()
$registry.targets.Independent251.id='Independent251'
$registry.targets.Independent251.compilerCapabilities=@{}
$registry|ConvertTo-Json -Depth 30|Set-Content -LiteralPath (Join-Path $Output 'TargetContracts.json')
. (Join-Path $Output 'TargetProfile.ps1')
foreach($name in @('AI8051U-34K64-32','STC32G12K128','Independent251')){
 $target=Get-Mx251TargetContract $name
 if($target.architecture -cne 'mcs251-flat24-codeff' -or $target.interruptVectors.count -ne 0){throw 'Architecture policy not inherited'}
 $reads=Get-Mx251CodeReads $target
 if(-not $reads.enabled -or $reads.compilerArguments[0] -cne '--mcs251-flat-code-reads'){throw 'CODE read policy not inherited'}
 if($target.compilerCapabilities.objectAddressNative.contract -cne 'mcs251-flat24-codeff-spx-v1'){throw 'Address policy not inherited'}
}
if((Get-Mx251SfrLayout (Get-Mx251TargetContract 'Independent251')).contract -cne 'legacy-v1'){throw 'Device SFR layout leaked'}
. (Join-Path $repo 'core/build/qualified/InterruptVectors.ps1')
$config=@{archives=@();link=@('@startup','main','irq');units=@(@{name='main';file='main.c'},@{name='irq';file='irq.c'})}
[IO.File]::WriteAllText((Join-Path $Output 'main.asm'),"; mx251-irq-module schema=2 hasMain=1 vectorCount=0`n")
[IO.File]::WriteAllText((Join-Path $Output 'irq.asm'),"; mx251-irq-module schema=2 hasMain=0 vectorCount=0`n; mx251-irq-definition schema=2 symbol=_handler slot=61 private=0`n")
$plan=Get-Mx251InterruptSelection $Output $config 0 -MaximumSlots 62
if($plan.added.Count -ne 1 -or $plan.added[0].slot -ne 61){throw 'Cross-unit interrupt lost'}
$rejected=$false
try{$null=Get-Mx251InterruptSelection $Output $config 0 -MaximumSlots 61}catch{if($_.Exception.Message -notlike 'MX251-IRQ011:*'){throw};$rejected=$true}
if(-not $rejected){throw 'Physical interrupt limit lost'}
if((New-Mx251InterruptTable @() 0).Trim() -cne 'ljmp'+"`t"+'__sdcc_mcs251_reset_trampoline'){throw 'Empty program reserves unused vectors'}
Write-Output 'PASS 3 device identities, independent SFR layout, cross-unit ISR discovery and physical slot limit'
