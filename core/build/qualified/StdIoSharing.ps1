# Choose the shared-library call form using complete, linked images. Compiler
# invocation reuse carries unchanged units/feedback stages into the trial.
function Invoke-Mx251StdIoSharing([string]$Root,$Config,$Receipt,[string]$BuildScript,$BuildParameters) {
 $record=@{schema=1;status='NO_SHARED_COST';applied=$false;savedBytes=0;beforeBytes=$Receipt.codeBytes;selectedBytes=$Receipt.codeBytes;units=@()}
 if($StdIoSharing -ceq 'OFF' -or $env:MX251_DEFER_NEAR_TRANSFERS -eq '1'){$record.status='OFF';return $record}
 $map=[IO.File]::ReadAllText((Join-Path $Root 'linked.partial.map'))
 if($map -cnotmatch '(?m)^C:\s+[0-9A-F]+\s+_printf\s') {return $record}
 $format=Get-Content -LiteralPath (Join-Path $Root 'format-runtime.json') -Raw|ConvertFrom-Json -AsHashtable
 $standardPuts=($format.ContainsKey('putsProvider') -and $format.putsProvider.implementation -ceq 'standard-source-current-backend')
 if(-not $standardPuts){
  $library=[regex]::Escape((Join-Path $toolRoot ('lib/mcs251-'+$Config.model+'-stack-auto/libsdcc.lib')).Replace('\','/'))
  $standardPuts=$map.Replace('\','/') -cmatch ($library+'\s*\[\s*puts\.rel\s*\]')
 }
 if(-not $standardPuts){return $record}
 foreach($u in $Config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.c'}){
  $asm=[IO.File]::ReadAllText((Join-Path $Root ($u.name+'.asm')))
  if($asm -cmatch '(?m)^; mx251-stdio-rewrites schema=1 printf-puts=[1-9][0-9]*\s*$' -and
     $asm -cmatch '(?m)^\s*(?:ecall|[a-z]*jmp)\s+_puts\s*(?:;[^\r\n]*)?$'){$record.units+=@($u.name)}
 }
 if(-not $record.units.Count){return $record}
 $trial=Join-Path $Root 'stdio-sharing';$candidate=Join-Path $trial 'candidate'
 [IO.Directory]::CreateDirectory($trial)|Out-Null
 $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=[Environment]::ProcessPath
 $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.Environment['MX251_COMPILE_REUSE']=$script:compileReuseRoot
 $start.Environment['MX251_STDIO_PRINTF_UNITS']=$record.units -join ','
 foreach($arg in @('-NoProfile','-NonInteractive','-File',$BuildScript,'-OutputDirectory',$candidate,'-StdIoSharing','OFF')){$start.ArgumentList.Add($arg)}
 foreach($key in $BuildParameters.Keys){
  if($key -in @('OutputDirectory','ManagedOutput','StdIoSharing')){continue}
  $value=$BuildParameters[$key]
  if($value -is [Management.Automation.SwitchParameter]){
   if($value.IsPresent){$start.ArgumentList.Add('-'+$key)}
   elseif($key -ceq 'WordCompareImmediate'){$start.ArgumentList.Add('-WordCompareImmediate:$false')}
  }else{$start.ArgumentList.Add('-'+$key);$start.ArgumentList.Add([string]$value)}
 }
 $record.arguments=@($start.ArgumentList);$process=[Diagnostics.Process]::new();$process.StartInfo=$start
 try {
  if(-not $process.Start()){throw 'stdio-choice-start-failed'}
  $stdout=$process.StandardOutput.ReadToEndAsync();$stderr=$process.StandardError.ReadToEndAsync()
  if(-not $process.WaitForExit(600000)){$process.Kill($true);$process.WaitForExit();throw 'stdio-choice-build-timeout'}
  [IO.File]::WriteAllText((Join-Path $trial 'build.stdout.log'),$stdout.GetAwaiter().GetResult())
  [IO.File]::WriteAllText((Join-Path $trial 'build.stderr.log'),$stderr.GetAwaiter().GetResult())
  if($process.ExitCode -ne 0){throw 'stdio-choice-build-failed'}
  $after=Get-Content -LiteralPath (Join-Path $candidate 'receipt.json') -Raw|ConvertFrom-Json -AsHashtable
  foreach($key in @('compilerSha256','profileLockSha256','projectFileSha256')){if($after[$key] -cne $Receipt[$key]){throw 'stdio-choice-input-changed'}}
  $record.printfBytes=$after.codeBytes;$record.printfFirmwareSha256=$after.firmwareSha256
  $record.status='KEEP_PUTS'
  $commands=Get-Content -LiteralPath (Join-Path $candidate 'commands.json') -Raw|ConvertFrom-Json -AsHashtable
  $record.reusedInvocations=@($commands|Where-Object{$_.ContainsKey('compileReuse')}).Count
  if($after.codeBytes -lt $Receipt.codeBytes){
   $backup=Join-Path $trial 'baseline';[IO.Directory]::CreateDirectory($backup)|Out-Null
   $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
   foreach($item in @(Get-ChildItem -LiteralPath $Root -Force|Where-Object{$_.Name -cnotin @('owner.json','stdio-sharing','compile-reuse')})){
    $destination=[IO.Path]::GetFullPath((Join-Path $backup $item.Name))
    if(-not $item.FullName.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or -not $destination.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'stdio-choice-output-path'}
    Move-Item -LiteralPath $item.FullName -Destination $destination
   }
   foreach($item in Get-ChildItem -LiteralPath $candidate -Force){Copy-Item -LiteralPath $item.FullName -Destination $Root -Recurse}
   $record.status='SHARED_PRINTF';$record.applied=$true;$record.selectedBytes=$after.codeBytes;$record.savedBytes=$Receipt.codeBytes-$after.codeBytes
  }
 }catch{
  if(Test-Path -LiteralPath (Join-Path $trial 'baseline')){throw}
  $record.status='KEEP_PUTS';$record.reason=$_.Exception.Message;$record.diagnostic=$_.ScriptStackTrace
 }finally{$process.Dispose()}
 return $record
}
