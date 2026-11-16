# Joint internal ABI trial, using compiler-emitted transport signatures.
. (Join-Path $PSScriptRoot 'CallTransportGroup.ps1')
function Assert-Mx251LeafAbiBinding([string]$Root,$Config,$Selected){
 $old=$Selected.symbol;$name='___m251r4_'+$old.Substring(1);$escaped=[regex]::Escape($name)
 $image=Get-Mx251FormatHex (Join-Path $Root 'firmware.hex');$definitions=@();$calls=@();$abi=0
 foreach($u in $Config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.c' -and $_.name -cin $Selected.units}){
  $lines=[IO.File]::ReadAllLines((Join-Path $Root ($u.name+'.asm')))
  $expected=0
  foreach($raw in $lines){
   if($raw -cmatch ('^;\s*mx251-bank-contract schema=1 symbol='+$escaped+' abi=4 bank=0 naked=0\s*$')){$abi++}
   $code=Get-Mx251GcText $raw
   if($old -cin @(Get-Mx251GcTokens $code)){throw 'leaf-abi-old-symbol-remains'}
   if($code -cmatch ('^ecall\s+'+$escaped+'$')){$expected++}
  }
  $found=0
  foreach($line in [IO.File]::ReadAllLines((Join-Path $Root ($u.name+'.rst')))){
   if($line.Length -lt 35 -or $line.Substring(6,6) -cnotmatch '^[0-9A-F]{6}$'){continue}
   $source=[regex]::Match($line.Substring(34),'^\s*(\d+) (.*)$');if(-not $source.Success){continue}
   $code=Get-Mx251GcText $source.Groups[2].Value
   if($code -cnotmatch ('^(?:'+$escaped+':{1,2}|ecall\s+'+$escaped+')$')){continue}
   $number=[int]$source.Groups[1].Value
   if($number -lt 1 -or $number -gt $lines.Count -or $source.Groups[2].Value.TrimEnd() -cne $lines[$number-1].TrimEnd()){throw 'leaf-abi-listing-source-mismatch'}
   $address=[Convert]::ToInt64($line.Substring(6,6),16)
   if($code -cmatch ('^'+$escaped+':')){$definitions+=@($address);continue}
   $bytes=[Convert]::FromHexString($line.Substring(13,15).Replace(' ',''))
   if($bytes.Length -ne 4 -or $bytes[0] -ne 0x9a){throw 'leaf-abi-call-encoding'}
   for($i=0;$i -lt 4;$i++){if(-not $image.ContainsKey($address+$i) -or $image[$address+$i] -ne $bytes[$i]){throw 'leaf-abi-listing-image-mismatch'}}
   $calls+=@([long]$bytes[1]*65536+[long]$bytes[2]*256+$bytes[3]);$found++
  }
  if($found -ne $expected){throw 'leaf-abi-call-listing-incomplete'}
 }
 if($abi -ne 1 -or $definitions.Count -ne 1 -or -not $calls.Count -or @($calls|Where-Object{$_ -ne $definitions[0]}).Count){throw 'leaf-abi-final-binding'}
 return @{symbol=$name;address=$definitions[0];calls=$calls.Count}
}

function Invoke-Mx251SelectiveLeafAbi([string]$Root,$Config,$Receipt,[string]$ToolRoot,[string]$ProfileRoot,[string]$BuildScript,$BuildParameters){
 $record=@{schema=1;status='NO_CANDIDATE';applied=$false;baselineCodeBytes=$Receipt.codeBytes;selectedCodeBytes=$Receipt.codeBytes;savedBytes=0;candidates=@();hardwareExecuted=$false;runtimeRatio=$null}
 $record.selectorSha256=Hash-File (Join-Path $PSScriptRoot 'CallTransportGroup.ps1')
 if($SelectiveLeafAbi -ceq 'OFF'){$record.status='OFF';return $record}
 if($env:MX251_INTERNAL_ABI4_CONTRACT){$record.status='EXPLICIT_CONTRACT';return $record}
 $keep=@($FunctionGcKeep.Split(',',[StringSplitOptions]::RemoveEmptyEntries))
 if($Receipt.functionGc.enabled){$gc=Get-Content -LiteralPath (Join-Path $Root 'function-gc/manifest.json') -Raw|ConvertFrom-Json -AsHashtable;$keep+=@($gc.roots|Where-Object{$_.StartsWith('fn:')}|ForEach-Object{$_.Split(':',3)[2]})}
 $record.candidates=@(Get-Mx251LeafAbiCandidates $Root $Config $ToolRoot $ProfileRoot $keep)
 if(-not $record.candidates.Count){return $record}
 $chosen=@($record.candidates|Select-Object -First 16);$record.chosen=$chosen
 $trial=Join-Path $Root 'leaf-abi';$candidate=Join-Path $trial 'candidate'
 [IO.Directory]::CreateDirectory($trial)|Out-Null
 $contract=Join-Path $trial 'internal-abi4.txt'
 [IO.File]::WriteAllText($contract,("MX251-INTERNAL-ABI4-V3`n"+(@($chosen|ForEach-Object{$_.scope+"`t"+$_.symbol+"`t"+$_.transport}) -join "`n")+"`n"),[Text.UTF8Encoding]::new($false))
 $record.contractSha256=Hash-File $contract
 $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=[Environment]::ProcessPath
 $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.Environment['MX251_INTERNAL_ABI4_CONTRACT']=$contract
 $start.Environment['MX251_DEFER_NEAR_TRANSFERS']='1'
 $start.Environment['MX251_COMPILE_REUSE']=$script:compileReuseRoot
 foreach($arg in @('-NoProfile','-NonInteractive','-File',$BuildScript,'-OutputDirectory',$candidate,'-SelectiveLeafAbi','OFF')){$start.ArgumentList.Add($arg)}
 foreach($key in $BuildParameters.Keys){
  if($key -in @('OutputDirectory','ManagedOutput','SelectiveLeafAbi')){continue}
  $value=$BuildParameters[$key]
  if($value -is [Management.Automation.SwitchParameter]){
   if($value.IsPresent){$start.ArgumentList.Add('-'+$key)}
   elseif($key -ceq 'WordCompareImmediate'){$start.ArgumentList.Add('-WordCompareImmediate:$false')}
  }
  else{$start.ArgumentList.Add('-'+$key);$start.ArgumentList.Add([string]$value)}
 }
 $record.arguments=@($start.ArgumentList);$process=[Diagnostics.Process]::new();$process.StartInfo=$start
 try{
  if(-not $process.Start()){throw 'leaf-abi-cannot-start-build'}
  $out=$process.StandardOutput.ReadToEndAsync();$err=$process.StandardError.ReadToEndAsync()
  if(-not $process.WaitForExit(600000)){$process.Kill($true);$process.WaitForExit();throw 'leaf-abi-build-timeout'}
  [IO.File]::WriteAllText((Join-Path $trial 'build.stdout.log'),$out.GetAwaiter().GetResult())
  [IO.File]::WriteAllText((Join-Path $trial 'build.stderr.log'),$err.GetAwaiter().GetResult())
  if($process.ExitCode -ne 0){throw 'leaf-abi-candidate-build-failed'}
  $after=Get-Content -LiteralPath (Join-Path $candidate 'receipt.json') -Raw|ConvertFrom-Json -AsHashtable
  foreach($key in @('compilerSha256','profileLockSha256','projectFileSha256')){if($after[$key] -cne $Receipt[$key]){throw 'leaf-abi-build-input-changed'}}
  if($after.buildIdentity.cmakeInputSha256 -cne $Receipt.buildIdentity.cmakeInputSha256){throw 'leaf-abi-cmake-input-changed'}
  foreach($key in $Receipt.inputs.Keys){if($after.inputs[$key].sha256 -cne $Receipt.inputs[$key].sha256){throw 'leaf-abi-source-changed'}}
  $record.finalBinding=@(foreach($member in $chosen){Assert-Mx251LeafAbiBinding $candidate $Config $member})
  $record.candidateCodeBytes=$after.codeBytes;$record.candidateFirmwareSha256=$after.firmwareSha256
  $record.status='KEEP_BASELINE'
  if($after.codeBytes -lt $Receipt.codeBytes){
   # Keep both original build directories, including their receipts and logs.
   # Publish every candidate artifact together; owner.json belongs to the outer
   # managed build. No paths outside this output directory are moved.
   $backup=Join-Path $trial 'baseline';[IO.Directory]::CreateDirectory($backup)|Out-Null
   $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
   foreach($item in @(Get-ChildItem -LiteralPath $Root -Force|Where-Object{$_.Name -cnotin @('owner.json','leaf-abi','compile-reuse')})){
    $destination=[IO.Path]::GetFullPath((Join-Path $backup $item.Name))
    if(-not $item.FullName.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase) -or -not $destination.StartsWith($prefix,[StringComparison]::OrdinalIgnoreCase)){throw 'leaf-abi-output-path'}
    Move-Item -LiteralPath $item.FullName -Destination $destination
   }
   foreach($item in Get-ChildItem -LiteralPath $candidate -Force){Copy-Item -LiteralPath $item.FullName -Destination $Root -Recurse}
   $record.applied=$true;$record.status='SMALLER_IMAGE';$record.selectedCodeBytes=$after.codeBytes;$record.savedBytes=$Receipt.codeBytes-$after.codeBytes
  }
 }catch{
  # A failed compiler trial leaves its diagnostics and the original image.
  # Publication failures must reach the outer build's failure handler.
  if(Test-Path -LiteralPath (Join-Path $trial 'baseline')){throw}
  $record.status='KEEP_BASELINE';$record.reason=$_.Exception.Message;$record.diagnostic=$_.ScriptStackTrace
 }finally{$process.Dispose()}
 return $record
}
