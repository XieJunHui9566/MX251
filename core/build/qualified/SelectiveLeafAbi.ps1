# One closed function per build; use the existing ABI4 compiler contract and compare
# complete linked images. Call frequency only ranks trials, not claimed savings.
function Get-Mx251LeafAbiCandidates([string]$Root,$Config,[string]$ToolRoot,[string]$ProfileRoot,[string[]]$Keep=@()) {
 if($Config.explicitAbi -or $Config.runtime -cne 'standard' -or @($Config.archives).Count){return}
 $cUnits=@($Config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.c'})
 $rawUnits=@($Config.units|Where-Object{[IO.Path]::GetExtension($_.file) -cne '.c'})
 if(@($cUnits|Where-Object{$_.backend -cne 'ordinary-wide' -or -not $_.stackAuto}).Count){return}
 $definitions=[hashtable]::new([StringComparer]::Ordinal);$texts=[hashtable]::new([StringComparer]::Ordinal);$calls=@();$refs=[hashtable]::new([StringComparer]::Ordinal)
 $slots=[Collections.Generic.HashSet[int]]::new()
 foreach($u in $cUnits){
  $text=[IO.File]::ReadAllText((Join-Path $Root ($u.name+'.asm')))
  if($text -cmatch '(?m)^; mx251-function begin .* (?:opaque|indirect)=1(?: |$)'){return}
  foreach($m in [regex]::Matches($text,'(?m)^; mx251-irq-definition schema=2 symbol=\S+ slot=([0-9]+) private=[01]\s*$')){$null=$slots.Add([int]$m.Groups[1].Value)}
  $texts[$u.name]=$text
  $rel=Read-Mx251GcRel ([IO.File]::ReadAllBytes((Join-Path $Root ($u.name+'.rel')))) $u.name
  foreach($name in $rel.Definitions){if(-not $refs.ContainsKey($name)){$refs[$name]=0};$refs[$name]++}
  foreach($m in [regex]::Matches($text,'(?ms)^; mx251-function begin schema=1 symbol=(\S+) isr=([01]) addressTaken=([01]) opaque=([01]) indirect=([01])\s*\r?\n(.*?)^; mx251-function end schema=1 symbol=\1\s*$')){
   $name=$m.Groups[1].Value;$body=$m.Groups[6].Value;$escaped=[regex]::Escape($name)
   $sig=[regex]::Matches($body,'(?m)^; mx251-scalar-definition schema=1 symbol='+$escaped+' signature=(\S+)\s*$')
   $abi=[regex]::Matches($body,'(?m)^;\s*mx251-bank-contract schema=1 symbol='+$escaped+' abi=2 bank=0 naked=0\s*$')
   if($sig.Count -ne 1 -or $abi.Count -ne 1 -or $m.Groups[2].Value -ne '0' -or $m.Groups[3].Value -ne '0'){continue}
   if($text -cnotmatch ('(?m)^\s*\.globl\s+'+$escaped+'\s*$')){continue}
   if(-not $definitions.ContainsKey($name)){$definitions[$name]=@()}
   $definitions[$name]+=@{symbol=$name;unit=$u.name;signature=$sig[0].Groups[1].Value}
  }
  foreach($m in [regex]::Matches($text,'(?m)^; mx251-scalar-call schema=1 owner=(\S+) target=(\S+) signature=(\S+) values=\S+\s*$')){
   $calls+=@{unit=$u.name;owner=$m.Groups[1].Value;target=$m.Groups[2].Value;signature=$m.Groups[3].Value}
  }
 }
 # Numeric vector stubs have no function ABI. Reuse the existing vector
 # analysis to distinguish registered ISR entrances from arbitrary raw code.
 if($rawUnits.Count){
  try{
   $vectorHome=Get-Mx251FormatHome (Join-Path $Root 'linked.partial.map')
   foreach($u in $rawUnits){$null=Get-Mx251FormatRawVectors $Root $u $vectorHome $slots}
  }catch{return}
 }
 $excluded=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 foreach($name in $Keep){$null=$excluded.Add($name)}
 foreach($file in @((Get-ChildItem -LiteralPath (Join-Path $ToolRoot 'lib') -Directory|Where-Object{$_.Name -like 'mcs251*'}|Get-ChildItem -Recurse -File -Filter '*.lib'))+@(Get-ChildItem -LiteralPath $ProfileRoot -Recurse -File -Filter '*.lib')){
  foreach($member in @(Read-Mx251GcArchive $file.FullName)){foreach($name in $member.rel.References){$null=$excluded.Add($name)}}
 }
 foreach($line in [IO.File]::ReadAllLines((Join-Path $Root 'startup.asm'))){foreach($name in @(Get-Mx251GcTokens (Get-Mx251GcText $line))){$null=$excluded.Add($name)}}
 $selected=@()
 foreach($name in $definitions.Keys){
  if($definitions[$name].Count -ne 1 -or $refs[$name] -ne 1 -or $excluded.Contains($name)){continue}
  $d=$definitions[$name][0]
  $signature=[regex]::Match($d.signature,'^(v|[iu](?:8|16|32))\(([iu](?:8|16|32)),([iu](?:8|16|32))\)$')
  if(-not $signature.Success){continue}
  $escaped=[regex]::Escape($name);$bad=$false;$machine=[hashtable]::new([StringComparer]::Ordinal)
  foreach($unit in $texts.Keys){
   $owner=''
   foreach($raw in $texts[$unit].Split("`n")){
    if($raw -cmatch '^; mx251-function begin schema=1 symbol=(\S+) '){$owner=$Matches[1]}
    if($raw -cmatch '^; mx251-function end '){$owner=''}
    $code=Get-Mx251GcText $raw
    if($name -cnotin @(Get-Mx251GcTokens $code)){continue}
    if($code -cmatch ('^(?:'+$escaped+':{1,2}|\.globl\s+'+$escaped+')$')){continue}
    if($owner -and $code -cmatch ('^ecall\s+'+$escaped+'$')){
     $key=$unit+':'+$owner;if(-not $machine.ContainsKey($key)){$machine[$key]=0};$machine[$key]++
    }else{$bad=$true}
   }
  }
  $edges=@($calls|Where-Object{$_.target -ceq $name})
  if($bad -or @($edges|Where-Object{$_.signature -cne $d.signature}).Count){continue}
  $typed=[hashtable]::new([StringComparer]::Ordinal);foreach($edge in $edges){$key=$edge.unit+':'+$edge.owner;if(-not $typed.ContainsKey($key)){$typed[$key]=0};$typed[$key]++}
  # IR metadata precedes branch sharing and dead-code removal; bind each
  # surviving machine call to its owner/target signature, not the old count.
  foreach($key in $machine.Keys){if(-not $typed.ContainsKey($key)){$bad=$true}}
  $count=0;foreach($n in $machine.Values){$count+=$n}
  # Sixteen call sites justify one whole-program trial. Rejected trials never
  # change the published image. Wider second arguments rank before narrower.
  if($bad -or $count -lt 16){continue}
  $ret=if($signature.Groups[1].Value -ceq 'v'){'void'}else{$signature.Groups[1].Value}
  $d.transport=$ret+'('+$signature.Groups[2].Value+','+$signature.Groups[3].Value+')'
  $d.calls=$count;$d.rank=$count*[int]$signature.Groups[3].Value.Substring(1)
  $selected+=$d
 }
 return @($selected|Sort-Object @{Expression={$_.rank};Descending=$true},symbol)
}

function Assert-Mx251LeafAbiBinding([string]$Root,$Config,$Selected){
 $old=$Selected.symbol;$name='___m251r4_'+$old.Substring(1);$escaped=[regex]::Escape($name)
 $image=Get-Mx251FormatHex (Join-Path $Root 'firmware.hex');$definitions=@();$calls=@();$abi=0
 foreach($u in $Config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.c'}){
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
 if($SelectiveLeafAbi -ceq 'OFF'){$record.status='OFF';return $record}
 if($env:MX251_INTERNAL_ABI4_CONTRACT){$record.status='EXPLICIT_CONTRACT';return $record}
 $keep=@($FunctionGcKeep.Split(',',[StringSplitOptions]::RemoveEmptyEntries))
 if($Receipt.functionGc.enabled){$gc=Get-Content -LiteralPath (Join-Path $Root 'function-gc/manifest.json') -Raw|ConvertFrom-Json -AsHashtable;$keep+=@($gc.roots|Where-Object{$_.StartsWith('fn:')}|ForEach-Object{$_.Split(':',3)[2]})}
 $record.candidates=@(Get-Mx251LeafAbiCandidates $Root $Config $ToolRoot $ProfileRoot $keep)
 if(-not $record.candidates.Count){return $record}
 $chosen=$record.candidates[0];$record.chosen=$chosen
 $trial=Join-Path $Root 'leaf-abi';$candidate=Join-Path $trial 'candidate'
 [IO.Directory]::CreateDirectory($trial)|Out-Null
 $contract=Join-Path $trial 'internal-abi4.txt'
 [IO.File]::WriteAllText($contract,("MX251-INTERNAL-ABI4-V2`n"+$chosen.symbol+"`t"+$chosen.transport+"`n"),[Text.UTF8Encoding]::new($false))
 $record.contractSha256=Hash-File $contract
 $start=[Diagnostics.ProcessStartInfo]::new();$start.FileName=[Environment]::ProcessPath
 $start.UseShellExecute=$false;$start.CreateNoWindow=$true;$start.RedirectStandardOutput=$true;$start.RedirectStandardError=$true
 $start.Environment['MX251_INTERNAL_ABI4_CONTRACT']=$contract
 $start.Environment['MX251_DEFER_NEAR_TRANSFERS']='1'
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
  $record.finalBinding=Assert-Mx251LeafAbiBinding $candidate $Config $chosen
  $record.candidateCodeBytes=$after.codeBytes;$record.candidateFirmwareSha256=$after.firmwareSha256
  $record.status='KEEP_BASELINE'
  if($after.codeBytes -lt $Receipt.codeBytes){
   # Keep both original build directories, including their receipts and logs.
   # Publish every candidate artifact together; owner.json belongs to the outer
   # managed build. No paths outside this output directory are moved.
   $backup=Join-Path $trial 'baseline';[IO.Directory]::CreateDirectory($backup)|Out-Null
   $prefix=[IO.Path]::GetFullPath($Root).TrimEnd('\')+'\'
   foreach($item in @(Get-ChildItem -LiteralPath $Root -Force|Where-Object{$_.Name -cnotin @('owner.json','leaf-abi')})){
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
