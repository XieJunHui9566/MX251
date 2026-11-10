# Byte constants on the instruction CFG. A join keeps only bytes that agree
# on every incoming path; unknown bytes form an inclusive address interval.
function Get-Mx251FactRegister([string]$Text,$Aliases) {
 $r=Get-Mx251BankRegister $Text
 if($r.known -and $r.mask){return @{first=$r.first;width=$r.width}}
 $t=$Text.ToLowerInvariant()
 switch($t) {
  {$_ -in @('dpx','dr56')} {return @{first=32;width=4}}
  'wr56' {return @{first=32;width=2}}
  {$_ -in @('dptr','wr58')} {return @{first=34;width=2}}
  'dpxl' {return @{first=33;width=1}}
  'dph' {return @{first=34;width=1}}
  'dpl' {return @{first=35;width=1}}
 }
 $a=Get-Mx251DirectAddress $Text $Aliases
 if($null -ne $a) {
  if($a -ge 0 -and $a -lt 8){return @{first=[int]$a;width=1}}
  if($a -eq 224){return @{first=11;width=1}}
  if($a -eq 240){return @{first=10;width=1}}
  if($a -in @(130,131,132)){return @{first=$(switch($a){130{35};131{34};132{33}});width=1}}
 }
 return $null
}
function Get-Mx251FactBytes([string]$Text,[int]$Width,[int[]]$Facts,$Aliases) {
 [int[]]$bytes=@(-1)*$Width
 if($Text.StartsWith('#')) {
  $v=Get-Mx251Numeric $Text.Substring(1)
  if($null -ne $v -and $v -lt ([long]1 -shl (8*$Width))) {
   for($i=0;$i -lt $Width;$i++){$bytes[$i]=($v -shr (8*($Width-1-$i))) -band 255}
  }
 } else {
  $r=Get-Mx251FactRegister $Text $Aliases
  if($null -ne $r -and $r.width -eq $Width){for($i=0;$i -lt $Width;$i++){$bytes[$i]=$Facts[$r.first+$i]}}
 }
 return ,$bytes
}
function Test-Mx251FactStore([string]$Text,[int]$Width,[int[]]$Facts) {
 if($null -eq $Facts -or $Width -notin @(1,2,4)){return $false}
 $t=$Text.ToLowerInvariant();$offset=0L
 if($t -eq '@dpx'){$first=33;$count=3}
 elseif($t -cmatch '^@(dr(?:[0-9]{1,2}))(?:\+(0x[0-9a-f]+|[0-9]+))?$') {
  $r=Get-Mx251BankRegister $Matches[1]
  if(-not $r.known -or -not $r.mask){return $false}
  $first=$r.first;$count=4
  if($Matches[2]){$offset=Get-Mx251Numeric $Matches[2]}
 } else {return $false}
 [long]$low=0;[long]$high=0
 for($i=$first;$i -lt $first+$count;$i++) {
  $low=$low -shl 8;$high=$high -shl 8
  if($Facts[$i] -ge 0){$low=$low -bor $Facts[$i];$high=$high -bor $Facts[$i]}else{$high=$high -bor 255}
 }
 return $low+$offset -ge 256 -and $high+$offset+$Width -le 16777216
}
function Invoke-Mx251FactInstruction($Node,[int[]]$Before,$Aliases,$Objects) {
 [int[]]$after=$Before.Clone();$op=$Node.op;$args=$Node.args
 if(-not $op -or $op -in @('nop','ret','eret','push','pushw','cmp','sjmp','ljmp','ejmp','jz','jnz','jc','jnc','je','jne','jslt','jsle','jsgt','jsge','jsl','jsg','jg','jle','jb','jnb','cjne')){return ,$after}
 if($op -in @('ecall','lcall','acall')){return ,([int[]](@(-1)*36))}
 if($op -in @('mul','div')) {
  if($Node.operand.ToLowerInvariant() -eq 'ab'){$after[10]=$after[11]=-1;return ,$after}
  $r=Get-Mx251BankRegister $args[0]
  if(-not $r.known -or -not $r.mask -or $r.width -gt 2){return ,([int[]](@(-1)*36))}
  $width=2*$r.width;$first=$r.first-($r.first%$width)
  for($i=$first;$i -lt $first+$width;$i++){$after[$i]=-1};return ,$after
 }
 if($op -notin @('mov','movx','movc','movz','movs','anl','orl','xrl','add','addc','sub','subb','pop','inc','dec','djnz','jbc','cpl','clr','setb','sll','srl','sra','rl','rr','rlc','rrc','swap','xch')){return ,([int[]](@(-1)*36))}
 $state=@{low=$null;high=$null;constants=@{};objects=$Objects;addressFacts=$Before}
 for($i=0;$i -lt 32;$i++){if($Before[$i] -ge 0){$state.constants[$i]=$Before[$i]}}
 if($Before[34] -ge 0 -and $Before[35] -ge 0){$state.low=($Before[34] -shl 8) -bor $Before[35]}
 if($Before[33] -ge 0){$state.high=$Before[33]}
 $bit=$op -in @('setb','jbc') -or ($op -in @('clr','cpl') -and $args[0].ToLowerInvariant() -notin @('a','acc')) -or ($op -eq 'mov' -and ($args[0].ToLowerInvariant() -eq 'c' -or $args[1].ToLowerInvariant() -eq 'c'))
 $width=1
 if($args.Count -gt 1){$source=Get-Mx251BankRegister $args[1];if($source.known){$width=$source.width}}
 try {
  $mask=Get-Mx251BankDestinationCore $args[0] $Aliases $state -Bit:$bit -Width $width
  if($op -eq 'xch'){$mask=$mask -bor (Get-Mx251BankDestinationCore $args[1] $Aliases $state)}
 } catch {return ,([int[]](@(-1)*36))}
 for($i=0;$i -lt 32;$i++){if($mask -band ([long]1 -shl $i)){$after[$i]=-1}}
 $direct=Get-Mx251DirectAddress $args[0] $Aliases
 if($null -ne $direct -and $direct -le 133 -and $direct+$width -gt 130){for($i=32;$i -lt 36;$i++){$after[$i]=-1}}
 $dest=Get-Mx251FactRegister $args[0] $Aliases
 if($bit){for($i=32;$i -lt 36;$i++){$after[$i]=-1};return ,$after}
 if($null -eq $dest){return ,$after}
 $first=$dest.first;$width=$dest.width
 [int[]]$value=@(-1)*$width
 if($op -eq 'mov'){$value=Get-Mx251FactBytes $args[1] $width $Before $Aliases}
 elseif($op -in @('movz','movs')) {
  $src=Get-Mx251FactRegister $args[1] $Aliases
  if($null -ne $src -and $src.width -lt $width) {
   $extension=if($op -eq 'movz'){0}elseif($Before[$src.first] -ge 0){if($Before[$src.first] -band 128){255}else{0}}else{-1}
   for($i=0;$i -lt $width-$src.width;$i++){$value[$i]=$extension}
   for($i=0;$i -lt $src.width;$i++){$value[$width-$src.width+$i]=$Before[$src.first+$i]}
  }
 } elseif($op -eq 'clr' -or ($op -in @('sub','xrl') -and $args[0] -ceq $args[1])){$value=[int[]](@(0)*$width)}
 elseif($op -in @('anl','orl','xrl')) {
  $rhs=Get-Mx251FactBytes $args[1] $width $Before $Aliases
  for($i=0;$i -lt $width;$i++) {
   $x=$Before[$first+$i];$y=$rhs[$i]
   if($op -eq 'anl' -and ($x -eq 0 -or $y -eq 0)){$value[$i]=0}
   elseif($op -eq 'orl' -and ($x -eq 255 -or $y -eq 255)){$value[$i]=255}
   elseif($x -ge 0 -and $y -ge 0){$value[$i]=switch($op){'anl'{$x -band $y};'orl'{$x -bor $y};'xrl'{$x -bxor $y}}}
  }
 }
 for($i=0;$i -lt $width;$i++){$after[$first+$i]=$value[$i]}
 if($op -eq 'xch') {
  $src=Get-Mx251FactRegister $args[1] $Aliases
  if($null -ne $src){for($i=0;$i -lt $src.width;$i++){$after[$src.first+$i]=-1}}
 }
 return ,$after
}
function Get-Mx251CfgAddressFacts([string]$Body,$Aliases,$Objects) {
 $lines=$Body.Split("`n");$count=$lines.Count
 if($count -gt 16384){return @{}}
 $labels=New-Mx251OrdinalMap;$nodes=[object[]]::new($count)
 for($i=0;$i -lt $count;$i++) {
  $line=($lines[$i] -split ';',2)[0].Trim();$op='';$operand='';$args=@()
  if($line -cmatch '^([A-Za-z_.$0-9]+):{1,2}$') {
   if($labels.ContainsKey($Matches[1])){return @{}};$labels[$Matches[1]]=$i
  } elseif($line -and $line -cnotmatch '^[A-Za-z_.$][A-Za-z_.$0-9]*\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)$') {
   if($line -notmatch '^(\w+)\s*(.*?)$'){return @{}}
   $op=$Matches[1].ToLowerInvariant();$operand=$Matches[2];$args=@($operand.Split(',')|ForEach-Object{$_.Trim()})
  }
  $nodes[$i]=@{op=$op;operand=$operand;args=$args;successors=@()}
 }
 $jumps=@('sjmp','ljmp','ejmp','jz','jnz','jc','jnc','je','jne','jslt','jsle','jsgt','jsge','jsl','jsg','jg','jle','jb','jnb','jbc','cjne','djnz')
 for($i=0;$i -lt $count;$i++) {
  $n=$nodes[$i];$succ=@()
  if($n.op -eq 'jmp'){return @{}}
  if($n.op -in $jumps) {
   if($labels.ContainsKey($n.args[-1])){$succ+=@($labels[$n.args[-1]])}
   elseif($n.op -notin @('ljmp','ejmp')){return @{}}
  }
  if($n.op -notin @('ret','eret','sjmp','ljmp','ejmp') -and $i+1 -lt $count){$succ+=@($i+1)}
  $n.successors=$succ
 }
 $incoming=[object[]]::new($count);$incoming[0]=[int[]](@(-1)*36)
 $queued=[bool[]]::new($count);$queue=[Collections.Generic.Queue[int]]::new();$queue.Enqueue(0);$queued[0]=$true
 $steps=0
 while($queue.Count) {
  if(++$steps -gt $count*40){return @{}}
  $i=$queue.Dequeue();$queued[$i]=$false
  $after=Invoke-Mx251FactInstruction $nodes[$i] $incoming[$i] $Aliases $Objects
  foreach($to in $nodes[$i].successors) {
   $changed=$false
   if($null -eq $incoming[$to]){$incoming[$to]=$after.Clone();$changed=$true}
   else {
    for($b=0;$b -lt 36;$b++){if($incoming[$to][$b] -ge 0 -and $incoming[$to][$b] -ne $after[$b]){$incoming[$to][$b]=-1;$changed=$true}}
   }
   if($changed -and -not $queued[$to]){$queue.Enqueue($to);$queued[$to]=$true}
  }
 }
 $facts=@{}
 for($i=0;$i -lt $count;$i++){if($null -ne $incoming[$i]){$facts[$i]=$incoming[$i]}}
 return $facts
}
