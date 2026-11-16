# Reuse compiler invocations within one build and its cost-selection trials.
# Source/header snapshots, tool/profile locks, arguments and feedback contents
# are part of the key; this is not a persistent project or machine-wide cache.
function Initialize-Mx251CompileReuse {
 $script:compileReuseRoot=if($env:MX251_COMPILE_REUSE){$env:MX251_COMPILE_REUSE}else{Join-Path $outputRoot 'compile-reuse'}
 [IO.Directory]::CreateDirectory($script:compileReuseRoot)|Out-Null
 $identity=@((Hash-File $compilerExe),(Hash-File $lockPath),(Hash-File $PSCommandPath),(Hash-File (Join-Path $PSScriptRoot 'CompileReuse.ps1')))
 foreach($name in @($inputs.Keys|Sort-Object -CaseSensitive)){$identity+=($name+'='+$inputs[$name].sha256)}
 $script:compileReuseIdentity=$identity -join "`n"
}

function Get-Mx251CompileReuse([string]$Exe,[string[]]$Arguments,$Environment) {
 if($Exe -cne $compilerExe -or $Arguments -cnotcontains '-c' -or $Arguments -cnotcontains '-o'){return $null}
 $outputAt=[Array]::IndexOf($Arguments,'-o')+1
 $name=[IO.Path]::GetFileNameWithoutExtension($Arguments[$outputAt])
 $parts=@($script:compileReuseIdentity)
 for($i=0;$i -lt $Arguments.Count;$i++) {
  $value=$Arguments[$i];$path=if([IO.Path]::IsPathRooted($value)){$value}else{Join-Path $outputRoot $value}
  if($i -ne $outputAt -and [IO.File]::Exists($path)){$parts+=('file:'+([IO.Path]::GetFileName($value))+':'+(Hash-File $path))}
  else{$parts+=$value.Replace($outputRoot,'<build>')}
 }
 foreach($key in @($Environment.Keys|Where-Object{$_ -cmatch '^MX251_' -and $_ -cnotin @('MX251_COMPILE_REUSE','MX251_STDIO_PRINTF_UNITS','MX251_STDIO_TRIAL','MX251_DEFER_NEAR_TRANSFERS','MX251_EXPERIMENT_PAUSE_FILE','MX251_VERIFIED_TOOLSET','MX251_OUTPUT_LEASE')}|Sort-Object -CaseSensitive)) {
  $value=$Environment[$key]
  if([IO.File]::Exists($value)){$value='file:'+(Hash-File $value)}
  $parts+=($key+'='+$value)
 }
 $key=[Convert]::ToHexString([Security.Cryptography.SHA256]::HashData([Text.Encoding]::UTF8.GetBytes(($parts -join "`n")))).ToLowerInvariant()
 return @{key=$key;name=$name;directory=(Join-Path $script:compileReuseRoot $key)}
}

function Restore-Mx251CompileReuse($Entry,[string]$Label) {
 $manifest=Join-Path $Entry.directory 'result.json'
 if(-not [IO.File]::Exists($manifest)){return $false}
 $record=Get-Content -LiteralPath $manifest -Raw|ConvertFrom-Json -AsHashtable
 foreach($file in $record.files){[IO.File]::Copy((Join-Path $Entry.directory $file),(Join-Path $outputRoot $file),$true)}
 foreach($stream in @('stdout','stderr')){[IO.File]::Copy((Join-Path $Entry.directory ($stream+'.log')),(Join-Path $outputRoot ($Label+'.'+$stream+'.log')),$true)}
 return $true
}

function Save-Mx251CompileReuse($Entry,[string]$Label) {
 [IO.Directory]::CreateDirectory($Entry.directory)|Out-Null
 $files=@()
 foreach($extension in @('.asm','.rel','.lst','.sym','.scalar.txt','.return-audit.jsonl')){
  $name=$Entry.name+$extension;$path=Join-Path $outputRoot $name
  if([IO.File]::Exists($path)){[IO.File]::Copy($path,(Join-Path $Entry.directory $name),$true);$files+=$name}
 }
 foreach($stream in @('stdout','stderr')){[IO.File]::Copy((Join-Path $outputRoot ($Label+'.'+$stream+'.log')),(Join-Path $Entry.directory ($stream+'.log')),$true)}
 Write-Json (Join-Path $Entry.directory 'result.json') @{key=$Entry.key;files=$files;compilerSha256=(Hash-File $compilerExe)}
}
