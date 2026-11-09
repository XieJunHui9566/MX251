# Verify the compiler's saved groups against the emitted control flow.
# The ordinary write analysis remains independent; only a proven entry/exit
# stack discipline removes bytes from the transitive call write set.
function Get-Mx251CalleePreservation([string]$Body,[string]$Symbol) {
 $claims=[regex]::Matches($Body,'(?m)^; mx251-callee-preserved schema=1 saved=([0-9a-f]{8})\s*$')
 if($claims.Count -ne 1){return 0L}
 [long]$saved=[Convert]::ToInt64($claims[0].Groups[1].Value,16)
 $groups=@(@{name='wr8';mask=768L;width=2},@{name='dr12';mask=61440L;width=4},
           @{name='dr24';mask=251658240L;width=4},@{name='dr28';mask=4026531840L;width=4})
 if(-not $saved -or ($saved -band (4294967295L -bxor 4278252288L))){return 0L}
 foreach($g in $groups){if(($saved -band $g.mask) -and ($saved -band $g.mask) -ne $g.mask){return 0L}}
 $code=[Collections.Generic.List[object]]::new();$labels=New-Mx251OrdinalMap
 foreach($raw in $Body.Split("`n")) {
  $line=($raw -split ';',2)[0].Trim();if(-not $line){continue}
  if($line -cmatch '^([A-Za-z_.$0-9]+):{1,2}$'){$labels[$Matches[1]]=$code.Count;continue}
  if($line -cmatch '^[A-Za-z_.$][A-Za-z_.$0-9]*\s*='){continue}
  if($line -cnotmatch '^(\w+)\s*(.*?)$'){return 0L}
  $code.Add(@{op=$Matches[1].ToLowerInvariant();arg=$Matches[2].Trim().ToLowerInvariant();raw=$Matches[2].Trim()})
 }
 if(-not $labels.ContainsKey($Symbol) -or $labels[$Symbol] -ne 0){return 0L}
 $prefix=0;$bytes=0;$slots=@{}
 foreach($g in $groups) {
  if(-not ($saved -band $g.mask)){continue}
  if($prefix -ge $code.Count -or $code[$prefix].op -cne 'push' -or $code[$prefix].arg -cne $g.name){return 0L}
  $prefix++;$bytes+=$g.width;$slots[$bytes]=$g.name
 }
 # Each instruction has one incoming stack depth and restoration state.
 # This also checks joins and loop back edges without unrolling loops.
 $states=@{};$pending=[Collections.Generic.Queue[int]]::new()
 $states[$prefix]=@($bytes,0L);$pending.Enqueue($prefix);$returns=0
 while($pending.Count) {
  $i=$pending.Dequeue();if($i -ge $code.Count){return 0L}
  $n=$code[$i];[int]$depth=$states[$i][0];[long]$restored=$states[$i][1]
  $args=@($n.arg.Split(',')|ForEach-Object{$_.Trim()});$next=@($i+1)
  if($n.op -eq 'eret') {
   if($n.arg -or $depth -ne 0 -or $restored -ne $saved){return 0L}
   $returns++;continue
  }
  if($n.op -in @('ret','reti')){return 0L}
  if($depth -lt $bytes -and $n.op -notin @('pop','sjmp','ljmp','ejmp')){return 0L}
  if($n.op -in @('sjmp','ljmp','ejmp','jz','jnz','jc','jnc','je','jne','jslt','jsle','jsgt','jsge','jsl','jsg','jg','jle','jb','jnb','jbc','cjne','djnz')) {
   $target=$n.raw.Split(',')[-1].Trim()
   if(-not $labels.ContainsKey($target)){return 0L}
   $next=@($labels[$target]);if($n.op -notin @('sjmp','ljmp','ejmp')){$next+=($i+1)}
  } elseif($n.op -in @('ecall','lcall','acall')) {
   if($depth -lt $bytes -or $n.raw -cnotmatch '^[A-Za-z_.$][A-Za-z_.$0-9]*$'){return 0L}
  } elseif($n.op -in @('push','pop','pushw')) {
   if($args.Count -ne 1){return 0L}
   $r=Get-Mx251BankRegister $n.arg
   $width=if($n.op -eq 'pushw'){2}elseif($r.known){$r.width}else{1}
   if($n.op -eq 'pop') {
    if($n.arg -in @('sp','spx','dr60','wr60','wr62','0x81','129')){return 0L}
    if($depth -le $bytes) {
     if(-not $slots.ContainsKey($depth) -or $slots[$depth] -cne $n.arg){return 0L}
     $restored=$restored -bor $r.mask
    } elseif($depth-$width -lt $bytes){return 0L}
    $depth-=$width
   } else {$depth+=$width}
  } elseif($args[0] -in @('spx','dr60')) {
   if($n.op -notin @('add','sub','inc','dec') -or $args.Count -ne 2 -or $args[1] -cnotmatch '^#(0x[0-9a-f]+|[0-9]+)$'){return 0L}
   $amount=Get-Mx251Numeric $args[1].Substring(1)
   if($n.op -in @('sub','dec')){$depth-=$amount}else{$depth+=$amount}
   if($depth -lt $bytes){return 0L}
  } elseif($args[0] -in @('sp','0x81','129','wr60','wr62')){return 0L}
  elseif($args[0] -cmatch '^@spx(?:([+-])(0x[0-9a-f]+|[0-9]+))?$') {
   $offset=0;if($Matches[2]){$offset=Get-Mx251Numeric $Matches[2];if($Matches[1] -eq '-'){$offset=-$offset}}
   if($n.op -ne 'mov' -or $args.Count -ne 2){return 0L}
   $r=Get-Mx251BankRegister $args[1];$width=if($r.known){$r.width}else{1}
   $first=$depth+$offset
   if($first -le $bytes -and $first+$width -gt 1){return 0L}
  }
  foreach($j in $next) {
   if($j -lt $prefix -or $j -ge $code.Count){return 0L}
   if($states.ContainsKey($j)) {
    if($states[$j][0] -ne $depth -or $states[$j][1] -ne $restored){return 0L}
   } else {$states[$j]=@($depth,$restored);$pending.Enqueue($j)}
  }
 }
 if(-not $returns){return 0L}
 return $saved
}

# Select groups from actual caller saves, after subtracting savings already
# available through ordinary call-clobber feedback. The cost is static code
# bytes, including every current return and the loss of a tail transfer.
function Select-Mx251CalleePreservation([string]$Root,$Scan,$Owners,$Config) {
 $targets=New-Mx251OrdinalMap
 foreach($f in $Scan.functions) {
  if($f.abi -notin @(2,4) -or $f.unit.StartsWith('library:') -or
     $f.body -notmatch '; mcs251 wide-mir accepted:' -or $f.body -match '; wide ordinary ABI2 passthrough:' -or
     $f.body -match '(?m)^\s*ejmp\s+@'){continue}
  $defs=@($Scan.symbols[$f.symbol]|Where-Object{if($f.exported){$_.exported}else{$_.unit -ceq $f.unit}})
  if($defs.Count -ne 1 -or ($f.exported -and (-not $Owners.ContainsKey($f.symbol) -or
     $Owners[$f.symbol].Count -ne 1 -or $Owners[$f.symbol][0] -cne $f.unit))){continue}
  $key=$f.unit+':'+$f.symbol
  $exits=[regex]::Matches($f.body,'(?m)^\s*eret\s*$').Count
  $tails=[regex]::Matches($f.body,'(?m)^\s*ejmp\s+(?!__wm_)([_A-Za-z.$][_A-Za-z.$0-9]*)\s*$').Count
  $targets[$key]=@{function=$f;groups=@{};exits=$exits+$tails;tails=$tails;mask=0L}
 }
 $groupMasks=@(768L,61440L,251658240L,4026531840L)
 foreach($caller in $Scan.functions) {
  foreach($m in [regex]::Matches($caller.body,'(?m)^; mx251-call-effects schema=1 caller=\S+ site=\d+ callee=(\S+) abi=[24] live=[0-9a-f]{8} setup=([0-9a-f]{8}) finish=([0-9a-f]{8}) complete=1 saved=([0-9a-f,]+)\s*$')) {
   $name=$m.Groups[1].Value
   if(-not $Scan.symbols.ContainsKey($name)){continue}
   $defs=@($Scan.symbols[$name]);$local=@($defs|Where-Object{-not $_.exported -and $_.unit -ceq $caller.unit})
   $defs=@(if($local.Count){$local}else{$defs|Where-Object{$_.exported}})
   if($defs.Count -ne 1){continue}
   $key=$defs[0].unit+':'+$name;if(-not $targets.ContainsKey($key)){continue}
   $target=$targets[$key];[long]$boundary=[Convert]::ToInt64($m.Groups[2].Value,16) -bor [Convert]::ToInt64($m.Groups[3].Value,16)
   foreach($part in $m.Groups[4].Value.Split(',')) {
    [long]$mask=[Convert]::ToInt64($part,16)
    if(($mask -band $boundary) -or -not ($mask -band $defs[0].writes)){continue}
    foreach($group in $groupMasks) {
     if(($mask -band $group) -ne $mask){continue}
     if(-not $target.groups.ContainsKey($group)){$target.groups[$group]=0}
     $target.groups[$group]+=4
    }
   }
  }
 }
 $selected=@();$files=New-Mx251OrdinalMap
 foreach($key in ($targets.Keys|Sort-Object -CaseSensitive)) {
  $t=$targets[$key];$cost=2*(1+$t.exits)+$t.tails
  foreach($group in $groupMasks) {
   if($t.groups.ContainsKey($group) -and $t.groups[$group] -gt $cost){
    $t.mask=$t.mask -bor $group
    $selected+=@{unit=$t.function.unit;symbol=$t.function.symbol;group=('{0:x8}' -f $group);savedBytes=$t.groups[$group];entryExitBytes=$cost}
   }
  }
  if($t.mask){
   $f=$t.function
   if(-not $files.ContainsKey($f.unit)){$files[$f.unit]=@('MX251-CALLEE-PRESERVE-1')}
   $files[$f.unit]+=($(if($f.exported){'global'}else{'local'})+' '+$f.symbol+' '+$f.abi+' '+('{0:x8}' -f $t.mask))
  }
 }
 $paths=New-Mx251OrdinalMap
 foreach($unit in $files.Keys) {
  $path=Join-Path $Root ('callee-preserve-'+$unit+'.txt')
  [IO.File]::WriteAllText($path,($files[$unit] -join "`n")+"`n",[Text.UTF8Encoding]::new($false));$paths[$unit]=$path
 }
 return @{selected=$selected;files=$paths}
}
