# Build the portable MCS-251 ABI4 integer runtime from its assembly sources.
param([Parameter(Mandatory)][string]$ToolchainRoot,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [string]$SourceDirectory=(Join-Path $PSScriptRoot '../sources/compiler/device/lib/mcs251/native-int'))
$ErrorActionPreference='Stop'
$ToolchainRoot=[IO.Path]::GetFullPath($ToolchainRoot)
$SourceDirectory=[IO.Path]::GetFullPath($SourceDirectory)
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$null=New-Item -ItemType Directory -Path $OutputDirectory
$members=@('_divslong','_divulong','_modslong','_modulong','_mullong')
$sources=@{}
Push-Location $OutputDirectory
try {
 foreach($name in $members){
  $source=Join-Path $SourceDirectory ($name+'.asm')
  Copy-Item -LiteralPath $source ($name+'.asm')
  & (Join-Path $ToolchainRoot 'bin/sdas251.exe') -plosgff ($name+'.rel') ($name+'.asm') *> ($name+'.log')
  if($LASTEXITCODE){throw "Assembling $name failed"}
  $sources[$name]=(Get-FileHash $source).Hash.ToLowerInvariant()
 }
 & (Join-Path $ToolchainRoot 'bin/sdar.exe') -rcsD libnativeint.lib @($members|ForEach-Object{$_+'.rel'}) *> archive.log
 if($LASTEXITCODE){throw 'Archiving native integer runtime failed'}
 $record=@{schema=1;abi=4;sources=$sources;archiveSha256=(Get-FileHash 'libnativeint.lib').Hash.ToLowerInvariant()}
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'build.json'),((ConvertTo-Json $record -Depth 5)+"`n"),[Text.UTF8Encoding]::new($false))
 Write-Output (Join-Path $OutputDirectory 'libnativeint.lib')
} finally {Pop-Location}
