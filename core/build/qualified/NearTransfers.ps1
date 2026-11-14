# Same-region transfers are selected after all compiler/runtime/ABI trials.
# Register-only entries keep their local frame layout when the return address
# shrinks from three bytes to two. Stack-parameter and escaping entries stay far.
function Get-Mx251NearUnits([string]$Root,$Config) {
 $units=@()
 foreach($u in $Config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.c'}){
  $lines=[IO.File]::ReadAllLines((Join-Path $Root ($u.name+'.asm')))
  $owners=@{};$functions=[hashtable]::new([StringComparer]::Ordinal);$labels=[hashtable]::new([StringComparer]::Ordinal);$references=[hashtable]::new([StringComparer]::Ordinal);$owner='';$tables=@()
  for($i=0;$i -lt $lines.Count;$i++){
   $raw=$lines[$i];$code=Get-Mx251GcText $raw
   if($raw -cmatch '^; mx251-function begin schema=1 symbol=(\S+) isr=([01]) addressTaken=([01]) opaque=([01]) indirect=([01])$'){
    $owner=$Matches[1]
    $functions[$owner]=@{symbol=$owner;unit=$u.name;isr=[int]$Matches[2];addressTaken=[int]$Matches[3];opaque=[int]$Matches[4];indirect=[int]$Matches[5];begin=$i;end=$i;signature='';abi=0;bank=-1;naked=1;returns=@();calls=@();branches=@();indirectJump=$false;indirectSites=@();fixedJumps=[Collections.Generic.HashSet[int]]::new();directives=$false;directiveSites=@()}
   }
   $owners[$i]=$owner
   foreach($token in @(Get-Mx251GcTokens $code)){
    if(-not $references.ContainsKey($token)){$references[$token]=[Collections.Generic.List[object]]::new()}
    $references[$token].Add(@{line=$i;code=$code;owner=$owner})
   }
   if($code -cmatch '^([A-Za-z_.$0-9]+):{1,2}$'){$labels[$owner+':'+$Matches[1]]=@{owner=$owner;line=$i}}
   if($owner){
    $f=$functions[$owner];$f.end=$i
    if($raw -cmatch '^; mx251-(?:scalar|near)-definition schema=1 symbol=(\S+) signature=(\S+)$'){
     if($Matches[1] -ceq $owner){$f.signature=$Matches[2]}
    }
    if($raw -cmatch '^;\s*mx251-bank-contract schema=1 symbol=(\S+) abi=(\d+) bank=(\d+) naked=([01])$'){
     if($Matches[1] -ceq $owner){$f.abi=[int]$Matches[2];$f.bank=[int]$Matches[3];$f.naked=[int]$Matches[4]}
    }
    if($code -ceq 'eret'){$f.returns+=@($i)}
    if($code -cmatch '^(?:e|l|s|a)?jmp\s+@'){$f.indirectJump=$true;$f.indirectSites+=@($i)}
    if($raw -cmatch '^;\s*mx251-jump-table schema=1 owner=(\S+) label=(\S+) entries=([1-9][0-9]*) stride=4(?: dispatch=(a-dptr))?$'){
     if($Matches[1] -ceq $owner){$tables+=@{owner=$owner;line=$i;label=$Matches[2];entries=[int]$Matches[3];kind=$(if($Matches[4]){'a-dptr'}else{'dr16'})}}
    }
    if($raw -cmatch '^;\s*mx251-address-table schema=1 owner=(\S+) low=(\S+) high=(\S+) ext=(\S+) entries=([1-9][0-9]*)$'){
     if($Matches[1] -ceq $owner){$tables+=@{owner=$owner;line=$i;labels=@($Matches[2],$Matches[3],$Matches[4]);entries=[int]$Matches[5];kind='bytes'}}
    }
    if($code.StartsWith('.') -and $code -cnotmatch '^\.globl\s+'){$f.directiveSites+=@($i)}
    if($code -cmatch '(?:^|[,\s])(?:\.|\$)\s*[+-]\s*\d'){$f.directives=$true}
    if($code -cmatch '^(ejmp|ljmp|sjmp|ajmp)\s+(\S+)$'){$f.branches+=@{line=$i;op=$Matches[1];target=$Matches[2]}}
    if($raw -cmatch '^; mx251-(?:scalar|near)-call schema=1 owner=(\S+) target=(\S+) signature=(\S+)(?: |$)'){$f.calls+=@{target=$Matches[2];signature=$Matches[3]}}
   }
   if($raw -cmatch '^; mx251-function end schema=1 '){$owner=''}
  }
  # Describe both register dispatches and legacy DPX dispatches. EJMP slots
  # stay four bytes; split address bytes continue to relocate to local labels.
  $known=[Collections.Generic.HashSet[int]]::new();$knownData=[Collections.Generic.HashSet[int]]::new()
  foreach($t in $tables){
   $f=$functions[$t.owner];$before=@()
   $length=switch($t.kind){'dr16'{5};'a-dptr'{3};'bytes'{18}}
   for($i=$t.line-1;$i -ge $f.begin -and $before.Count -lt $length;$i--){$code=Get-Mx251GcText $lines[$i];if($code){$before+=@{line=$i;code=$code}}}
   if($before.Count -ne $length){continue}
   if($t.kind -ceq 'bytes'){
    $patterns=@()
    for($part=0;$part -lt 3;$part++){
     $label=[regex]::Escape($t.labels[$part])
     if($part){$patterns+=@('^mov\s+a,b$')}
     $patterns+=@(('^mov\s+dptr,#'+$label+'$'),('^mov\s+dpxl,#\('+ $label+'\s*>>\s*16\)$'),'^movc\s+a,@a\+dptr$')
     if($part -lt 2){$patterns+=@('^push\s+acc$')}
    }
    $patterns+=@('^mov\s+dpxl,a$','^pop\s+dph$','^pop\s+dpl$','^mov\s+dr28,dpx$','^ejmp\s+@dr28$')
    $valid=$true
    for($j=0;$j -lt $length;$j++){if($before[$length-1-$j].code -cnotmatch $patterns[$j]){$valid=$false;break}}
    if(-not $valid){continue}
    $i=$t.line+1;$data=@();$targets=@()
    for($part=0;$part -lt 3 -and $valid;$part++){
     while($i -le $f.end -and -not (Get-Mx251GcText $lines[$i])){$i++}
     if($i -gt $f.end -or (Get-Mx251GcText $lines[$i]) -cne ($t.labels[$part]+':')){$valid=$false;break}
     $i++;$shift=if($part){'\s*>>\s*'+(8*$part)}else{''}
     for($entry=0;$entry -lt $t.entries;$entry++){
      while($i -le $f.end -and -not (Get-Mx251GcText $lines[$i])){$i++}
      if($i -gt $f.end -or (Get-Mx251GcText $lines[$i]) -cnotmatch ('^\.db\s+([A-Za-z_.$0-9]+)'+$shift+'$')){$valid=$false;break}
      $target=$Matches[1]
      if(-not $labels.ContainsKey($t.owner+':'+$target) -or $target -ceq $t.owner -or $target -cin $t.labels){$valid=$false;break}
      if($part -eq 0){$targets+=@($target)}elseif($targets[$entry] -cne $target){$valid=$false;break}
      $data+=@($i);$i++
     }
    }
    if($valid){$null=$known.Add($before[0].line);foreach($line in $data){$null=$knownData.Add($line)}}
    continue
   }
   $label=[regex]::Escape($t.label)
   if($t.kind -ceq 'a-dptr'){
    if($before[0].code -cnotmatch '^jmp\s+@a\+dptr$' -or
       $before[1].code -cnotmatch ('^mov\s+dpxl,#\('+ $label+'\s*>>\s*16\)$') -or
       $before[2].code -cnotmatch ('^mov\s+dptr,#'+$label+'$')){continue}
   }elseif($before[0].code -cnotmatch '^ejmp\s+@dr16$' -or
      $before[1].code -cnotmatch '^add\s+dr16,dr20$' -or $before[2].code -cnotmatch ('^mov\s+wr18,#'+$label+'$') -or
      $before[3].code -cnotmatch '^movz\s+wr16,a$' -or $before[4].code -cnotmatch ('^mov\s+a,#\('+ $label+'>>16\)$')){continue}
   $i=$t.line+1;while($i -le $f.end -and -not (Get-Mx251GcText $lines[$i])){$i++}
   if($i -gt $f.end -or (Get-Mx251GcText $lines[$i]) -cne ($t.label+':')){continue}
   $slots=@();$i++
   while($i -le $f.end -and $slots.Count -lt $t.entries){
    $code=Get-Mx251GcText $lines[$i]
    if($code){if($code -cnotmatch '^ejmp\s+[A-Za-z_.$0-9]+$'){break};$slots+=@($i)}
    $i++
   }
   if($slots.Count -ne $t.entries){continue}
   $null=$known.Add($before[0].line);foreach($slot in $slots){$null=$f.fixedJumps.Add($slot)}
  }
  foreach($f in $functions.Values){
   $f.indirectJump=@($f.indirectSites|Where-Object{-not $known.Contains($_)}).Count -ne 0
   $f.directives=$f.directives -or @($f.directiveSites|Where-Object{-not $knownData.Contains($_)}).Count -ne 0
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
 $selected=@();$changes=@();$nodes=[hashtable]::new([StringComparer]::Ordinal)
 foreach($u in $Units){foreach($f in $u.functions.Values){
  # Fixed-width jump tables and inline assembly retain their original layout.
  if(-not $f.opaque -and -not $f.indirectJump -and -not $f.directives){
   foreach($b in $f.branches){if($b.op -ceq 'ejmp' -and -not $f.fixedJumps.Contains($b.line) -and $b.target -cne $f.symbol -and $u.labels.ContainsKey($f.symbol+':'+$b.target)){
    $changes+=@{unit=$u.name;line=$b.line;from='ejmp';to='ljmp';target=$b.target;owner=$f.symbol}
   }}
  }
  if($definitions[$f.symbol].Count -ne 1){continue}
  $node=@{function=$f;unit=$u;eligible=$true;calls=@();tails=@();neighbors=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)};$nodes[$f.symbol]=$node
  if($excludedSet.Contains($f.symbol) -or
     $f.isr -or $f.addressTaken -or $f.opaque -or $f.indirect -or $f.indirectJump -or $f.directives -or
     $f.abi -notin @(2,4) -or $f.bank -ne 0 -or $f.naked){$node.eligible=$false}
  $sig=[regex]::Match($f.signature,'^(?:v|[iu](?:8|16|32)|f32|p3_\d+)\((?<args>(?:(?:[iu](?:8|16|32)|f32|p3_\d+)(?:,(?:[iu](?:8|16|32)|f32|p3_\d+))*)?)\)$')
  if(-not $sig.Success){$node.eligible=$false;continue}
  $arity=if($sig.Groups['args'].Value){$sig.Groups['args'].Value.Split(',').Count}else{0}
  if($arity -gt $(if($f.abi -eq 2){1}else{2})){$node.eligible=$false}
 }}
 foreach($node in $nodes.Values){
  $f=$node.function;$u=$node.unit
  foreach($b in $f.branches){
   if($b.target.StartsWith('@')){continue}
   if($b.target -cne $f.symbol -and $u.labels.ContainsKey($f.symbol+':'+$b.target)){continue}
   if($b.op -cne 'ejmp' -or -not $nodes.ContainsKey($b.target)){$node.eligible=$false;continue}
   $edge=@{unit=$u.name;line=$b.line;from='ejmp';to='ljmp';target=$b.target;owner=$f.symbol;fixed=$f.fixedJumps.Contains($b.line)}
   $node.tails+=@($edge)
   $null=$node.neighbors.Add($b.target);$null=$nodes[$b.target].neighbors.Add($f.symbol)
  }
  if(-not $f.returns.Count -and -not $node.tails.Count){$node.eligible=$false}
  $calls=@();$bad=$false;$name=[regex]::Escape($f.symbol)
  foreach($other in $Units){
   if(-not $other.references.ContainsKey($f.symbol)){continue}
   foreach($reference in $other.references[$f.symbol]){
    $i=$reference.line;$code=$reference.code
    if($code -cmatch ('^(?:'+$name+':{1,2}|\.globl\s+'+$name+')$')){continue}
    $owner=$reference.owner
    $call=$code -cmatch ('^ecall\s+'+$name+'$');$tail=$code -cmatch ('^ejmp\s+'+$name+'$')
    if(( -not $call -and -not $tail) -or -not $owner -or $other.functions[$owner].opaque -or $other.functions[$owner].directives){
     $bad=$true
     if($owner -and $nodes.ContainsKey($owner) -and $code -cmatch '^(?:j\w+|[elsa]jmp|djnz|cjne)\b'){$nodes[$owner].eligible=$false}
     continue
    }
    $typed=@($other.functions[$owner].calls|Where-Object{$_.target -ceq $f.symbol})
    if(-not $typed.Count -or @($typed|Where-Object{$_.signature -cne $f.signature}).Count){$bad=$true;continue}
    if($call){$calls+=@{unit=$other.name;line=$i;from='ecall';to='lcall';target=$f.symbol;owner=$owner}}
    elseif(-not $nodes.ContainsKey($owner)){$bad=$true}
   }
  }
  $node.calls=$calls;if($bad){$node.eligible=$false}
 }
 # Tail edges preserve the existing return address. Their connected component
 # must use one width, including members reached only through a tail jump.
 $visited=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal);$groups=@();$tailJumps=0;$fixedTailJumps=0
 foreach($name in @($nodes.Keys|Sort-Object)){
  if(-not $visited.Add($name)){continue}
  $queue=[Collections.Generic.Queue[string]]::new();$queue.Enqueue($name);$members=@();$eligible=$true;$callCount=0
  while($queue.Count){
   $node=$nodes[$queue.Dequeue()];$members+=@($node);$eligible=$eligible -and $node.eligible;$callCount+=$node.calls.Count
   foreach($next in $node.neighbors){if($visited.Add($next)){$queue.Enqueue($next)}}
  }
  if(-not $eligible -or -not $callCount){continue}
  $groupTails=@($members|ForEach-Object{$_.tails});$groupId=$groups.Count
  $groups+=@{id=$groupId;members=@($members|ForEach-Object{$_.function.symbol}|Sort-Object);calls=$callCount;tailJumps=@($groupTails|Where-Object{-not $_.fixed}).Count;fixedTailJumps=@($groupTails|Where-Object{$_.fixed}).Count}
  foreach($node in $members){
   $f=$node.function
   $selected+=@{symbol=$f.symbol;unit=$f.unit;signature=$f.signature;abi=$f.abi;calls=$node.calls.Count;returns=$f.returns.Count;group=$groupId}
   $changes+=$node.calls
   foreach($line in $f.returns){$changes+=@{unit=$f.unit;line=$line;from='eret';to='ret';target='';owner=$f.symbol}}
   foreach($tail in $node.tails){if($tail.fixed){$fixedTailJumps++}else{$changes+=@($tail);$tailJumps++}}
  }
 }
 return @{functions=$selected;groups=$groups;tailJumps=$tailJumps;fixedTailJumps=$fixedTailJumps;changes=$changes;calls=@($changes|Where-Object{$_.from -ceq 'ecall'}).Count;jumps=@($changes|Where-Object{$_.from -ceq 'ejmp'}).Count}
}

function Get-Mx251PagePlan($Bindings) {
 # Start with all eligible transfers short, then permanently remove those
 # whose relocated source/target no longer share a page. Removing a choice
 # changes later addresses, so recompute until the remaining set is stable.
 $candidates=@($Bindings|Where-Object{$_.op -cin @('lcall','ljmp')})
 $selected=$candidates;$rounds=0
 do {
  $rounds++;$addresses=[long[]]@($selected|ForEach-Object{$_.address}|Sort-Object)
  $next=@()
  foreach($b in $selected){
   $sourceShift=[Array]::BinarySearch($addresses,[long]$b.address)
   $targetShift=[Array]::BinarySearch($addresses,[long]$b.encodedTarget)
   if($sourceShift -lt 0){$sourceShift=-bnot $sourceShift}
   if($targetShift -lt 0){$targetShift=-bnot $targetShift}
   $source=$b.address-$sourceShift;$target=$b.encodedTarget-$targetShift
   if((($source+2) -shr 11) -eq ($target -shr 11)){
    $next+=@{unit=$b.unit;line=$b.line;from=$b.op;to=$(if($b.op -ceq 'lcall'){'acall'}else{'ajmp'});address=$b.address;encodedTarget=$b.encodedTarget;finalAddress=$source;finalTarget=$target;owner=$b.owner;target=$b.target;op=$b.op}
   }
  }
  $stable=$next.Count -eq $selected.Count;$selected=$next
 }while(-not $stable)
 return @{candidates=$candidates.Count;rounds=$rounds;changes=$selected;calls=@($selected|Where-Object{$_.from -ceq 'lcall'}).Count;jumps=@($selected|Where-Object{$_.from -ceq 'ljmp'}).Count;savedBytes=$selected.Count}
}

function Assert-Mx251NearImage([string]$Root,$Plan,$Image,[long]$CodeBase,[long]$CodeBytes) {
 $region=$CodeBase -shr 16;$bindings=@();$labels=[hashtable]::new([StringComparer]::Ordinal)
 foreach($unit in @(@($Plan.changes)+@($Plan.functions)|ForEach-Object{$_.unit}|Sort-Object -Unique)){
  $lines=[IO.File]::ReadAllLines((Join-Path $Root ($unit+'.asm')))
  $owners=@{};$owner=''
  for($i=0;$i -lt $lines.Count;$i++){
   if($lines[$i] -cmatch '^; mx251-function begin schema=1 symbol=(\S+) '){$owner=$Matches[1]}
   $owners[$i+1]=$owner
   if($lines[$i] -cmatch '^; mx251-function end schema=1 '){$owner=''}
  }
  $wanted=@{};foreach($c in $Plan.changes|Where-Object{$_.unit -ceq $unit}){$wanted[$c.line+1]=$c}
  foreach($raw in [IO.File]::ReadAllLines((Join-Path $Root ($unit+'.rst')))){
   if($raw.Length -lt 35 -or $raw.Substring(6,6) -cnotmatch '^[0-9A-F]{6}$'){continue}
   $source=[regex]::Match($raw.Substring(34),'^\s*(\d+) (.*)$');if(-not $source.Success){continue}
   $number=[int]$source.Groups[1].Value;$code=Get-Mx251GcText $source.Groups[2].Value
   $address=[Convert]::ToInt64($raw.Substring(6,6),16)
   if($code -cmatch '^([A-Za-z_.$0-9]+):{1,2}$'){$labels[$unit+':'+$owners[$number]+':'+$Matches[1]]=$address}
   if(-not $wanted.ContainsKey($number)){continue}
   $c=$wanted[$number]
   if($source.Groups[2].Value.TrimEnd() -cne $lines[$number-1].TrimEnd()){throw 'near-listing-source-mismatch'}
   $bytes=[Convert]::FromHexString($raw.Substring(13,15).Replace(' ',''))
   $opcode=switch($c.to){'ljmp'{2};'lcall'{18};'ret'{34};'ajmp'{1};'acall'{17}}
   $paged=$c.to -cin @('acall','ajmp');$mask=if($paged){31}else{255}
   $length=if($c.to -ceq 'ret'){1}elseif($paged){2}else{3}
   if($bytes.Length -ne $length -or ($bytes[0] -band $mask) -ne $opcode -or (($address+$length) -shr 16) -ne $region){throw 'near-transfer-encoding-or-region'}
   for($i=0;$i -lt $bytes.Length;$i++){if($Image[$address+$i] -ne $bytes[$i]){throw 'near-listing-image-mismatch'}}
   $target=if($paged){(($address+2) -band -2048)+(([int]$bytes[0] -band 224) -shl 3)+[int]$bytes[1]}elseif($length -eq 3){($region -shl 16)+[int]$bytes[1]*256+$bytes[2]}else{$null}
   $bindings+=@{unit=$unit;line=$number;op=$c.to;owner=$c.owner;target=$c.target;address=$address;encodedTarget=$target}
   $wanted.Remove($number)
  }
  if($wanted.Count){throw 'near-transfer-missing-listing'}
 }
 foreach($b in $bindings|Where-Object{$_.op -cne 'ret'}){
  $unit=$b.unit;$owner=$b.owner
  $entry=@($Plan.functions|Where-Object{$_.symbol -ceq $b.target});if($entry.Count){$unit=$entry[0].unit;$owner=$b.target}
  $key=$unit+':'+$owner+':'+$b.target
  if(-not $labels.ContainsKey($key) -or $b.encodedTarget -ne $labels[$key]){throw 'near-transfer-target-mismatch'}
 }
 foreach($address in $Image.Keys){if($address -lt $CodeBase -or $address -ge $CodeBase+$CodeBytes){throw 'near-image-outside-code-region'}}
 return $bindings
}

function Invoke-Mx251NearTransfers([string]$Root,$Config,$Receipt,[string]$ToolRoot,[string]$ProfileRoot,[string]$Mode) {
 $record=@{schema=3;status='OFF';applied=$false;savedBytes=0;functions=@();groups=@();tailJumps=0;fixedTailJumps=0;calls=0;jumps=0;returnAddressBytes=2;baselineCodeBytes=$Receipt.codeBytes;pageTransfers=@{status='NOT_RUN';calls=0;jumps=0;savedBytes=0}}
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
 $record.functions=$plan.functions;$record.calls=$plan.calls;$record.jumps=$plan.jumps;$record.groups=$plan.groups;$record.tailJumps=$plan.tailJumps;$record.fixedTailJumps=$plan.fixedTailJumps
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
 $saving=$plan.calls+$plan.jumps
 if($Receipt.codeBytes-$image.Count -ne $saving){throw 'near-whole-image-size-mismatch'}
 # All selected instructions live in the concatenated CSEG. Its only layout
 # change is deleting one byte at each selected site; linker relocation keeps
 # references from other sections and unmodified units current.
 $map=[IO.File]::ReadAllText((Join-Path $Root 'linked.partial.map'))
 $segment=[regex]::Match($map,'(?m)^CSEG\s+([0-9A-F]+)\s+([0-9A-F]+)\s+=.*\(REL,CON,CODE\)')
 $pages=Get-Mx251PagePlan $record.bindings
 $pages.status='NO_CANDIDATE';$pages.baselineCodeBytes=$image.Count
 if($pages.savedBytes){
  if(-not $segment.Success){throw 'page-transfer-CSEG-layout'}
  $start=[Convert]::ToInt64($segment.Groups[1].Value,16);$end=$start+[Convert]::ToInt64($segment.Groups[2].Value,16)
  foreach($b in $pages.changes){if($b.address -lt $start -or $b.address+3 -gt $end -or $b.encodedTarget -lt $start -or $b.encodedTarget -ge $end){throw 'page-transfer-outside-CSEG'}}
  $pageBackup=Join-Path $Root 'page-baseline';$null=[IO.Directory]::CreateDirectory($pageBackup)
  foreach($p in Get-ChildItem -LiteralPath $Root -File|Where-Object{$_.Name -like 'linked.partial.*' -or $_.Extension -cin @('.asm','.rel','.rst','.lst','.sym')}){[IO.File]::Copy($p.FullName,(Join-Path $pageBackup $p.Name),$false)}
  foreach($group in $pages.changes|Group-Object unit){
   $unit=@($units|Where-Object{$_.name -ceq $group.Name})[0]
   foreach($b in $group.Group){
    $line=$b.line-1;$unit.lines[$line]=[regex]::Replace($unit.lines[$line],('^(\s*)'+$b.from+'\b'),('${1}'+$b.to))
    $change=@($plan.changes|Where-Object{$_.unit -ceq $b.unit -and $_.line -eq $line})[0];$change.to=$b.to
   }
   [IO.File]::WriteAllLines((Join-Path $Root ($group.Name+'.asm')),$unit.lines,[Text.UTF8Encoding]::new($false))
   Run-Tool ('page-assemble-'+$group.Name) (Join-Path $ToolRoot 'bin/sdas251.exe') @('-plosgff',($group.Name+'.rel'),($group.Name+'.asm'))
  }
  Run-Tool 'page-link' $compilerExe @($prior.arguments)
  $image=Get-Mx251FormatHex (Join-Path $Root 'linked.partial.hex')
  $record.bindings=@(Assert-Mx251NearImage $Root $plan $image $layout.codeBase $layout.codeBytes)
  $bound=@{};foreach($b in $record.bindings){$bound[$b.unit+':'+$b.line]=$b}
  foreach($b in $pages.changes){$actual=$bound[$b.unit+':'+$b.line];if($actual.address -ne $b.finalAddress -or $actual.encodedTarget -ne $b.finalTarget){throw 'page-transfer-layout-mismatch'}}
  $saving+=$pages.savedBytes
  if($Receipt.codeBytes-$image.Count -ne $saving){throw 'page-whole-image-size-mismatch'}
  $pages.status='APPLIED'
 }
 $record.pageTransfers=$pages
 $record.savedBytes=$saving;$record.codeBytes=$image.Count;$record.status='APPLIED';$record.applied=$true
 [IO.File]::Copy((Join-Path $Root 'linked.partial.hex'),(Join-Path $Root 'firmware.hex'),$true)
 $last=($image.Keys|Measure-Object -Maximum).Maximum
 $binary=[byte[]]::new([int]($last-$layout.codeBase+1));[Array]::Fill[byte]($binary,255)
 foreach($address in $image.Keys){$binary[[int]($address-$layout.codeBase)]=$image[$address]}
 [IO.File]::WriteAllBytes((Join-Path $Root 'firmware.bin'),$binary)
 $Receipt.codeBytes=$image.Count;$Receipt.firmwareSha256=Hash-File (Join-Path $Root 'firmware.hex')
 $Receipt.binary.sha256=Hash-File (Join-Path $Root 'firmware.bin');$Receipt.binary.length=$binary.Length
 $finalLabel=if($pages.savedBytes){'page-link'}else{'near-link'}
 $prior.label='near-baseline-link';$trial=@($commands|Where-Object{$_.label -ceq $finalLabel})[-1];$trial.label='link';$trial.actualLogLabel=$finalLabel
 Write-Json (Join-Path $Root 'commands.json') @($commands.ToArray())
 $effects=New-Mx251CallEffects $Root $Config $Receipt
 Write-Json (Join-Path $Root 'call-effects.json') $effects
 $Receipt.callEffects.sha256=Hash-File (Join-Path $Root 'call-effects.json')
 return $record
}
