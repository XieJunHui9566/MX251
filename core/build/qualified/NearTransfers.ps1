# Same-region transfers are selected after all compiler/runtime/ABI trials.
# Register-only entries keep their local frame layout when the return address
# shrinks from three bytes to two. Stack-parameter and escaping entries stay far.
function Get-Mx251NearUnits([string]$Root,$Config) {
 $units=@()
 foreach($u in $Config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.c'}){
  $lines=[IO.File]::ReadAllLines((Join-Path $Root ($u.name+'.asm')))
  $owners=@{};$functions=[hashtable]::new([StringComparer]::Ordinal);$labels=[hashtable]::new([StringComparer]::Ordinal);$references=[hashtable]::new([StringComparer]::Ordinal);$owner=''
  for($i=0;$i -lt $lines.Count;$i++){
   $raw=$lines[$i];$code=Get-Mx251GcText $raw
   if($raw -cmatch '^; mx251-function begin schema=1 symbol=(\S+) isr=([01]) addressTaken=([01]) opaque=([01]) indirect=([01])$'){
    $owner=$Matches[1]
    $functions[$owner]=@{symbol=$owner;unit=$u.name;isr=[int]$Matches[2];addressTaken=[int]$Matches[3];opaque=[int]$Matches[4];indirect=[int]$Matches[5];begin=$i;end=$i;signature='';abi=0;bank=-1;naked=1;returns=@();calls=@();branches=@();indirectJump=$false;directives=$false}
   }
   $owners[$i]=$owner
   foreach($token in @(Get-Mx251GcTokens $code)){
    if(-not $references.ContainsKey($token)){$references[$token]=[Collections.Generic.List[object]]::new()}
    $references[$token].Add(@{line=$i;code=$code;owner=$owner})
   }
   if($code -cmatch '^([A-Za-z_.$0-9]+):{1,2}$'){$labels[$Matches[1]]=@{owner=$owner;line=$i}}
   if($owner){
    $f=$functions[$owner];$f.end=$i
    if($raw -cmatch '^; mx251-(?:scalar|near)-definition schema=1 symbol=(\S+) signature=(\S+)$'){
     if($Matches[1] -ceq $owner){$f.signature=$Matches[2]}
    }
    if($raw -cmatch '^;\s*mx251-bank-contract schema=1 symbol=(\S+) abi=(\d+) bank=(\d+) naked=([01])$'){
     if($Matches[1] -ceq $owner){$f.abi=[int]$Matches[2];$f.bank=[int]$Matches[3];$f.naked=[int]$Matches[4]}
    }
    if($code -ceq 'eret'){$f.returns+=@($i)}
    if($code -cmatch '^(?:e|l|s|a)?jmp\s+@'){$f.indirectJump=$true}
    if($code.StartsWith('.') -and $code -cnotmatch '^\.globl\s+'){$f.directives=$true}
    if($code -cmatch '(?:^|[,\s])(?:\.|\$)\s*[+-]\s*\d'){$f.directives=$true}
    if($code -cmatch '^(ejmp|ljmp|sjmp|ajmp)\s+(\S+)$'){$f.branches+=@{line=$i;op=$Matches[1];target=$Matches[2]}}
    if($raw -cmatch '^; mx251-(?:scalar|near)-call schema=1 owner=(\S+) target=(\S+) signature=(\S+)(?: |$)'){$f.calls+=@{target=$Matches[2];signature=$Matches[3]}}
   }
   if($raw -cmatch '^; mx251-function end schema=1 '){$owner=''}
  }
  $units+=@{name=$u.name;lines=$lines;owners=$owners;functions=$functions;labels=$labels;references=$references}
 }
 return $units
}

function Get-Mx251NearPlan($Units,[string[]]$Excluded) {
 if($null -eq $Excluded){$Excluded=@()}
 $definitions=[hashtable]::new([StringComparer]::Ordinal);$excludedSet=[Collections.Generic.HashSet[string]]::new([string[]]$Excluded,[StringComparer]::Ordinal)
 foreach($u in $Units){foreach($name in $u.functions.Keys){
  if(-not $definitions.ContainsKey($name)){$definitions[$name]=@()};$definitions[$name]+=@($u.functions[$name])
 }}
 $selected=@();$changes=@()
 foreach($u in $Units){foreach($f in $u.functions.Values){
  # Fixed-width jump tables and inline assembly retain their original layout.
  if(-not $f.opaque -and -not $f.indirectJump -and -not $f.directives){
   foreach($b in $f.branches){if($b.op -ceq 'ejmp' -and $u.labels.ContainsKey($b.target) -and $u.labels[$b.target].owner -ceq $f.symbol){
    $changes+=@{unit=$u.name;line=$b.line;from='ejmp';to='ljmp';target=$b.target;owner=$f.symbol}
   }}
  }
  if($definitions[$f.symbol].Count -ne 1 -or $excludedSet.Contains($f.symbol) -or
     $f.isr -or $f.addressTaken -or $f.opaque -or $f.indirect -or $f.indirectJump -or $f.directives -or
     $f.abi -notin @(2,4) -or $f.bank -ne 0 -or $f.naked -or -not $f.returns.Count){continue}
  $sig=[regex]::Match($f.signature,'^(?:v|[iu](?:8|16|32)|f32|p3_\d+)\((?<args>(?:(?:[iu](?:8|16|32)|f32|p3_\d+)(?:,(?:[iu](?:8|16|32)|f32|p3_\d+))*)?)\)$')
  if(-not $sig.Success){continue}
  $arity=if($sig.Groups['args'].Value){$sig.Groups['args'].Value.Split(',').Count}else{0}
  if($arity -gt $(if($f.abi -eq 2){1}else{2})){continue}
  if(@($f.branches|Where-Object{-not $u.labels.ContainsKey($_.target) -or $u.labels[$_.target].owner -cne $f.symbol}).Count){continue}
  $calls=@();$bad=$false;$name=[regex]::Escape($f.symbol)
  foreach($other in $Units){
   if(-not $other.references.ContainsKey($f.symbol)){continue}
   foreach($reference in $other.references[$f.symbol]){
    $i=$reference.line;$code=$reference.code
    if($code -cmatch ('^(?:'+$name+':{1,2}|\.globl\s+'+$name+')$')){continue}
    $owner=$reference.owner
    if($code -cnotmatch ('^ecall\s+'+$name+'$') -or -not $owner -or $other.functions[$owner].opaque -or $other.functions[$owner].directives){$bad=$true;break}
    $typed=@($other.functions[$owner].calls|Where-Object{$_.target -ceq $f.symbol})
    if(-not $typed.Count -or @($typed|Where-Object{$_.signature -cne $f.signature}).Count){$bad=$true;break}
    $calls+=@{unit=$other.name;line=$i;from='ecall';to='lcall';target=$f.symbol;owner=$owner}
   }
   if($bad){break}
  }
  if($bad -or -not $calls.Count){continue}
  $selected+=@{symbol=$f.symbol;unit=$u.name;signature=$f.signature;abi=$f.abi;calls=$calls.Count;returns=$f.returns.Count}
  $changes+=$calls
  foreach($line in $f.returns){$changes+=@{unit=$u.name;line=$line;from='eret';to='ret';target='';owner=$f.symbol}}
 }}
 return @{functions=$selected;changes=$changes;calls=@($changes|Where-Object{$_.from -ceq 'ecall'}).Count;jumps=@($changes|Where-Object{$_.from -ceq 'ejmp'}).Count}
}

function Assert-Mx251NearImage([string]$Root,$Plan,$Image,[long]$CodeBase,[long]$CodeBytes) {
 $region=$CodeBase -shr 16;$bindings=@();$labels=[hashtable]::new([StringComparer]::Ordinal)
 foreach($group in $Plan.changes|Group-Object unit){
  $lines=[IO.File]::ReadAllLines((Join-Path $Root ($group.Name+'.asm')))
  $wanted=@{};foreach($c in $group.Group){$wanted[$c.line+1]=$c}
  foreach($raw in [IO.File]::ReadAllLines((Join-Path $Root ($group.Name+'.rst')))){
   if($raw.Length -lt 35 -or $raw.Substring(6,6) -cnotmatch '^[0-9A-F]{6}$'){continue}
   $source=[regex]::Match($raw.Substring(34),'^\s*(\d+) (.*)$');if(-not $source.Success){continue}
   $number=[int]$source.Groups[1].Value;$code=Get-Mx251GcText $source.Groups[2].Value
   $address=[Convert]::ToInt64($raw.Substring(6,6),16)
   if($code -cmatch '^([A-Za-z_.$0-9]+):{1,2}$'){$labels[$group.Name+':'+$Matches[1]]=$address}
   if(-not $wanted.ContainsKey($number)){continue}
   $c=$wanted[$number]
   if($source.Groups[2].Value.TrimEnd() -cne $lines[$number-1].TrimEnd()){throw 'near-listing-source-mismatch'}
   $bytes=[Convert]::FromHexString($raw.Substring(13,15).Replace(' ',''))
   $opcode=switch($c.to){'ljmp'{2};'lcall'{18};'ret'{34}}
   $length=if($c.to -ceq 'ret'){1}else{3}
   if($bytes.Length -ne $length -or $bytes[0] -ne $opcode -or (($address+$length) -shr 16) -ne $region){throw 'near-transfer-encoding-or-region'}
   for($i=0;$i -lt $bytes.Length;$i++){if($Image[$address+$i] -ne $bytes[$i]){throw 'near-listing-image-mismatch'}}
   $bindings+=@{unit=$group.Name;line=$number;op=$c.to;owner=$c.owner;target=$c.target;address=$address;encodedTarget=$(if($length -eq 3){($region -shl 16)+[int]$bytes[1]*256+$bytes[2]}else{$null})}
   $wanted.Remove($number)
  }
  if($wanted.Count){throw 'near-transfer-missing-listing'}
 }
 foreach($b in $bindings|Where-Object{$_.op -cne 'ret'}){
  $unit=$b.unit
  if($b.op -ceq 'lcall'){$unit=@($Plan.functions|Where-Object{$_.symbol -ceq $b.target})[0].unit}
  if(-not $labels.ContainsKey($unit+':'+$b.target) -or $b.encodedTarget -ne $labels[$unit+':'+$b.target]){throw 'near-transfer-target-mismatch'}
 }
 foreach($address in $Image.Keys){if($address -lt $CodeBase -or $address -ge $CodeBase+$CodeBytes){throw 'near-image-outside-code-region'}}
 return $bindings
}

function Invoke-Mx251NearTransfers([string]$Root,$Config,$Receipt,[string]$ToolRoot,[string]$ProfileRoot,[string]$Mode) {
 $record=@{schema=1;status='OFF';applied=$false;savedBytes=0;functions=@();calls=0;jumps=0;returnAddressBytes=2;baselineCodeBytes=$Receipt.codeBytes}
 if($Mode -ceq 'OFF'){return $record}
 if($env:MX251_DEFER_NEAR_TRANSFERS -ceq '1'){$record.status='DEFERRED_TO_OUTER_BUILD';return $record}
 $record.status='NO_CANDIDATE'
 $layout=$Receipt.targetContract.qualifiedMemory
 if(($layout.codeBase -shr 16) -ne (($layout.codeBase+$layout.codeBytes-1) -shr 16)){$record.status='MULTIPLE_CODE_REGIONS';return $record}
 if(@($Config.archives).Count){$record.status='PROJECT_ARCHIVES';return $record}
 $units=@(Get-Mx251NearUnits $Root $Config)
 $excluded=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 # Entry points referenced by startup, archives, data initializers, or opaque
 # assembly must keep their original return convention.
 foreach($raw in [IO.File]::ReadAllLines((Join-Path $Root 'startup.asm'))){foreach($name in @(Get-Mx251GcTokens (Get-Mx251GcText $raw))){$null=$excluded.Add($name)}}
 foreach($file in @((Get-ChildItem -LiteralPath (Join-Path $ToolRoot 'lib') -Recurse -File -Filter '*.lib'))+@(Get-ChildItem -LiteralPath $ProfileRoot -Recurse -File -Filter '*.lib')){
  foreach($m in [regex]::Matches([Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes($file.FullName)),'(?m)^S (\S+) Ref[0-9A-F]+\r?$')){$null=$excluded.Add($m.Groups[1].Value)}
 }
 if($Receipt.functionGc.enabled){
  $gc=Get-Content -LiteralPath (Join-Path $Root 'function-gc/manifest.json') -Raw|ConvertFrom-Json -AsHashtable
  foreach($name in $gc.roots|Where-Object{$_.StartsWith('fn:')}){$null=$excluded.Add($name.Split(':',3)[2])}
 }
 $slots=[Collections.Generic.HashSet[int]]::new()
 foreach($u in $units){foreach($raw in $u.lines){if($raw -cmatch '^; mx251-irq-definition schema=2 symbol=\S+ slot=(\d+) private=[01]$'){$null=$slots.Add([int]$Matches[1])}}}
 try {
  $nearHome=Get-Mx251FormatHome (Join-Path $Root 'linked.partial.map')
  foreach($u in $Config.units|Where-Object{[IO.Path]::GetExtension($_.file) -cne '.c'}){$null=Get-Mx251FormatRawVectors $Root $u $nearHome $slots}
 }catch{$record.status='RAW_ASSEMBLY_LAYOUT';$record.reason=$_.Exception.Message;return $record}
 $plan=Get-Mx251NearPlan $units @($excluded)
 $record.functions=$plan.functions;$record.calls=$plan.calls;$record.jumps=$plan.jumps
 if(-not $plan.changes.Count){return $record}
 $backup=Join-Path $Root 'near-baseline';$null=[IO.Directory]::CreateDirectory($backup)
 foreach($p in Get-ChildItem -LiteralPath $Root -File|Where-Object{$_.Name -like 'linked.partial.*' -or $_.Name -in @('firmware.hex','firmware.bin','receipt.json','commands.json','call-effects.json','selective-leaf-abi.json') -or $_.Extension -cin @('.asm','.rel','.rst','.lst','.sym')}){
  [IO.File]::Copy($p.FullName,(Join-Path $backup $p.Name),$false)
 }
 $prior=@($commands|Where-Object{$_.label -ceq 'link'})[-1]
 foreach($group in $plan.changes|Group-Object unit){
  $unit=@($units|Where-Object{$_.name -ceq $group.Name})[0]
  foreach($c in $group.Group){$unit.lines[$c.line]=[regex]::Replace($unit.lines[$c.line],('^(\s*)'+$c.from+'\b'),('${1}'+$c.to))}
  [IO.File]::WriteAllLines((Join-Path $Root ($group.Name+'.asm')),$unit.lines,[Text.UTF8Encoding]::new($false))
  Run-Tool ('near-assemble-'+$group.Name) (Join-Path $ToolRoot 'bin/sdas251.exe') @('-plosgff',($group.Name+'.rel'),($group.Name+'.asm'))
 }
 Run-Tool 'near-link' $compilerExe @($prior.arguments)
 $image=Get-Mx251FormatHex (Join-Path $Root 'linked.partial.hex')
 $record.bindings=@(Assert-Mx251NearImage $Root $plan $image $layout.codeBase $layout.codeBytes)
 # All replacements save one byte except the one-byte ERET -> RET pair.
 $saving=$plan.calls+$plan.jumps
 if($Receipt.codeBytes-$image.Count -ne $saving){throw 'near-whole-image-size-mismatch'}
 $record.savedBytes=$saving;$record.codeBytes=$image.Count;$record.status='APPLIED';$record.applied=$true
 [IO.File]::Copy((Join-Path $Root 'linked.partial.hex'),(Join-Path $Root 'firmware.hex'),$true)
 $last=($image.Keys|Measure-Object -Maximum).Maximum
 $binary=[byte[]]::new([int]($last-$layout.codeBase+1));[Array]::Fill[byte]($binary,255)
 foreach($address in $image.Keys){$binary[[int]($address-$layout.codeBase)]=$image[$address]}
 [IO.File]::WriteAllBytes((Join-Path $Root 'firmware.bin'),$binary)
 $Receipt.codeBytes=$image.Count;$Receipt.firmwareSha256=Hash-File (Join-Path $Root 'firmware.hex')
 $Receipt.binary.sha256=Hash-File (Join-Path $Root 'firmware.bin');$Receipt.binary.length=$binary.Length
 $prior.label='near-baseline-link';$trial=@($commands|Where-Object{$_.label -ceq 'near-link'})[-1];$trial.label='link';$trial.actualLogLabel='near-link'
 Write-Json (Join-Path $Root 'commands.json') @($commands.ToArray())
 $effects=New-Mx251CallEffects $Root $Config $Receipt
 Write-Json (Join-Path $Root 'call-effects.json') $effects
 $Receipt.callEffects.sha256=Hash-File (Join-Path $Root 'call-effects.json')
 return $record
}
