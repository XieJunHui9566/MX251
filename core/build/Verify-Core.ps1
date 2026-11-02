#requires -Version 7.0
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$core=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
function Hash([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Local([string]$Path){
 if([IO.Path]::IsPathRooted($Path) -or $Path.Contains(':') -or $Path -match '(^|[\\/])\.\.?([\\/]|$)'){throw "Unsafe core-relative path: $Path"}
 $full=[IO.Path]::GetFullPath((Join-Path $core $Path))
 if(-not $full.StartsWith($core+[IO.Path]::DirectorySeparatorChar,[StringComparison]::OrdinalIgnoreCase)){throw 'Core path escapes root'}
 $full
}
$manifest=Get-Content -LiteralPath (Local 'core.json') -Raw -Encoding UTF8|ConvertFrom-Json -AsHashtable
if($null -eq $manifest.compiler){throw 'PREPARING: compiler is not bound'}
$count=0
$deps=Get-Content -LiteralPath (Local 'sources/dependency-files.json') -Raw -Encoding UTF8|ConvertFrom-Json -AsHashtable
foreach($item in $deps.files){if((Hash (Local $item.path)) -cne $item.sha256){throw "Dependency mismatch: $($item.path)"};$count++}
$sourceManifest=Local 'sources/compiler-files.json'
$sourceCount=0
if(-not (Test-Path -LiteralPath $sourceManifest)){throw 'Compiler source manifest missing'}
$sources=Get-Content -LiteralPath $sourceManifest -Raw -Encoding UTF8|ConvertFrom-Json -AsHashtable
foreach($item in $sources.files){if((Hash (Local $item.path)) -cne $item.sha256){throw "Compiler source mismatch: $($item.path)"};$sourceCount++}
if((Hash (Local $manifest.compiler.path)) -cne $manifest.compiler.sha256){throw 'Compiler mismatch'}
foreach($profile in $manifest.profiles){
 $lock=Get-Content -LiteralPath (Local ($profile+'/profile.lock.json')) -Raw -Encoding UTF8|ConvertFrom-Json -AsHashtable
 if($lock.mx251Version -cne $manifest.sourceVersion){throw 'MX251 source version mismatch'}
 if($lock.compilerOverlay.sha256 -cne $manifest.compiler.sha256){throw 'Profile compiler mismatch'}
 if((Hash (Local 'build/qualified/TargetContracts.json')) -cne $lock.targetContract.registrySha256){throw 'Target registry mismatch'}
 foreach($item in $lock.toolchain){if((Hash (Local ('toolchain/'+$item.path))) -cne $item.sha256){throw "Tool mismatch: $($item.path)"};$count++}
 foreach($item in $lock.profile){if((Hash (Local ($profile+'/'+$item.path))) -cne $item.sha256){throw "Profile mismatch: $($item.path)"};$count++}
}
[pscustomobject]@{status='CONTENT_LOCKS_PASS';filesChecked=$count;compilerSourceFilesChecked=$sourceCount;compilerSha256=$manifest.compiler.sha256;coreStatus=$manifest.status;hardwareQualificationGranted=$false}|ConvertTo-Json
