#requires -Version 7.0
param(
 [Parameter(Mandatory=$true)][string]$ProjectPath,
 [Parameter(Mandatory=$true)][string]$CMake,
 [Parameter(Mandatory=$true)][string]$Ninja,
 [string]$PowerShell=(Join-Path $PSHOME 'pwsh.exe'),
 [ValidateSet('ON','OFF')][string]$FunctionGc='ON',
 [string[]]$FunctionGcKeep=@(),
 [ValidateSet('128','256','512')][string]$WideValueLimit='512',
 [ValidateSet('512','1024','2048')][string]$WideNodeLimit='2048',
 [switch]$C251BitComplement,
 [switch]$LeafCallFeedback,
 [switch]$VolatileDataWide,
 [switch]$ConstantRotateFusion,
 [switch]$WordCompareImmediate
)
$ErrorActionPreference='Stop'
Set-StrictMode -Version Latest
$core=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '..'))
$project=[IO.Path]::GetFullPath($ProjectPath)
$manifest=Get-Content -LiteralPath (Join-Path $core 'core.json') -Raw -Encoding UTF8|ConvertFrom-Json -AsHashtable
if($null -eq $manifest.compiler){throw 'Core is PREPARING: no new compiler has been bound.'}
$cfg=Get-Content -LiteralPath (Join-Path $project 'mx251.json') -Raw -Encoding UTF8|ConvertFrom-Json -AsHashtable
if($cfg.target -cnotin @('AI8051U-34K64-32','STC32G12K128')){throw 'Unknown target profile; no inferred capability.'}
foreach($file in @($CMake,$Ninja,$PowerShell,(Join-Path $project 'CMakeLists.txt'))){if(-not(Test-Path -LiteralPath $file -PathType Leaf)){throw "Missing explicit input: $file"}}
$compiler=Join-Path $core $manifest.compiler.path
if((Get-FileHash -LiteralPath $compiler -Algorithm SHA256).Hash.ToLowerInvariant() -cne $manifest.compiler.sha256){throw 'Core compiler hash mismatch'}
$build=Join-Path $project '.build/cmake-mx251-core'
New-Item -ItemType Directory -Path $build -Force|Out-Null
$success=Join-Path $build 'core-build.json'
if(Test-Path -LiteralPath $success){Remove-Item -LiteralPath $success}
$commands=[Collections.Generic.List[object]]::new()
function Run([string]$Label,[string[]]$Arguments){
 $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=[IO.Path]::GetFullPath($CMake)
 $start.WorkingDirectory=$project;$start.UseShellExecute=$false;$start.CreateNoWindow=$true
 $start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 # 外部遗留实验变量不能偷偷改变本次配置；直接使用低层模块时由调用者承担契约责任。
 foreach($key in @($start.Environment.Keys)){
  if($key.StartsWith('MX251_',[StringComparison]::OrdinalIgnoreCase) -or $key -in @('SDCC_HOME','SDCC_INCLUDE','SDCC_LIB','SDCC_BIN','SDCCLIB','SDCCDIR','CPATH','C_INCLUDE_PATH')){$start.Environment.Remove($key)|Out-Null}
 }
 foreach($arg in $Arguments){$start.ArgumentList.Add($arg)}
 $process=[Diagnostics.Process]::new();$process.StartInfo=$start
 try{
  if(-not $process.Start()){throw "Cannot start $Label"}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  if(-not $process.WaitForExit(1800000)){$process.Kill($true);$process.WaitForExit();throw "$Label timed out"}
  $out=$stdout.GetAwaiter().GetResult();$err=$stderr.GetAwaiter().GetResult()
  [IO.File]::WriteAllText((Join-Path $build "$Label.stdout.log"),$out,[Text.UTF8Encoding]::new($false))
  [IO.File]::WriteAllText((Join-Path $build "$Label.stderr.log"),$err,[Text.UTF8Encoding]::new($false))
  if($out){Write-Host $out};if($err){Write-Host $err}
  $commands.Add([ordered]@{label=$Label;executable=$start.FileName;arguments=$Arguments;exitCode=$process.ExitCode})
  [IO.File]::WriteAllText((Join-Path $build 'commands.json'),(ConvertTo-Json -InputObject @($commands.ToArray()) -Depth 20),[Text.UTF8Encoding]::new($false))
  if($process.ExitCode -ne 0){throw "$Label failed; prior firmware is not a result of this invocation."}
 }finally{$process.Dispose()}
}
$onoff={param($value) if($value){'ON'}else{'OFF'}}
$arguments=@('-S',$project,'-B',$build,'-G','Ninja',('-DCMAKE_MAKE_PROGRAM='+[IO.Path]::GetFullPath($Ninja)),
 ('-DMX251_PROJECT_MODULE='+ (Join-Path $core 'build/qualified/Project.cmake')),
 ('-DMX251_BUILD_SCRIPT='+ (Join-Path $core 'build/qualified/Build.ps1')),
 ('-DMX251_PWSH='+[IO.Path]::GetFullPath($PowerShell)),('-DMX251_TOOLCHAIN='+(Join-Path $core 'toolchain')),
 ('-DMX251_PROFILE='+(Join-Path $core ('profiles/'+$cfg.target))),('-DMX251_FUNCTION_GC='+$FunctionGc),
 ('-DMX251_FUNCTION_GC_KEEP='+($FunctionGcKeep -join ';')),('-DMX251_WIDE_VALUE_LIMIT='+$WideValueLimit),
 ('-DMX251_WIDE_NODE_LIMIT='+$WideNodeLimit),'-DMX251_ORDERED_ASM=AUTO',
 ('-DMX251_LEAF_CALL_FEEDBACK='+(&$onoff $LeafCallFeedback)),('-DMX251_VOLATILE_DATA_WIDE='+(&$onoff $VolatileDataWide)),
 ('-DMX251_CONSTANT_ROTATE_FUSION='+(&$onoff $ConstantRotateFusion)),('-DMX251_WORD_COMPARE_IMMEDIATE='+(&$onoff $WordCompareImmediate)),
 ('-DMX251_C251_BIT_COMPLEMENT='+(&$onoff $C251BitComplement)),
 '-DMX251_RANGE_NARROW_MULTIPLY=OFF','-DMX251_CALLEE_COST_FILE=','-DMX251_CALLEE_COST_SHA256=')
Run 'configure' $arguments
Run 'build' @('--build',$build,'--target','firmware')
$receiptPath=Join-Path $project '.build/mx251/latest/receipt.json'
$receipt=Get-Content -LiteralPath $receiptPath -Raw -Encoding UTF8|ConvertFrom-Json -AsHashtable
if($receipt.status -cne 'complete' -or $receipt.compilerSha256 -cne $manifest.compiler.sha256){throw 'Final compiler/receipt mismatch'}
$record=[ordered]@{status='complete';coreBuildId=$manifest.buildId;compilerSha256=$manifest.compiler.sha256;
 receipt=$receiptPath;receiptSha256=(Get-FileHash -LiteralPath $receiptPath -Algorithm SHA256).Hash.ToLowerInvariant();
 hardwareExecuted=$false;c251BitComplement=[bool]$C251BitComplement;volatileDataWide=[bool]$VolatileDataWide;functionGc=$FunctionGc;sourceVersion=$manifest.sourceVersion}
[IO.File]::WriteAllText($success,(ConvertTo-Json -InputObject $record -Depth 10),[Text.UTF8Encoding]::new($false))
Write-Host "MX251 build complete: $receiptPath"
