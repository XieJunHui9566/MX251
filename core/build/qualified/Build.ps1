#requires -Version 7.0
param(
 [Parameter(Mandatory=$true)][string]$Project,
 [Parameter(Mandatory=$true)][string]$ToolchainRoot,
 [Parameter(Mandatory=$true)][string]$OutputDirectory,
 [Parameter(Mandatory=$true)][string]$ProfileRoot,
 [string]$CMakeInput,
 [switch]$C251BitComplement,
 [switch]$LeafCallFeedback,
 [switch]$WordCompareImmediate,
 [switch]$RangeNarrowMultiply,
 [string]$CalleeCostFile,
 [string]$CalleeCostSha256,
 [switch]$VolatileDataWide,
 [switch]$ConstantRotateFusion,
 [switch]$FunctionGc,
 [string]$FunctionGcKeep='',
 [ValidateSet('AUTO','OFF')][string]$OrderedAsm='AUTO',
 [ValidateSet('128','256','512')][string]$WideValueLimit='128',
 [ValidateSet('512','1024','2048')][string]$WideNodeLimit='512',
 [switch]$ManagedOutput
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$commands=[Collections.Generic.List[object]]::new()
if([bool]$CalleeCostFile -ne [bool]$CalleeCostSha256){throw 'Callee cost file and SHA-256 must be supplied together'}
if($CalleeCostFile){
 $CalleeCostFile=[IO.Path]::GetFullPath($CalleeCostFile)
 if($CalleeCostSha256 -cnotmatch '^[0-9a-f]{64}$' -or (Get-FileHash -LiteralPath $CalleeCostFile -Algorithm SHA256).Hash.ToLowerInvariant() -cne $CalleeCostSha256){throw 'Callee cost evidence hash mismatch'}
}
$ownsOutput=$false
$outputRoot=$null
$includeDirectories=@()
$compilerExe=$null
$vectorCount=0
function Write-Json($Path,$Value) {
 [IO.File]::WriteAllText($Path,(ConvertTo-Json -InputObject $Value -Depth 50)+[Environment]::NewLine,[Text.UTF8Encoding]::new($false))
}
function Hash-File([string]$Path) { return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant() }
function Keys-Only($Object,[string[]]$Allowed,[string]$Label) {
 if($Object -isnot [Collections.IDictionary]) {throw "$Label must be an object"}
 foreach($key in $Object.Keys) {if($key -notin $Allowed){throw "Unknown $Label property: $key"}}
}
function Choice($Value,[string[]]$Allowed,[string]$Label) {
 if($Value -isnot [string] -or $Value -cnotin $Allowed){throw "Unsupported ${Label}: $Value"}
}
function Relative-File([string]$Root,$Relative) {
 if($Relative -isnot [string] -or [string]::IsNullOrWhiteSpace($Relative) -or [IO.Path]::IsPathRooted($Relative) -or $Relative -match '(^|[\\/])\.\.?([\\/]|$)' -or $Relative.Contains(':')) {throw "Invalid relative path: $Relative"}
 $full=[IO.Path]::GetFullPath((Join-Path $Root $Relative))
 if(-not $full.StartsWith($Root.TrimEnd('\')+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Path escapes its declared root'}
 $item=Get-Item -LiteralPath $full -Force
 if($item -isnot [IO.FileInfo]){throw "Expected file: $Relative"}
 $ancestor=$item
 while($null -ne $ancestor -and $ancestor.FullName -ne $Root) {
  if($ancestor.Attributes -band [IO.FileAttributes]::ReparsePoint){throw "Reparse point is outside this qualified build contract: $Relative"}
  $ancestor=if($ancestor -is [IO.DirectoryInfo]){$ancestor.Parent}else{$ancestor.Directory}
 }
 return $full
}
function Defines($Values) {
 if($Values -isnot [array]){throw 'defines must be an array'}
 foreach($value in $Values){if($value -isnot [string] -or $value -notmatch '^[A-Za-z_][A-Za-z0-9_]*(=[A-Za-z0-9_+.-]+)?$'){throw "Unsupported define: $value"}}
}
$script:scalarParameterContracts=@{}
function Run-Tool([string]$Label,[string]$Exe,[string[]]$Arguments,[switch]$AllowFailure) {
 if($Exe -ceq $compilerExe -and $Arguments -contains '-c' -and $Arguments -contains '-o'){
  $outAt=[Array]::IndexOf($Arguments,'-o')+1
  $name=[IO.Path]::GetFileNameWithoutExtension($Arguments[$outAt])
  if($script:scalarParameterContracts.ContainsKey($name)){
   $binding=$script:scalarParameterContracts[$name]
   if((Hash-File $binding.path) -cne $binding.sha256){throw 'scalar-contract-drift'}
   $Arguments+=@('--mcs251-scalar-parameters',$binding.path)
  }
 }

 $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=$Exe;$start.WorkingDirectory=$outputRoot
 $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 # 子进程只使用锁定工具；用户环境保持原样，避免环境变量暗中替换头文件或运行库。
 foreach($key in @('SDCC_HOME','SDCC_INCLUDE','SDCC_LIB','SDCC_BIN','SDCCLIB','SDCCDIR','CPATH','C_INCLUDE_PATH')){$start.Environment.Remove($key)|Out-Null}
 # 候选仅替换编译器；预处理器、汇编器、头文件和库仍来自已锁定的父工具链。
 $start.Environment['SDCC_HOME']=$toolRoot
 # 容量必须来自本次显式构建选项；同名用户环境不能暗中改变导入范围。
 $start.Environment['MX251_WIDE_VALUE_LIMIT']=$WideValueLimit
 $start.Environment['MX251_WIDE_NODE_LIMIT']=$WideNodeLimit
 $start.Environment.Remove('MX251_OBJECT_ADDRESS_CAPABILITY')|Out-Null
 if($objectAddressRecord.enabled){$start.Environment['MX251_OBJECT_ADDRESS_CAPABILITY']=$objectAddressRecord.contract}
 $start.Environment['PATH']=(Join-Path $toolRoot 'bin')+';'+(Join-Path $env:SystemRoot 'System32')+';'+$env:SystemRoot
 foreach($arg in $Arguments){$start.ArgumentList.Add($arg)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$start
 $stdout=[IO.File]::Create((Join-Path $outputRoot ($Label+'.stdout.log')))
 $stderr=[IO.File]::Create((Join-Path $outputRoot ($Label+'.stderr.log')))
 try {
  if($env:MX251_EXPERIMENT_PAUSE_FILE -and (Test-Path -LiteralPath $env:MX251_EXPERIMENT_PAUSE_FILE)){throw 'PAUSE_REQUESTED: no tool child started'}
  if(-not $process.Start()){throw "Cannot start $Label"}
  $readOut=$process.StandardOutput.BaseStream.CopyToAsync($stdout);$readErr=$process.StandardError.BaseStream.CopyToAsync($stderr)
  if(-not $process.WaitForExit(120000)){$process.Kill($true);$process.WaitForExit();throw "Tool timeout: $Label"}
  [Threading.Tasks.Task]::WaitAll([Threading.Tasks.Task[]]@($readOut,$readErr))
  $code=$process.ExitCode
  $commands.Add([ordered]@{label=$Label;executable=$Exe;arguments=$Arguments;exitCode=$code})
  Write-Json (Join-Path $outputRoot 'commands.json') @($commands.ToArray())
  if($code -ne 0 -and -not $AllowFailure){throw "$Label failed with exit code $code; original stdout/stderr logs retained"}
 } finally {$stdout.Dispose();$stderr.Dispose();$process.Dispose()}
}
function Flags([string]$Backend,[bool]$StackAuto,$ExtraDefines) {
 $f=@('-mmcs251',('--model-'+$config.model),('--opt-code-'+$config.optimization))
 if($vectorCount -gt 0){$f+=@('--mcs251-vector-count',([string]$vectorCount))}
 if($config.standard -ne 'default'){$f+=('--std-'+$config.standard)}
 if($config.explicitAbi){$f+='--mcs251-native-abi'}
 if($StackAuto){$f+='--stack-auto'}
 if($Backend -eq 'ordinary-wide'){$f+='--mcs251-wide-ordinary'}
 if($Backend -eq 'explicit-wide'){$f+='--mcs251-wide-backend'}
 if($Backend -in @('ordinary-wide','explicit-wide')){$f+=@($orderedAsmRecord.compilerArguments)}
 if($C251BitComplement){$f+='--mcs251-c251-bit-complement'}
 $f+='--mcs251-function-metadata'
 if($WordCompareImmediate){$f+='--mcs251-word-compare-immediate'}
 if($RangeNarrowMultiply){$f+='--mcs251-range-narrow-multiply'}
 if($CalleeCostFile){$f+=@('--mcs251-callee-cost',$CalleeCostFile,'--mcs251-callee-cost-sha256',$CalleeCostSha256)}
 if($ConstantRotateFusion -and $Backend -in @('ordinary-wide','explicit-wide')){$f+='--mcs251-constant-rotate-fusion'}
 if($VolatileDataWide -and $Backend -in @('ordinary-wide','explicit-wide')){$f+='--mcs251-volatile-data-wide'}
 if($runtime -in @('native-int','native-int-mdu32')){$f+='--mcs251-native-int-runtime'}
 if($runtime -in @('native-math','native-math-wide','native-math-mdu32','native-math-wide-mdu32')){$f+=@('--mcs251-native-runtime','--mcs251-native-int-runtime','--nostdlib')}
 foreach($d in @($config.defines)+@($ExtraDefines)){$f+=('-D'+$d)}
 foreach($directory in $includeDirectories){$f+=('-I'+$directory)}
 return $f
}
function Check-Assembly([string]$Path,[bool]$QualifiedStartup=$false) {
 # 已知汇编边界：未解析表达式会影响DR立即数扩展编码。尚未修复前明确拒绝，不能静默发错码。
 $lineNumber=0
 foreach($line in [IO.File]::ReadAllLines($Path)){
  $lineNumber++
  $code=($line -split ';',2)[0]
  $match=[regex]::Match($code,'(?i)^\s*(?:[\w.$]+:\s*)?(mov|cmp)\s+(dr\d+|dpx|spx)\s*,\s*#(.+?)\s*$')
  if(-not $match.Success){continue}
  $expr=$match.Groups[3].Value
  if($expr -match '^[+-]?(0[xX][0-9a-fA-F]+|[0-9]+)$'){continue}
  if($QualifiedStartup -and $match.Groups[1].Value -ieq 'mov' -and $match.Groups[2].Value -ieq 'spx' -and $expr -ceq '__mcs251_stack_initial'){continue}
  throw "Unqualified symbolic DR immediate at ${Path}:${lineNumber}: $expr; use explicit byte/word loads. No firmware published."
 }
}
try {
 . (Join-Path $PSScriptRoot 'InterruptDiagnostics.ps1')
 . (Join-Path $PSScriptRoot 'TargetProfile.ps1')
 $projectPath=(Resolve-Path -LiteralPath $Project).Path
 $projectRoot=Split-Path -Parent $projectPath
 $toolRoot=(Resolve-Path -LiteralPath $ToolchainRoot).Path.TrimEnd('\')
 $config=Get-Content -LiteralPath $projectPath -Raw -Encoding UTF8 | ConvertFrom-Json -AsHashtable
 # 工程管理适配层：编译器、运行库和启动来自当前组件锁定的 profile。
 $ProfileRoot=(Resolve-Path -LiteralPath $ProfileRoot).Path
 if($CMakeInput){
  $spec=Get-Content -LiteralPath $CMakeInput -Raw -Encoding UTF8 | ConvertFrom-Json -AsHashtable
  Keys-Only $spec @('sources','includeDirectories','defines') 'CMake inputs'
  Defines $spec.defines
  if(@($spec.defines|Where-Object{$_ -match '^MX251_(IRC_HZ|DELAY_TICKS)(=|$)'}).Count){throw 'Set the IRC clock in project settings, not CMake overrides'}
  $config.defines=@($config.defines)+@($spec.defines)
  $previous=@{};if($config.ContainsKey('units')){foreach($u in $config.units){$previous[$u.file.Replace('\','/')]=$u}}
  $newUnits=@();$newFiles=@();$newArchives=[ordered]@{};$newLink=@()
  foreach($source in $spec.sources){
   Keys-Only $source @('file','MX251_BACKEND','MX251_ARCHIVE','defines','stackAuto') 'CMake source'
   $relative=[IO.Path]::GetRelativePath($projectRoot,[IO.Path]::GetFullPath($source.file)).Replace('\','/')
   $null=Relative-File $projectRoot $relative
   if($relative -in $newFiles){throw "Duplicate source: $relative"};$newFiles+=$relative
   if([IO.Path]::GetExtension($relative) -in @('.h','.inc')){continue}
   if($previous.ContainsKey($relative)){$unit=$previous[$relative].Clone()}
   else{$unit=@{name=('u'+$newUnits.Count);file=$relative;backend=$config.backend;stackAuto=$true;defines=@()}}
   if($source.ContainsKey('MX251_BACKEND')){$unit.backend=$source.MX251_BACKEND}
   if($source.ContainsKey('stackAuto')){if($source.stackAuto -isnot [bool]){throw 'CMake stackAuto must be boolean'};$unit.stackAuto=$source.stackAuto}
   if($source.ContainsKey('defines')){
    Defines $source.defines
    if(@($source.defines|Where-Object{$_ -match '^MX251_(IRC_HZ|DELAY_TICKS)(=|$)'}).Count){throw 'Per-source clock overrides are unsupported'}
    $unit.defines=@($unit.defines)+@($source.defines)
   }
   $newUnits+=$unit
   $archive=if($source.ContainsKey('MX251_ARCHIVE')){$source.MX251_ARCHIVE}else{$null}
   if($archive){
    if(-not $newArchives.Contains($archive)){$newArchives[$archive]=@();$newLink+=$archive}
    $newArchives[$archive]+=$unit.name
   }else{$newLink+=$unit.name}
  }
  $config.units=$newUnits;$config.files=$newFiles
  $config.archives=@(foreach($name in $newArchives.Keys){@{name=$name;units=@($newArchives[$name])}})
  $config.link=$newLink+@('@startup')
  $includeDirectories=@($spec.includeDirectories)
 }elseif(Test-Path -LiteralPath (Join-Path $projectRoot 'include') -PathType Container){$includeDirectories=@('include')}
 $normalizedIncludes=@()
 foreach($directory in $includeDirectories){
  $full=[IO.Path]::GetFullPath($directory,$projectRoot)
  $relative=[IO.Path]::GetRelativePath($projectRoot,$full).Replace('\','/')
  if($relative -eq '.' -or $relative -match '(^|/)\.\.(/|$)' -or [IO.Path]::IsPathRooted($relative) -or -not (Test-Path -LiteralPath $full -PathType Container)){throw "Include directory must be inside the project: $directory"}
  $pending=[Collections.Generic.Stack[string]]::new();$pending.Push($full)
  while($pending.Count){
   $dir=$pending.Pop();if((Get-Item -LiteralPath $dir).Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked include directory rejected'}
   foreach($entry in Get-ChildItem -LiteralPath $dir -Force){
    if($entry.Attributes -band [IO.FileAttributes]::ReparsePoint){throw 'Linked include input rejected'}
    if($entry.PSIsContainer){$pending.Push($entry.FullName)}
    elseif($entry.Extension -in @('.h','.inc')){
     $file=[IO.Path]::GetRelativePath($projectRoot,$entry.FullName).Replace('\','/')
     if($file -notin $config.files){$config.files+=@($file)}
    }
   }
  }
  $normalizedIncludes+=$relative
 }
 $includeDirectories=$normalizedIncludes
 $commonKeys=@('schema','target','model','optimization','backend','explicitAbi','standard','defines','files','units','archives','link')
 if($config.schema -eq 1){
  Keys-Only $config ($commonKeys+@('nativeMath')) 'project'
  if(-not $config.ContainsKey('nativeMath') -or $config.nativeMath -isnot [bool]){throw 'nativeMath must be boolean in schema 1'}
  $runtime=if($config.nativeMath){'native-math'}else{'standard'}
 } elseif($config.schema -eq 2){
  Keys-Only $config ($commonKeys+@('runtime')) 'project'
  if(-not $config.ContainsKey('runtime')){throw 'Missing project property: runtime'}
  Choice $config.runtime @('standard','native-int','native-math','native-math-wide','native-int-mdu32','native-math-mdu32','native-math-wide-mdu32') 'runtime'
  $runtime=$config.runtime
 } else{throw 'Unsupported project schema'}
 foreach($key in $commonKeys){if(-not $config.ContainsKey($key)){throw "Missing project property: $key"}}
 $targetContract=Get-Mx251TargetContract $config.target
 $orderedAsmRecord=Get-Mx251OrderedAsm $targetContract $OrderedAsm
 $layout=$targetContract.qualifiedMemory
 $objectAddressRecord=[ordered]@{enabled=$false;requested=($env:MX251_OBJECT_ADDRESS_NATIVE -ceq '1');reason='disabled';contract='mcs251-flat24-codeff-spx-v1';finalClosureRequired=$true;hardwareQualified=$false}
 if($objectAddressRecord.requested){
  $objectAddressRecord.reason='unknown-capability-or-memory-layout'
  if($targetContract.compilerCapabilities.ContainsKey('objectAddressNative')){
   $cap=$targetContract.compilerCapabilities.objectAddressNative
   Keys-Only $cap @('schema','contract') 'native object address capability'
   if($cap.schema -eq 1 -and $cap.contract -ceq $objectAddressRecord.contract -and
      $targetContract.instructionMode -ceq '32-bit' -and $layout.codeBase -eq 16711680 -and
      $layout.codeBytes -gt 0 -and $layout.codeBytes -le 65536 -and $layout.stackBase -ge 256 -and
      $layout.stackBytes -gt 0 -and ($layout.stackBase+$layout.stackBytes) -le 16777216){
    $objectAddressRecord.enabled=$true;$objectAddressRecord.reason='explicit-capability-and-bounded-layout'
   }
  }
 }
 Choice $config.model @('small','large') 'model'
 Choice $config.optimization @('speed','size') 'optimization'
 Choice $config.backend @('legacy','ordinary-wide','explicit-wide') 'backend'
 Choice $config.standard @('default','sdcc11') 'standard'
 if($config.explicitAbi -isnot [bool]){throw 'explicitAbi must be boolean'}
 if($runtime -notin @('standard','native-int-mdu32') -and $config.model -ne 'small'){throw 'Native runtime is qualified only for small stack-auto'}
 if($runtime -in @('native-math','native-math-mdu32') -and ($config.explicitAbi -or $config.backend -notin @('legacy','ordinary-wide'))){throw 'Native math entry currently qualifies the fixed ordinary ABI full-math project only'}
 if($runtime -in @('native-math-wide','native-math-wide-mdu32') -and (-not $config.explicitAbi -or $config.backend -ne 'explicit-wide')){throw 'native-math-wide requires explicit ABI and explicit-wide backend'}
 if($runtime -eq 'native-int' -and ($config.explicitAbi -or $config.backend -ne 'explicit-wide')){throw 'native-int qualification requires the fixed explicit-wide/default ABI configuration'}
 Defines $config.defines
 foreach($key in @('files','units','archives','link')){if($config[$key] -isnot [array]){throw "$key must be an array"}}
 if(-not $config.units.Count){throw 'No translation units'}
 $inputs=@{};$units=@{};$archives=@{};$coverage=@{}
 foreach($file in $config.files){
  if($inputs.ContainsKey($file)){throw "Duplicate input: $file"}
  if([IO.Path]::GetFileName($file) -ieq 'startup.asm'){throw 'startup.asm is reserved by the qualified profile'}
  if([IO.Path]::GetExtension($file) -cnotin @('.c','.h','.asm','.inc')){throw "Unsupported input type: $file"}
  $path=Relative-File $projectRoot $file
  $inputs[$file]=[ordered]@{path=$path;sha256=(Hash-File $path)}
 }
 foreach($u in $config.units){
  Keys-Only $u @('name','file','backend','stackAuto','defines') 'unit'
  foreach($key in @('name','file','backend','stackAuto','defines')){if(-not $u.ContainsKey($key)){throw "Missing unit property: $key"}}
  if($u.name -isnot [string] -or $u.name -notmatch '^[A-Za-z_][A-Za-z0-9_]*$' -or $u.name -ieq 'startup' -or $units.ContainsKey($u.name)){throw 'Duplicate or invalid unit name'}
  if(-not $inputs.ContainsKey($u.file) -or [IO.Path]::GetExtension($u.file) -cnotin @('.c','.asm')){throw "Missing unit source: $($u.file)"}
  Choice $u.backend @('legacy','ordinary-wide','explicit-wide') 'unit backend'
  if($u.stackAuto -isnot [bool]){throw 'stackAuto must be boolean'}
  if(-not $u.stackAuto -and -not $config.explicitAbi){throw 'Non-auto units require the explicit reentrant ABI qualification profile'}
  if($runtime -ne 'standard' -and -not $u.stackAuto){throw 'Native runtime requires stack-auto in every unit'}
  if($runtime -notin @('standard','native-int-mdu32') -and $u.backend -ne $config.backend){throw 'Native runtime unit backend is outside this qualification profile'}
  Defines $u.defines;$units[$u.name]=$u;$coverage[$u.name]=0
 }
 foreach($a in $config.archives){
  Keys-Only $a @('name','units') 'archive'
  if($a.name -isnot [string] -or $a.name -notmatch '^[A-Za-z_][A-Za-z0-9_]*$' -or $a.name -ieq 'startup' -or $units.ContainsKey($a.name) -or $archives.ContainsKey($a.name)){throw 'Duplicate or invalid archive name'}
  if($a.units -isnot [array] -or -not $a.units.Count){throw 'Empty or invalid archive'}
  foreach($n in $a.units){if(-not $units.ContainsKey($n)){throw "Unknown archive member: $n"};$coverage[$n]++}
  $archives[$a.name]=$a
 }
 $seen=@{};$linkInputs=@()
 foreach($n in $config.link){
  if($n -isnot [string] -or $seen.ContainsKey($n)){throw 'Duplicate or invalid link entry'};$seen[$n]=$true
  if($n -eq '@startup'){$linkInputs+='startup.rel'}
  elseif($units.ContainsKey($n)){$linkInputs+=($n+'.rel');$coverage[$n]++}
  elseif($archives.ContainsKey($n)){$linkInputs+=($n+'.lib')}
  else{throw "Unknown link input: $n"}
 }
 if(-not $seen.ContainsKey('@startup')){throw 'Qualified startup must be linked exactly once'}
 foreach($n in $coverage.Keys){if($coverage[$n] -ne 1){throw "Unit $n must be placed exactly once"}}
 foreach($n in $archives.Keys){if(-not $seen.ContainsKey($n)){throw "Unused archive declaration: $n"}}
 # 工具、运行库、头文件和启动均按清单核验；不修改系统PATH或IDE默认工具链。
 $lockPath=Join-Path $ProfileRoot 'profile.lock.json';$lock=Get-Content -LiteralPath $lockPath -Raw | ConvertFrom-Json -AsHashtable
 foreach($entry in $lock.toolchain){$path=Relative-File $toolRoot $entry.path;if((Hash-File $path) -ne $entry.sha256){throw "Toolchain lock mismatch: $($entry.path)"}}
 foreach($entry in $lock.profile){$path=Relative-File $ProfileRoot $entry.path;if((Hash-File $path) -ne $entry.sha256){throw "Profile lock mismatch: $($entry.path)"}}
 Assert-Mx251TargetProfile $lock $targetContract $ProfileRoot $runtime
 $compilerExe=Join-Path $toolRoot 'bin/sdcc.exe'
 if($lock.ContainsKey('compilerOverlay')){
  Keys-Only $lock.compilerOverlay @('path','sha256') 'compiler overlay'
  $compilerExe=Relative-File $ProfileRoot $lock.compilerOverlay.path
  if((Hash-File $compilerExe) -cne $lock.compilerOverlay.sha256 -or @($lock.profile|Where-Object{$_.path -ceq $lock.compilerOverlay.path -and $_.sha256 -ceq $lock.compilerOverlay.sha256}).Count -ne 1){throw 'Compiler overlay must be pinned exactly once in profile lock'}
 }
 $vectorCount=[int]$targetContract.interruptVectors.count

 $outputRoot=[IO.Path]::GetFullPath($OutputDirectory)
 if(Test-Path -LiteralPath $outputRoot){
  if(-not $ManagedOutput){throw 'Output directory already exists; choose a fresh build directory. Existing files are preserved.'}
  $expected=[IO.Path]::GetFullPath((Join-Path (Split-Path -Parent $projectPath) '.build/mx251/latest'))
  $items=@(Get-ChildItem -LiteralPath $outputRoot -Force)
  if($outputRoot -ine $expected -or $items.Count -ne 1 -or $items[0].Name -cne 'owner.json' -or
     ($items[0].Attributes -band [IO.FileAttributes]::ReparsePoint) -or
     ((Get-Item -LiteralPath $outputRoot).Attributes -band [IO.FileAttributes]::ReparsePoint)){throw 'Managed output must be a fresh owned directory'}
  $owner=Get-Content -LiteralPath $items[0].FullName -Raw | ConvertFrom-Json
  if($owner.schema -ne 1 -or $owner.project -cne $projectPath){throw 'Managed output owner mismatch'}
 } elseif($ManagedOutput){throw 'Managed output ownership record is missing'}
 if($outputRoot.StartsWith($toolRoot+'\',[StringComparison]::OrdinalIgnoreCase) -or $outputRoot -eq $toolRoot -or  $outputRoot.StartsWith($ProfileRoot+'\',[StringComparison]::OrdinalIgnoreCase)){throw 'Output must be separate from toolchain and profile'}
 [IO.Directory]::CreateDirectory($outputRoot) | Out-Null;$ownsOutput=$true
 Write-Json (Join-Path $outputRoot 'build-state.json') @{status='building';project=$projectPath;profileLockSha256=(Hash-File $lockPath)}
 Write-Json (Join-Path $outputRoot 'project.snapshot.json') $config
 foreach($file in $inputs.Keys){$dest=Join-Path $outputRoot $file;[IO.Directory]::CreateDirectory((Split-Path -Parent $dest))|Out-Null;[IO.File]::Copy($inputs[$file].path,$dest,$false);if((Hash-File $dest) -ne $inputs[$file].sha256){throw 'Input changed during snapshot'}}
 [IO.File]::Copy((Join-Path $ProfileRoot 'startup.asm'),(Join-Path $outputRoot 'startup.asm'),$false)
 foreach($u in $config.units){
  if([IO.Path]::GetExtension($u.file) -eq '.c'){$args=@(Flags $u.backend $u.stackAuto $u.defines)+@('-c',$u.file,'-o',($u.name+'.rel'));Run-Tool $u.name $compilerExe $args;Check-Assembly (Join-Path $outputRoot ($u.name+'.asm'))}
  else{Check-Assembly (Join-Path $outputRoot $u.file);Run-Tool $u.name (Join-Path $toolRoot 'bin/sdas251.exe') @('-plosgff',($u.name+'.rel'),$u.file)}
 }
 . (Join-Path $PSScriptRoot 'ScalarParameters.ps1')
 $scalarRecord=Invoke-Mx251ScalarParameters $outputRoot $config $toolRoot
 Write-Json (Join-Path $outputRoot 'scalar-parameters.json') $scalarRecord
 $callClobberRecord=$null
 if(-not $LeafCallFeedback) {
  . (Join-Path $PSScriptRoot 'CallClobberFeedback.ps1')
  $callClobberRecord=Invoke-Mx251CallClobberFeedback $outputRoot $config $inputs (Hash-File $compilerExe) (Hash-File $lockPath)
  Write-Json (Join-Path $outputRoot 'call-clobber-feedback.json') $callClobberRecord
 }
 $leafFeedbackRecord=$null
 if($LeafCallFeedback) {
  . (Join-Path $PSScriptRoot 'CallEffects.ps1')
  . (Join-Path $PSScriptRoot 'LeafCallFeedback.ps1')
  $leafFeedbackRecord=New-Mx251LeafFeedback $outputRoot $config $inputs (Hash-File $compilerExe) (Hash-File $lockPath)
  Assert-Mx251LeafFeedback $outputRoot $config $leafFeedbackRecord (Hash-File $compilerExe) (Hash-File $lockPath)
  foreach($u in $config.units) {
   if([IO.Path]::GetExtension($u.file) -ceq '.c' -and $u.backend -cne 'legacy') {
    $args=@(Flags $u.backend $u.stackAuto $u.defines)+@('--mcs251-leaf-effects','leaf-effects.txt','-c',$u.file,'-o',($u.name+'.rel'))
    Run-Tool ('feedback-'+$u.name) $compilerExe $args;Check-Assembly (Join-Path $outputRoot ($u.name+'.asm'))
   }
  }
  Assert-Mx251LeafFeedback $outputRoot $config $leafFeedbackRecord (Hash-File $compilerExe) (Hash-File $lockPath)
  $leafFeedbackRecord.finalValidated=$true
  Write-Json (Join-Path $outputRoot 'leaf-feedback.json') $leafFeedbackRecord
 }
 $functionGcRecord=$null
 if($FunctionGc) {
  . (Join-Path $PSScriptRoot 'FunctionGc.ps1')
  $gcUnits=@($config.units | ForEach-Object {
   [ordered]@{name=$_.name;assembly=(Join-Path $outputRoot $(if([IO.Path]::GetExtension($_.file) -eq '.c'){$_.name+'.asm'}else{$_.file}));object=(Join-Path $outputRoot ($_.name+'.rel'));language=$(if([IO.Path]::GetExtension($_.file) -eq '.c'){'c'}else{'asm'})}
  })
  # 库中潜在的反向引用也属于根；保守检查整个配套251库集合，不依赖抽取次序。
  $gcLibs=@(Get-ChildItem -LiteralPath (Join-Path $toolRoot 'lib') -Directory | Where-Object {$_.Name -like 'mcs251*'} | Get-ChildItem -Recurse -File -Filter '*.lib' | ForEach-Object {$_.FullName})
  $profileLibs=Join-Path $ProfileRoot 'runtime'
  if(Test-Path -LiteralPath $profileLibs){$gcLibs+=@(Get-ChildItem -LiteralPath $profileLibs -File -Filter '*.lib' | ForEach-Object {$_.FullName})}
  $keep=@();if($FunctionGcKeep){$keep=@($FunctionGcKeep.Split(','));foreach($s in $keep){if($s -cnotmatch '^[A-Za-z_.$][A-Za-z0-9_.$]*$'){throw 'Invalid explicit function GC root'}}}
  $gcResult=Invoke-Mx251FunctionGc -Root $outputRoot -Units $gcUnits -ExtraAssembly @((Join-Path $outputRoot 'startup.asm')) -LibraryPaths $gcLibs -KeepSymbols $keep
  foreach($changed in $gcResult.ChangedUnits) {
   [IO.File]::Copy($changed.candidateAssembly,$changed.assembly,$true)
   Check-Assembly $changed.assembly
   Run-Tool ('function-gc-'+$changed.name) (Join-Path $toolRoot 'bin/sdas251.exe') @('-plosgff',$changed.object,$changed.assembly)
  }
  $functionGcRecord=$gcResult.Manifest
 }
 foreach($a in $config.archives){$args=@('-rc',($a.name+'.lib'))+@($a.units|ForEach-Object{$_+'.rel'});Run-Tool ('archive-'+$a.name) (Join-Path $toolRoot 'bin/sdar.exe') $args}
 Check-Assembly (Join-Path $outputRoot 'startup.asm') $true
 Run-Tool 'startup' (Join-Path $toolRoot 'bin/sdas251.exe') @('-plosgff','startup.rel','startup.asm')
 $flags=@(Flags $config.backend $true @())
 $libs=@()
 if($runtime -eq 'native-int'){$libs+=(Join-Path $ProfileRoot 'runtime/libnativeint.lib')}
 if($runtime -eq 'native-int-mdu32'){$libs+=(Join-Path $ProfileRoot 'runtime/libai8051umdu32.lib')}
 if($runtime -in @('native-math','native-math-wide','native-math-mdu32','native-math-wide-mdu32')){
  $libs+=(Join-Path $ProfileRoot 'runtime/libnativefloat.lib')
  if($runtime -eq 'native-math'){$libs+=(Join-Path $ProfileRoot 'runtime/libnativeint.lib')}
  if($runtime -in @('native-math-mdu32','native-math-wide-mdu32')){$libs+=(Join-Path $ProfileRoot 'runtime/libai8051umdu32.lib')}
  foreach($n in @('mcs251','libsdcc','libint','liblong','liblonglong')){$libs+=(Join-Path $toolRoot ('lib/mcs251-small-stack-auto/'+$n+'.lib'))}
  if($runtime -eq 'native-math-wide'){$libs+=(Join-Path $ProfileRoot 'runtime/libnativeint.lib')}
 }
 $args=$flags+@('--code-loc',('0x{0:x}' -f $layout.codeBase),'--code-size',('0x{0:x}' -f $layout.codeBytes),'--xram-loc',('0x{0:x}' -f $layout.xramBase),'--xram-size',('0x{0:x}' -f $layout.xramBytes),'--iram-size',('0x{0:x}' -f $layout.iramBytes),'--mcs251-stack',('0x{0:x},0x{1:x},0x{2:x}' -f $layout.stackBase,$layout.stackBytes,$layout.stackLimit))+$linkInputs+$libs+@('-o','linked.partial.hex')
 . (Join-Path $PSScriptRoot 'InterruptVectors.ps1')
 $interruptPlan=Get-Mx251InterruptSelection $outputRoot $config $vectorCount
 if($interruptPlan.needsDiscovery){
  # 只有含 ISR 的归档需要先取得实际抽取成员；中间链接不发布 firmware。
  $discoveryArgs=@($args);$discoveryArgs[$discoveryArgs.Count-1]='irq-discovery.hex'
  Run-Tool 'irq-discovery' $compilerExe $discoveryArgs -AllowFailure
  $interruptPlan=Get-Mx251InterruptSelection $outputRoot $config $vectorCount (Join-Path $outputRoot 'irq-discovery.map')
 }
 Set-Mx251InterruptTable $outputRoot $interruptPlan
 if($interruptPlan.changed){
  Check-Assembly $interruptPlan.mainAssembly
  Run-Tool 'interrupt-vectors' (Join-Path $toolRoot 'bin/sdas251.exe') @('-plosgff',($interruptPlan.mainUnit+'.rel'),$interruptPlan.mainAssembly)
  foreach($a in $config.archives){if($interruptPlan.mainUnit -in $a.units){
   $archiveArgs=@('-rc',($a.name+'.lib'))+@($a.units|ForEach-Object{$_+'.rel'})
   Run-Tool ('interrupt-archive-'+$a.name) (Join-Path $toolRoot 'bin/sdar.exe') $archiveArgs
  }}
 }
 Write-Json (Join-Path $outputRoot 'interrupt-vectors.plan.json') $interruptPlan
 Run-Tool 'link' $compilerExe $args
 . (Join-Path $PSScriptRoot 'FormatRuntime.ps1')
 $formatRuntimeRecord=Invoke-Mx251FormatRuntime $outputRoot $config $toolRoot $args
 Write-Json (Join-Path $outputRoot 'format-runtime.json') $formatRuntimeRecord
 . (Join-Path $PSScriptRoot 'StartupRuntime.ps1')
 $startupRuntimeRecord=Invoke-Mx251StartupRuntime $outputRoot $config $toolRoot
 Write-Json (Join-Path $outputRoot 'startup-runtime.json') $startupRuntimeRecord
 # 成功退出仍需核对完整HEX和真实SSEG后，才发布最终固件名称。
 $hex=Join-Path $outputRoot 'linked.partial.hex';$memory=@{};$base=0L;$eof=$false
 foreach($line in [IO.File]::ReadAllLines($hex)){
  if($eof -or $line -notmatch '^:([0-9A-Fa-f]{2})+$'){throw 'Malformed HEX record or data after EOF'}
  $b=[Convert]::FromHexString($line.Substring(1));if($b.Length -ne $b[0]+5){throw 'HEX length mismatch'}
  $sum=0;foreach($byte in $b){$sum+=$byte};if(($sum-band 255)-ne 0){throw 'HEX checksum mismatch'}
  $offset=([int]$b[1]-shl 8)+[int]$b[2]
  switch($b[3]){
   0 {for($i=0;$i-lt $b[0];$i++){$addr=$base+$offset+$i;if($addr-lt $layout.codeBase -or $addr-ge ($layout.codeBase+$layout.codeBytes) -or $memory.ContainsKey($addr)){throw 'HEX overlap or out-of-profile code address'};$memory[$addr]=$b[4+$i]}}
   1 {if($b[0]-ne 0 -or $offset-ne 0){throw 'Invalid EOF'};$eof=$true}
   2 {if($b[0]-ne 2 -or $offset-ne 0){throw 'Invalid segment record'};$base=[long]((([int]$b[4]-shl 8)+[int]$b[5])-shl 4)}
   4 {if($b[0]-ne 2 -or $offset-ne 0){throw 'Invalid linear record'};$base=[long]((([int]$b[4]-shl 8)+[int]$b[5])-shl 16)}
   default {throw "Unsupported HEX record type: $($b[3])"}
  }
 }
 if(-not $eof -or -not $memory.ContainsKey([long]$layout.codeBase)){throw 'Missing EOF or reset vector'}
 $map=[IO.File]::ReadAllText((Join-Path $outputRoot 'linked.partial.map'))
 foreach($pair in @(@('s_SSEG',$layout.stackBase),@('l_SSEG',$layout.stackBytes),@('__mcs251_stack_initial',($layout.stackBase-1)))){
  $match=[regex]::Match($map,'(?m)^[CD]:\s+([0-9A-F]+)\s+'+[regex]::Escape($pair[0])+'\s*$')
  if(-not $match.Success -or [Convert]::ToInt32($match.Groups[1].Value,16)-ne $pair[1]){throw "Link contract mismatch: $($pair[0])"}
 }
 Assert-Mx251InterruptTable $outputRoot $config $interruptPlan $memory $map ([long]$layout.codeBase)
 [IO.File]::Move($hex,(Join-Path $outputRoot 'firmware.hex'))
 # BIN 从已核验的 CODE 起点开始，空洞填 FF；物理装载地址显式写入凭据，不能从文件名猜测。
 $last=($memory.Keys | Measure-Object -Maximum).Maximum
 $binary=[byte[]]::new([int]($last-$layout.codeBase+1))
 [Array]::Fill[byte]($binary,255)
 foreach($address in $memory.Keys){$binary[[int]($address-$layout.codeBase)]=$memory[$address]}
 [IO.File]::WriteAllBytes((Join-Path $outputRoot 'firmware.bin'),$binary)
 $receipt=[ordered]@{status='complete';schema=4;target=$config.target;model=$config.model;optimization=$config.optimization;runtime=$runtime;projectSnapshotSha256=(Hash-File (Join-Path $outputRoot 'project.snapshot.json'));profileLockSha256=(Hash-File $lockPath);compilerSha256=(Hash-File $compilerExe);interruptVectorCount=$vectorCount;firmwareSha256=(Hash-File (Join-Path $outputRoot 'firmware.hex'));codeBytes=$memory.Count;stackBase=$layout.stackBase;stackBytes=$layout.stackBytes;inputs=$inputs;hardwareExecuted=$false}
 $receipt.targetContract=[ordered]@{id=$targetContract.id;revision=$targetContract.revision;registrySha256=(Hash-File (Join-Path $PSScriptRoot 'TargetContracts.json'));startupSha256=$targetContract.startup.sha256;interruptVectors=$targetContract.interruptVectors;qualifiedMemory=$layout}
 $receipt.binary=[ordered]@{file='firmware.bin';sha256=(Hash-File (Join-Path $outputRoot 'firmware.bin'));baseAddress=$layout.codeBase;length=$binary.Length;gapFill=255}
 $receipt.interruptClosure=[ordered]@{schema=1;finalVerified=$true;addedEntries=@($interruptPlan.added);manifestSha256=(Hash-File (Join-Path $outputRoot 'interrupt-vectors.json'));scriptSha256=(Hash-File (Join-Path $PSScriptRoot 'InterruptVectors.ps1'))}
 $receipt.c251BitComplement=[bool]$C251BitComplement
 $receipt.orderedAsm=$orderedAsmRecord
 $receipt.wideCapacity=[ordered]@{schema=1;valueLimit=[int]$WideValueLimit;nodeLimit=[int]$WideNodeLimit;overflow='whole-function-fallback-with-reason';selection='explicit-build-option'}
 $receipt.wordCompareImmediate=[bool]$WordCompareImmediate
 $receipt.rangeNarrowMultiply=[bool]$RangeNarrowMultiply
 $receipt.calleeCostEvidence=if($CalleeCostFile){[ordered]@{path=$CalleeCostFile;sha256=$CalleeCostSha256}}else{$null}
 $receipt.volatileDataWide=[ordered]@{enabled=[bool]$VolatileDataWide;contract='experimental-explicit-allocated-DATA-full-object';atomicQualified=$false;hardwareQualified=$false}
 $receipt.objectAddressNative=$objectAddressRecord
 $receipt.constantRotateFusion=[bool]$ConstantRotateFusion
 $receipt.buildIdentity=[ordered]@{
  compilerPath=$compilerExe;compilerSha256=$receipt.compilerSha256
  toolchainRoot=$toolRoot;profileRoot=$ProfileRoot
  toolchainLock=$lock.toolchain;profileLock=$lock.profile
  buildScriptSha256=(Hash-File $PSCommandPath)
  targetProfileScriptSha256=(Hash-File (Join-Path $PSScriptRoot 'TargetProfile.ps1'))
  targetContractSha256=$receipt.targetContract.registrySha256
  c251BitComplement=[bool]$C251BitComplement
  orderedAsm=$orderedAsmRecord
  wideCapacity=$receipt.wideCapacity
  wordCompareImmediate=$receipt.wordCompareImmediate
  rangeNarrowMultiply=$receipt.rangeNarrowMultiply
  calleeCostEvidence=$receipt.calleeCostEvidence
  volatileDataWide=$receipt.volatileDataWide
  constantRotateFusion=$receipt.constantRotateFusion
  cmakeInputSha256=if($CMakeInput){Hash-File $CMakeInput}else{$null}
  abi=if($config.explicitAbi){'mcs251-r4-enabled'}else{'ordinary'}
 }
 . (Join-Path $PSScriptRoot 'CallEffects.ps1')
 $effects=New-Mx251CallEffects $outputRoot $config $receipt
 Write-Json (Join-Path $outputRoot 'call-effects.json') $effects
 $receipt.formatRuntime=[ordered]@{status=$formatRuntimeRecord.status;applied=$formatRuntimeRecord.applied;recordSha256=(Hash-File (Join-Path $outputRoot 'format-runtime.json'));scriptSha256=(Hash-File (Join-Path $PSScriptRoot 'FormatRuntime.ps1'));contractSha256=(Hash-File (Join-Path $PSScriptRoot 'FormatRuntimeContract.json'))}
 $receipt.startupRuntime=[ordered]@{status=$startupRuntimeRecord.status;applied=$startupRuntimeRecord.applied;recordSha256=(Hash-File (Join-Path $outputRoot 'startup-runtime.json'));scriptSha256=(Hash-File (Join-Path $PSScriptRoot 'StartupRuntime.ps1'));contractSha256=(Hash-File (Join-Path $PSScriptRoot 'StartupRuntimeContract.json'))}
 $receipt.callEffects=[ordered]@{schema=1;mode='audit-only';sha256=(Hash-File (Join-Path $outputRoot 'call-effects.json'));scriptSha256=(Hash-File (Join-Path $PSScriptRoot 'CallEffects.ps1'))}
 $receipt.scalarParameters=@{status=$scalarRecord.status;applied=$scalarRecord.applied;recordSha256=(Hash-File (Join-Path $outputRoot 'scalar-parameters.json'));scriptSha256=(Hash-File (Join-Path $PSScriptRoot 'ScalarParameters.ps1'))}
 $receipt.callClobberFeedback=[ordered]@{enabled=($null -ne $callClobberRecord);status=$(if($null -ne $callClobberRecord){$callClobberRecord.status}else{'EXPLICIT_LEAF_FEEDBACK_SELECTED'});recordSha256=$(if($null -ne $callClobberRecord){Hash-File (Join-Path $outputRoot 'call-clobber-feedback.json')}else{$null});scriptSha256=(Hash-File (Join-Path $PSScriptRoot 'CallClobberFeedback.ps1'))}
 $receipt.leafCallFeedback=[ordered]@{enabled=[bool]$LeafCallFeedback;finalValidated=($null -ne $leafFeedbackRecord -and $leafFeedbackRecord.finalValidated);recordSha256=if($LeafCallFeedback){Hash-File (Join-Path $outputRoot 'leaf-feedback.json')}else{$null};scriptSha256=(Hash-File (Join-Path $PSScriptRoot 'LeafCallFeedback.ps1'))}
 $receipt.functionGc=[ordered]@{enabled=[bool]$FunctionGc;record=$functionGcRecord;manifestSha256=if($FunctionGc){Hash-File (Join-Path $outputRoot 'function-gc/manifest.json')}else{$null};scriptSha256=if($FunctionGc){Hash-File (Join-Path $PSScriptRoot 'FunctionGc.ps1')}else{$null}}
 Write-Json (Join-Path $outputRoot 'receipt.json') $receipt
 Write-Json (Join-Path $outputRoot 'build-state.json') @{status='complete';firmware='firmware.hex';firmwareSha256=$receipt.firmwareSha256}
 Write-Output "Build complete: $outputRoot\firmware.hex ($($memory.Count) code bytes)"
 exit 0
} catch {
 $message=$_.Exception.Message
 if($ownsOutput){
  $linkError=Join-Path $outputRoot 'link.stderr.log'
  if([IO.File]::Exists($linkError)){
   $interruptDiagnostics=@(Get-Mx251InterruptDiagnostics ([IO.File]::ReadAllText($linkError)))
   if($interruptDiagnostics.Count){
    Write-Json (Join-Path $outputRoot 'interrupt-diagnostics.json') $interruptDiagnostics
    $message+=[Environment]::NewLine+(($interruptDiagnostics|ForEach-Object{"[$($_.code)] $($_.message)"}) -join [Environment]::NewLine)
   }
  }
  foreach($name in @('firmware.hex','firmware.bin','linked.partial.hex','receipt.json')){$path=Join-Path $outputRoot $name;if([IO.File]::Exists($path)){[IO.File]::Delete($path)}}
  Write-Json (Join-Path $outputRoot 'build-state.json') @{status='failed';error=$message}
 }
 [Console]::Error.WriteLine($message)
 exit 1
}
