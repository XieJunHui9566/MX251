# Reassemble runtime sources and compare exact REL members before deriving
# register effects. Run when the library changes, not on every app build.
param([Parameter(Mandatory)][string]$Archive,
 [Parameter(Mandatory)][string]$SourceDirectory,
 [Parameter(Mandatory)][string]$Assembler,
 [Parameter(Mandatory)][string]$OutputDirectory,
 [string]$ArchiveRelativePath='runtime/libnativeint.lib',
 [ValidateSet(2,4)][int]$Abi=4)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../..'))
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
. (Join-Path $repo 'core/build/qualified/StartupArchive.ps1')
function Get-Mx251StartupDigest([byte[]]$Data){return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Data)).ToLowerInvariant()}
$Archive=[IO.Path]::GetFullPath($Archive);$SourceDirectory=[IO.Path]::GetFullPath($SourceDirectory)
$Assembler=[IO.Path]::GetFullPath($Assembler);$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$null=New-Item -ItemType Directory -Path $OutputDirectory -ErrorAction Stop
$functions=@()
Push-Location $OutputDirectory
try {
 foreach($member in @(Read-Mx251StartupArchive $Archive)) {
  $name=[IO.Path]::GetFileNameWithoutExtension($member.name)
  $source=Join-Path $SourceDirectory ($name+'.asm');Copy-Item -LiteralPath $source ($name+'.asm')
  & $Assembler -plosgff ($name+'.rel') ($name+'.asm') *> ($name+'.log')
  if($LASTEXITCODE -or (Get-FileHash ($name+'.rel')).Hash.ToLowerInvariant() -cne $member.sha256){throw "Runtime member differs from source: $name"}
  $rel=[Text.Encoding]::ASCII.GetString($member.data)
  $exports=@([regex]::Matches($rel,'(?m)^S ([^\s]+) Def[0-9A-Fa-f]+\s*$')|ForEach-Object{$_.Groups[1].Value}|Where-Object{$_ -cne '.__.ABS.'})
  if($exports.Count -ne 1){throw 'Expected one function per runtime member'}
  $asm=[IO.File]::ReadAllText($source)
  $body=($asm.Split("`n")|ForEach-Object{
   $code=($_ -split ';',2)[0].Trim()
   if($code.StartsWith('.')){
    if($code -cnotmatch '^\.(?:module|globl)\s+\S+$' -and $code -cnotmatch '^\.area\s+CSEG\s+\(CODE\)$'){throw "Unsupported runtime directive: $code"}
   }else{$_}
  }) -join "`n"
  $analysis=Get-Mx251BankBody $body (New-Mx251OrdinalMap)
  if($analysis.reason){throw ($name+': '+$analysis.reason+' '+$analysis.failureLine)}
  $functions+=@{symbol=$exports[0];member=$member.name;module=([regex]::Match($rel,'(?m)^M (\S+)').Groups[1].Value);
   abi=$Abi;localWrites=('{0:x8}' -f [long]$analysis.writes);calls=@($analysis.calls|Sort-Object -CaseSensitive);
   instructions=$analysis.instructions;sourceSha256=(Get-FileHash $source).Hash.ToLowerInvariant();objectSha256=$member.sha256}
 }
 $contract=@{schema=1;archives=@(@{path=$ArchiveRelativePath;sha256=(Get-FileHash $Archive).Hash.ToLowerInvariant();functions=$functions})}
 $json=ConvertTo-Json -InputObject $contract -Depth 10
 [IO.File]::WriteAllText((Join-Path $OutputDirectory 'RuntimeCallEffects.json'),$json+"`n",[Text.UTF8Encoding]::new($false))
 $functions|Select-Object symbol,localWrites,calls|Format-Table
} finally {Pop-Location}
