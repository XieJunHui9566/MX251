# 推导普通 bank-0 R0..R31 的 may-write 上界，不推导净效果或省略内存访问。
# 未知目标、别名、指令和间接调用逐函数退回全破坏。
function New-Mx251OrdinalMap { return ,([hashtable]::new([StringComparer]::Ordinal)) }
function Get-Mx251Numeric([string]$Text) {
 $t=$Text.Trim()
 while($t.StartsWith('(') -and $t.EndsWith(')')){$t=$t.Substring(1,$t.Length-2).Trim()}
 if($t -cmatch '^0[xX][0-9a-fA-F]{1,8}$'){return [Convert]::ToInt64($t.Substring(2),16)}
 if($t -cmatch '^[0-9]{1,10}$'){return [long]::Parse($t,[Globalization.CultureInfo]::InvariantCulture)}
 return $null
}
function Get-Mx251DirectAddress([string]$Text,$Aliases,[int]$Depth=0) {
 if($Depth -gt 8){return $null}
 $t=$Text.Trim()
 while($t.StartsWith('(') -and $t.EndsWith(')')){$t=$t.Substring(1,$t.Length-2).Trim()}
 $number=Get-Mx251Numeric $t;if($null -ne $number){return $number}
 if($Aliases.ContainsKey($t)){return $Aliases[$t]}
 $m=[regex]::Match($t,'^(.+?)\s*([+-])\s*(0x[0-9a-fA-F]+|[0-9]+)$')
 if($m.Success){
  $left=Get-Mx251DirectAddress $m.Groups[1].Value $Aliases ($Depth+1)
  $right=Get-Mx251Numeric $m.Groups[3].Value
  if($null -ne $left -and $null -ne $right){if($m.Groups[2].Value -eq '+'){return ($left+$right)}else{return ($left-$right)}}
 }
 return $null
}
# GPR 按物理字节建模；经典 DATA 的 0..7 与 bank-0 R0..R7 对应。
# DATA 8..31 的别名不猜测，保守污染全部高组；A/B 的 SFR 与位别名
# 必须计入 R11/R10，不能继续沿用只分析低组时的零掩码。
function Get-Mx251BankRegister([string]$Text) {
 $t=$Text.ToLowerInvariant();$m=[regex]::Match($t,'^(ar|r|wr|dr)([0-9]{1,2})$')
 if($m.Success){
  $kind=$m.Groups[1].Value;$first=[int]$m.Groups[2].Value;$width=switch($kind){'wr'{2};'dr'{4};default{1}}
  if(($kind -eq 'ar' -and $first -lt 8) -or ($kind -eq 'r' -and $first -lt 16) -or ($width -eq 2 -and $first -lt 32 -and $first%2 -eq 0) -or ($width -eq 4 -and $first -lt 32 -and $first%4 -eq 0)) {
   [long]$mask=0;for($i=$first;$i -lt $first+$width;$i++){$mask=$mask -bor ([long]1 -shl $i)}
   return @{known=$true;mask=$mask;first=$first;width=$width}
  }
 }
 if($t -in @('a','acc')){return @{known=$true;mask=2048;first=11;width=1}}
 if($t -eq 'b'){return @{known=$true;mask=1024;first=10;width=1}}
 if($t -in @('c','dpl','dph','dpxl')){return @{known=$true;mask=0;first=32;width=1}}
 if($t -in @('dptr','wr56','wr58','wr60','wr62')){return @{known=$true;mask=0;first=32;width=2}}
 if($t -in @('dpx','spx','dr56','dr60')){return @{known=$true;mask=0;first=32;width=4}}
 return @{known=$false;mask=4294967295L;first=-1;width=0}
}
function Get-Mx251DirectGprMask([long]$Address,[int]$Width,$State) {
 if($Address -lt 0 -or $Address+$Width -gt 256 -or $Width -notin @(1,2,4)){throw 'unresolved-direct-width'}
 [long]$mask=0
 for($i=$Address;$i -lt $Address+$Width;$i++){
  if($i -in @(129,208,209)){throw 'stack-or-register-bank-write'}
  if($i -in @(130,131,132,133)){$State.low=$null;$State.high=$null}
  if($i -lt 8){$mask=$mask -bor ([long]1 -shl [int]$i)}
  elseif($i -lt 32){$mask=$mask -bor 4294967040L}
  elseif($i -eq 224){$mask=$mask -bor 2048}
  elseif($i -eq 240){$mask=$mask -bor 1024}
 }
 return $mask
}
# 仅在当前直线基本块内传播实际 GPR 字节常量；入口、标签、分支和调用
# 均清空事实。按物理别名失效，不把指针变量或设备名称当作地址证明。
function Clear-Mx251BankConstants($State,[long]$Mask) {
 if(-not $State.ContainsKey('constants')){$State.constants=@{}}
 for($i=0;$i -lt 32;$i++){if($Mask -band ([long]1 -shl $i)){$State.constants.Remove($i)}}
}
function Get-Mx251BankConstant([string]$Text,$State) {
 $r=Get-Mx251BankRegister $Text
 if(-not $r.known -or -not $r.mask -or -not $State.ContainsKey('constants')){return $null}
 [long]$v=0
 for($i=$r.first;$i -lt $r.first+$r.width;$i++){
  if(-not $State.constants.ContainsKey($i)){return $null}
  $v=($v -shl 8) -bor [long]$State.constants[$i]
 }
 return $v
}
function Set-Mx251BankConstant([string]$Text,[long]$Value,$State) {
 $r=Get-Mx251BankRegister $Text
 if(-not $r.known -or -not $r.mask -or $Value -lt 0 -or $Value -ge ([long]1 -shl (8*$r.width))){return}
 for($j=0;$j -lt $r.width;$j++){$State.constants[$r.first+$j]=($Value -shr (8*($r.width-1-$j))) -band 255}
}
function Get-Mx251FlatStoreMask([long]$Address,[int]$Width,$State) {
 # 拒绝越过 24 位边界；不得把绕回寄存器/栈控制区的写判为普通内存。
 if($Address -lt 0 -or $Width -notin @(1,2,4) -or $Address+$Width -gt 16777216){throw 'unproven-flat-address-span'}
 if($Address -lt 256){return (Get-Mx251DirectGprMask $Address $Width $State)}
 return 0
}
function Get-Mx251BankDestination([string]$Text,$Aliases,$State,[switch]$Bit,[int]$Width=1) {
 $mask=Get-Mx251BankDestinationCore $Text $Aliases $State -Bit:$Bit -Width $Width
 Clear-Mx251BankConstants $State $mask
 return $mask
}
function Get-Mx251BankDestinationCore([string]$Text,$Aliases,$State,[switch]$Bit,[int]$Width=1) {
 $t=$Text.Trim();$lower=$t.ToLowerInvariant()
 if($Bit) {
  if($lower -in @('c','cy','ac','ov','p','f0','f1')){return 0}
  $m=[regex]::Match($t,'^(.+)\.([0-7])$')
  if($m.Success){
   $base=Get-Mx251DirectAddress $m.Groups[1].Value $Aliases
   if($null -eq $base -or $base -lt 128 -or $base -gt 255 -or $base -in @(208,209)){throw 'unresolved-or-bank-changing-bit-write'}
   return (Get-Mx251DirectGprMask $base 1 $State)
  }
  $address=Get-Mx251Numeric $t
  if($null -eq $address -and $Aliases.ContainsKey($t)){$address=$Aliases[$t]}
  if($null -eq $address -or $address -lt 0 -or $address -gt 255 -or $address -in @(211,212)){throw 'unresolved-or-bank-changing-bit-write'}
  if($address -ge 128){return (Get-Mx251DirectGprMask ($address -band 248) 1 $State)}
  return 0
 }
 $reg=Get-Mx251BankRegister $t
 if($reg.known){
  if($lower -in @('dptr','dpl','dph','wr58')){$State.low=$null}
  if($lower -in @('dpxl','wr56')){$State.high=$null}
  if($lower -in @('dpx','dr56')){$State.low=$null;$State.high=$null}
  return $reg.mask
 }
 if($lower -cmatch '^@spx(?:[+-](?:0x[0-9a-f]+|[0-9]+))?$'){return 0}
 if($lower -eq '@dpx'){
  if($null -eq $State.low -or $null -eq $State.high){throw 'unknown-indirect-write'}
  $address=([long]$State.high -shl 16) -bor [long]$State.low
  return (Get-Mx251FlatStoreMask $address $Width $State)
 }
 if($lower -cmatch '^@(dr(?:[0-9]{1,2}))$'){
  $address=Get-Mx251BankConstant $Matches[1] $State
  if($null -eq $address){throw 'unknown-indirect-write'}
  return (Get-Mx251FlatStoreMask $address $Width $State)
 }
 if($t.StartsWith('@')){throw 'unknown-indirect-write'}
 $address=Get-Mx251DirectAddress $t $Aliases
 if($null -eq $address){throw 'unresolved-direct-write'}
 return (Get-Mx251DirectGprMask $address $Width $State)
}
function Get-Mx251BankBody([string]$Body,$Aliases) {
 $labels=New-Mx251OrdinalMap;$calls=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 foreach($m in [regex]::Matches($Body,'(?m)^\s*([A-Za-z_.$0-9]+):{1,2}\s*$')){$labels[$m.Groups[1].Value]=$true}
 $mask=0;$count=0;$state=@{low=$null;high=$null;constants=@{}};$reason=$null
 try {
  foreach($raw in $Body.Split("`n")) {
   $line=($raw -split ';',2)[0].Trim();if(-not $line){continue}
   if($line -cmatch '^[A-Za-z_.$0-9]+:{1,2}$'){$state.low=$null;$state.high=$null;$state.constants.Clear();continue}
   if($line -cmatch '^[A-Za-z_.$][A-Za-z_.$0-9]*\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)$'){continue}
   $m=[regex]::Match($line,'^(\w+)\s*(.*?)$');if(-not $m.Success){throw 'unknown-syntax'}
   $op=$m.Groups[1].Value.ToLowerInvariant();$operand=$m.Groups[2].Value.Trim();$args=@($operand.Split(',')|ForEach-Object{$_.Trim()});$count++
   if($op -in @('ecall','lcall','acall')) {
    if($args.Count -ne 1 -or $operand -cnotmatch '^[A-Za-z_.$][A-Za-z_.$0-9]*$'){throw 'indirect-or-unknown-call'}
    $null=$calls.Add($operand);$state.low=$null;$state.high=$null;$state.constants.Clear();continue
   }
   if($op -in @('eret','ret','nop')){if($operand){throw 'unexpected-operands'};continue}
   if($op -in @('sjmp','ljmp','ejmp','jz','jnz','jc','jnc','je','jne','jslt','jsle','jsgt','jsge','jsl','jsg','jg','jle','jb','jnb','jbc','cjne','djnz')) {
    $expected=switch($op){'cjne'{3};'djnz'{2};'jb'{2};'jnb'{2};'jbc'{2};default{1}}
    if($args.Count -ne $expected){throw 'malformed-control-flow'}
    if(-not $labels.ContainsKey($args[-1])){
     if($op -in @('ljmp','ejmp') -and $operand -cmatch '^[A-Za-z_.$][A-Za-z_.$0-9]*$'){
      $null=$calls.Add($operand);$state.low=$null;$state.high=$null;$state.constants.Clear();continue
     }
     throw 'nonlocal-control-flow'
    }
    if($op -eq 'djnz'){$mask=$mask -bor (Get-Mx251BankDestination $args[0] $Aliases $state)}
    if($op -eq 'jbc'){$mask=$mask -bor (Get-Mx251BankDestination $args[0] $Aliases $state -Bit)}
    $state.low=$null;$state.high=$null;$state.constants.Clear();continue
   }
   if($op -eq 'pushw') {if($args.Count -ne 1 -or -not $operand.StartsWith('#')){throw 'invalid-word-push'};continue}
   if($op -eq 'push') {if($args.Count -ne 1){throw 'invalid-push'};continue}
   if($op -eq 'pop') {if($args.Count -ne 1){throw 'invalid-pop'};$mask=$mask -bor (Get-Mx251BankDestination $operand $Aliases $state);continue}
   if($op -in @('mov','movx','movc','movz','movs','anl','orl','xrl','add','addc','sub','subb')) {
    if($args.Count -ne 2){throw 'invalid-binary-operation'}
    $isBit=($op -eq 'mov' -and ($args[0].ToLowerInvariant() -eq 'c' -or $args[1].ToLowerInvariant() -eq 'c'))
    $source=Get-Mx251BankRegister $args[1]
    $destination=Get-Mx251BankRegister $args[0]
    $constant=$null
    if($op -eq 'mov' -and $destination.mask -and $destination.known){
     if($args[1].StartsWith('#')){$constant=Get-Mx251Numeric $args[1].Substring(1)}
     elseif($source.known -and $source.width -eq $destination.width){$constant=Get-Mx251BankConstant $args[1] $state}
    }
    $width=if($source.known){$source.width}else{1}
    $mask=$mask -bor (Get-Mx251BankDestination $args[0] $Aliases $state -Bit:$isBit -Width $width)
    if($null -ne $constant){Set-Mx251BankConstant $args[0] $constant $state}
    $dest=$args[0].ToLowerInvariant()
    if($dest -in @('dptr','dpl','dph','dpx','dpxl','wr56','wr58','dr56')) {
     $v=if($args[1].StartsWith('#')){Get-Mx251Numeric $args[1].Substring(1)}else{$null}
     if($op -eq 'mov' -and $dest -eq 'dptr' -and $null -ne $v -and $v -le 65535){$state.low=$v}
     elseif($op -eq 'mov' -and $dest -eq 'dpxl' -and $null -ne $v -and $v -le 255){$state.high=$v}
     else{$state.low=$null;$state.high=$null}
    }
    continue
   }
   if($op -eq 'cmp'){if($args.Count -ne 2){throw 'invalid-compare'};continue}
   if($op -in @('inc','dec','cpl','clr','setb','sll','srl','sra','rl','rr','rlc','rrc','swap')) {
    if($args.Count -gt 2 -or ($args.Count -eq 2 -and ($op -notin @('inc','dec') -or -not $args[1].StartsWith('#')))){throw 'invalid-unary-operation'}
    $bit=($op -eq 'setb' -or ($op -in @('clr','cpl') -and $args[0].ToLowerInvariant() -notin @('a','acc')))
    $mask=$mask -bor (Get-Mx251BankDestination $args[0] $Aliases $state -Bit:$bit)
    if($args[0].ToLowerInvariant() -in @('dptr','dpl','dph','dpx','dpxl','wr56','wr58','dr56')){$state.low=$null;$state.high=$null}
    continue
   }
   if($op -eq 'xch') {
    if($args.Count -ne 2){throw 'invalid-exchange'}
    foreach($arg in $args){$mask=$mask -bor (Get-Mx251BankDestination $arg $Aliases $state)}
    continue
   }
   if($op -in @('mul','div') -and $operand.ToLowerInvariant() -eq 'ab'){$mask=$mask -bor 3072;Clear-Mx251BankConstants $state 3072;continue}
   throw ('unknown-instruction:'+ $op)
  }
 } catch {$reason=$_.Exception.Message;$mask=4294967295L}
 return @{writes=$mask;calls=@($calls);reason=$reason;instructions=$count;failureLine=$(if($reason){$line}else{$null})}
}
function Get-Mx251BankFunctions([string]$Root,$Config) {
 $symbols=New-Mx251OrdinalMap;$all=@()
 foreach($unit in $Config.units) {
  if([IO.Path]::GetExtension($unit.file) -cne '.c'){continue}
  $path=Join-Path $Root ($unit.name+'.asm');$text=[IO.File]::ReadAllText($path);$aliases=New-Mx251OrdinalMap
  foreach($m in [regex]::Matches($text,'(?m)^\s*([A-Za-z_.$][A-Za-z_.$0-9]*)\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)\s*$')){
   $key=$m.Groups[1].Value;$value=Get-Mx251Numeric $m.Groups[2].Value
   if($aliases.ContainsKey($key) -and $aliases[$key] -ne $value){$aliases[$key]=$null}else{$aliases[$key]=$value}
  }
  foreach($m in [regex]::Matches($text,'(?ms)^; mx251-function begin schema=1 symbol=([^\s]+) isr=([01]) addressTaken=([01]) opaque=([01]) indirect=([01])\s*\r?\n(.*?)^; mx251-function end schema=1 symbol=\1\s*\r?$')) {
   $symbol=$m.Groups[1].Value;$body=$m.Groups[6].Value
   $contract=[regex]::Matches($body,'(?m)^;\s*mx251-bank-contract schema=1 symbol='+[regex]::Escape($symbol)+' abi=([24]) bank=([0-9]+) naked=([01])\s*$')
   $exported=[regex]::IsMatch($text,'(?m)^\s*\.globl\s+'+[regex]::Escape($symbol)+'\s*$')
   $reason=$null;$abi=0
   if($m.Groups[2].Value -ne '0' -or $m.Groups[4].Value -ne '0' -or $m.Groups[5].Value -ne '0'){$reason='special-or-opaque-function'}
   elseif($contract.Count -ne 1){$reason='missing-function-contract'}
   elseif($contract[0].Groups[2].Value -ne '0' -or $contract[0].Groups[3].Value -ne '0'){$reason='special-function-abi'}
   else{$abi=[int]$contract[0].Groups[1].Value}
   $analysis=if($reason){@{writes=4294967295L;calls=@();reason=$reason;instructions=0;failureLine=$null}}else{Get-Mx251BankBody $body $aliases}
   $row=@{symbol=$symbol;unit=$unit.name;abi=$abi;exported=$exported;localWrites=$analysis.writes;writes=$analysis.writes;calls=@($analysis.calls);reason=$analysis.reason;failureLine=$analysis.failureLine;instructions=$analysis.instructions;body=$body;aliases=$aliases}
   if(-not $symbols.ContainsKey($symbol)){$symbols[$symbol]=@()};$symbols[$symbol]+=@($row);$all+=@($row)
  }
 }
 # 单调并集覆盖直接调用闭包；未知定义、局部重名或特殊 ABI 都贡献 top。
 do {
  $changed=$false
  foreach($row in $all) {
   $mask=$row.localWrites
   foreach($callee in $row.calls) {
    $definitions=@(if($symbols.ContainsKey($callee)){$symbols[$callee]})
    $local=@($definitions|Where-Object{$_.unit -ceq $row.unit -and -not $_.exported})
    $targets=@(if($local.Count){$local}else{$definitions|Where-Object{$_.exported}})
    if($targets.Count -ne 1 -or $targets[0].abi -ne 2){$mask=4294967295L;break}
    $mask=$mask -bor $targets[0].writes
   }
   if($mask -ne $row.writes){$row.writes=$mask;$changed=$true}
  }
 } while($changed)
 return @{symbols=$symbols;functions=$all}
}

function Invoke-Mx251CallClobberFeedback([string]$Root,$Config,$Inputs,[string]$CompilerSha,[string]$ProfileSha) {
 $record=[ordered]@{schema=1;status='NO_ELIGIBLE_CLOSURE';compilerSha256=$CompilerSha;profileLockSha256=$ProfileSha;inputs=$Inputs;bindings=@();recompiledUnits=@();finalValidated=$false;fallback=$false}
 if(@($Config.archives).Count){$record.status='FALLBACK_PROJECT_ARCHIVE_BINDING_UNPROVEN';return $record}
 $scan=Get-Mx251BankFunctions $Root $Config
 # 绑定实际对象定义，而非仅凭源函数名；同名或未定义目标不进入反馈。
 $owners=New-Mx251OrdinalMap
 foreach($u in $Config.units){
  $text=[IO.File]::ReadAllText((Join-Path $Root ($u.name+'.rel')))
  foreach($m in [regex]::Matches($text,'(?m)^S ([^\s]+) Def[0-9A-Fa-f]+\s*$')){
   $name=$m.Groups[1].Value;if(-not $owners.ContainsKey($name)){$owners[$name]=@()};$owners[$name]+=@($u.name)
  }
 }
 $bindings=@()
 foreach($key in ($scan.symbols.Keys|Sort-Object -CaseSensitive)) {
  $defs=@($scan.symbols[$key]);$defs=@($defs|Where-Object{$_.exported})
  if($defs.Count -eq 1 -and $defs[0].abi -eq 2 -and $defs[0].writes -ne 4294967295L -and
     $owners.ContainsKey($key) -and $owners[$key].Count -eq 1 -and $owners[$key][0] -ceq $defs[0].unit){
   $bindings+=@{symbol=$key;unit=$defs[0].unit;abi=2;writes=$defs[0].writes}
  }
 }
 $record.bindings=$bindings
 $record.analysis=@($scan.functions|ForEach-Object{@{symbol=$_.symbol;unit=$_.unit;abi=$_.abi;localWrites=$_.localWrites;writes=$_.writes;calls=$_.calls;reason=$_.reason;instructions=$_.instructions}})
 if(-not $bindings.Count){return $record}
 $byName=New-Mx251OrdinalMap;foreach($b in $bindings){$byName[$b.symbol]=$b}
 $units=@()
 foreach($u in $Config.units){
  if([IO.Path]::GetExtension($u.file) -cne '.c' -or $u.backend -ceq 'legacy'){continue}
  $needed=$false
  foreach($f in @($scan.functions|Where-Object{$_.unit -ceq $u.name})) {
   foreach($callee in $f.calls){if($byName.ContainsKey($callee)){$needed=$true;break}}
   if($needed){break}
  }
  if($needed){$units+=@($u)}
 }
 if(-not $units.Count){$record.status='NO_REACHABLE_FEEDBACK_CALLS';return $record}
 $pass=Join-Path $Root 'call-clobber-pass1';[IO.Directory]::CreateDirectory($pass)|Out-Null
 $saved=@()
 foreach($u in $Config.units){foreach($ext in @('.asm','.rel','.lst','.sym','.stdout.log','.stderr.log')){
  $name=$u.name+$ext;$path=Join-Path $Root $name
  if(Test-Path -LiteralPath $path -PathType Leaf){[IO.File]::Copy($path,(Join-Path $pass $name),$false);$saved+=@{path=$name;sha256=(Hash-File $path)}}
 }}
 $record.pass1=$saved
 $path=Join-Path $Root 'call-clobbers.txt'
 $disabled=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 $record.attempts=@();$record.repairHistory=@();$record.maxAttempts=4;$nextUnits=$units
 try {
  # 有界、单调的事实修复：每轮必须新增破坏位或撤销一个反馈目标。
  # 最多四次编译，只重编引用变化事实的单元；最终仍逐项核对真实省略位。
  # 不收敛、无新增事实或任何身份/边界失败都恢复原对象，不放宽安全判断。
  for($attempt=0;$attempt -lt $record.maxAttempts;$attempt++) {
  $lines=@('MX251-CALL-CLOBBERS-1')+@($bindings|Where-Object{-not $disabled.Contains($_.symbol)}|ForEach-Object{($_.symbol+' 2 {0:x8}' -f [long]$_.writes)})
  [IO.File]::WriteAllText($path,($lines -join "`n")+"`n",[Text.UTF8Encoding]::new($false));$record.feedbackSha256=Hash-File $path
  foreach($u in $nextUnits){
   $args=@(Flags $u.backend $u.stackAuto $u.defines)+@('--mcs251-leaf-effects','call-clobbers.txt','-c',$u.file,'-o',($u.name+'.rel'))
   Run-Tool ('clobber-'+$u.name) $compilerExe $args;Check-Assembly (Join-Path $Root ($u.name+'.asm'))
   $record.recompiledUnits+=@($u.name)
  }
  if((Hash-File $compilerExe) -cne $CompilerSha -or (Hash-File $lockPath) -cne $ProfileSha -or (Hash-File $path) -cne $record.feedbackSha256){throw 'clobber-feedback-identity-changed'}
  foreach($entry in $Inputs.GetEnumerator()){if((Hash-File (Join-Path $Root $entry.Key)) -cne $entry.Value.sha256){throw 'clobber-feedback-source-changed'}}
  $final=Get-Mx251BankFunctions $Root $Config
  $expanded=@()
  foreach($b in $bindings){
   if(-not $final.symbols.ContainsKey($b.symbol)){throw 'clobber-final-definition-missing'}
   $defs=@($final.symbols[$b.symbol]|Where-Object{$_.exported})
   if($defs.Count -ne 1 -or $defs[0].unit -cne $b.unit -or $defs[0].abi -ne $b.abi){throw ('clobber-final-definition-changed:'+ $b.symbol)}
   if($defs[0].writes -band (4294967295L -bxor $b.writes)){$expanded+=@{symbol=$b.symbol;before=$b.writes;after=$defs[0].writes}}
  }
  $record.expandedBounds=$expanded
  # 分配器可改变未被省略的寄存器；正确性条件是每一个实际省略位都与
  # 最终真实调用闭包的破坏集合不相交，不能只检查第一遍推测掩码。
  $uses=@()
  foreach($f in $final.functions){
   $pending=$null
   foreach($m in [regex]::Matches($f.body,'(?m)^;\s*mx251 call-clobber (save|restore) callee=([^\s]+) removed=([0-9a-f]{2}) writes=([0-9a-f]{8})\s*$')){
    if($m.Groups[1].Value -ceq 'save'){
     if($null -ne $pending){throw 'clobber-overlapping-ordinary-save'}
     $pending=$m;continue
    }
    if($null -eq $pending -or $pending.Groups[2].Value -cne $m.Groups[2].Value -or $pending.Groups[3].Value -cne $m.Groups[3].Value){throw 'clobber-unpaired-ordinary-save'}
    $start=$pending.Index+$pending.Length
    $boundary=Get-Mx251BankBody $f.body.Substring($start,$m.Index-$start) $f.aliases
    if($boundary.calls.Count -ne 1 -or $boundary.calls[0] -cne $m.Groups[2].Value){$boundary.writes=4294967295L;$boundary.reason='unexpected-call-boundary'}
    $uses+=@{caller=$f.symbol;unit=$f.unit;callee=$m.Groups[2].Value;removed=[Convert]::ToInt32($m.Groups[3].Value,16);kind='ordinary';boundaryWrites=$boundary.writes;boundaryReason=$boundary.reason}
    $pending=$null
   }
   if($null -ne $pending){throw 'clobber-unclosed-ordinary-save'}
   foreach($m in [regex]::Matches($f.body,'(?m)^; mx251-call-save caller=([^\s]+) site=([0-9]+) callee=([^\s]+) omitted=([0-9a-f]{8}) groups=([0-9]+) bytes=([0-9]+) verified=([01])\s*$')){
    if($m.Groups[1].Value -cne $f.symbol -or $m.Groups[7].Value -cne '1'){throw 'clobber-call-setup-proof-missing'}
    $mask=[Convert]::ToUInt32($m.Groups[4].Value,16)
    $uses+=@{caller=$f.symbol;unit=$f.unit;callee=$m.Groups[3].Value;removed=[long]$mask;kind='wide';site=[int]$m.Groups[2].Value}
   }
  }
  $record.actualUses=$uses
  $violations=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  foreach($use in $uses){
   if(-not $byName.ContainsKey($use.callee) -or -not $final.symbols.ContainsKey($use.callee)){throw 'clobber-unbound-actual-call'}
   $defs=@($final.symbols[$use.callee]|Where-Object{$_.exported})
   if($defs.Count -ne 1 -or $defs[0].abi -ne 2){throw ('clobber-actual-binding-violated:'+ $use.callee)}
   if(($defs[0].writes -band $use.removed) -or ($use.kind -ceq 'ordinary' -and ($use.boundaryWrites -band $use.removed))){$null=$violations.Add($use.callee)}
   $use.finalCalleeWrites=$defs[0].writes
  }
  $record.attempts+=@{attempt=$attempt;units=@($nextUnits|ForEach-Object{$_.name});feedbackSha256=$record.feedbackSha256;disabled=@($disabled);violations=@($violations);uses=$uses}
  if($violations.Count) {
   if($attempt+1 -ge $record.maxAttempts){throw ('clobber-actual-preservation-violated:'+(@($violations) -join ','))}
   # 每次修复合并所有已观察到的写集合扩张。只删除本轮已触发
   # 冲突的事实，会让重新分配使用另一个已经变宽的旧事实并再次退回。
   # 合并仅增加破坏位，最终仍逐个核对实际省略位；不是采信中间估计。
   $repairTargets=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
   $record.repairBounds=@();$repairProgress=$false;$newDisabled=@()
   foreach($growth in $expanded){
    if($disabled.Contains($growth.symbol)){continue}
    $binding=$byName[$growth.symbol];$before=$binding.writes
    $binding.writes=$binding.writes -bor $growth.after
    if($binding.writes -ne $before){$repairProgress=$true;$null=$repairTargets.Add($growth.symbol)}
    $record.repairBounds+=@{symbol=$growth.symbol;before=$before;after=$binding.writes}
   }
   foreach($name in $violations){
    # 普通调用参数准备/结果搬运的破坏并不属于 callee。不能靠扩大 callee
    # 写集合伪造其来源；此类冲突和未解释冲突继续撤销该目标的反馈。
    $boundaryConflict=@($uses|Where-Object{$_.callee -ceq $name -and $_.kind -ceq 'ordinary' -and ($_.boundaryWrites -band $_.removed)}).Count
    if($boundaryConflict -or -not $repairTargets.Contains($name)){if($disabled.Add($name)){$repairProgress=$true;$newDisabled+=@($name)}}
    $null=$repairTargets.Add($name)
   }
   $record.repairHistory+=@{attempt=$attempt;expanded=$record.repairBounds;disabledAdded=$newDisabled;progress=$repairProgress}
   if(-not $repairProgress){throw 'clobber-repair-no-progress'}
   # 只重新编译引用变化事实的单元；最终仍核对整个调用闭包。
   $nextUnits=@($units|Where-Object{
    $name=$_.name;$needed=$false
    foreach($f in @($scan.functions|Where-Object{$_.unit -ceq $name})){
     foreach($callee in $f.calls){if($repairTargets.Contains($callee)){$needed=$true;break}}
    }
    $needed
   })
   if(-not $nextUnits.Count){throw 'clobber-repair-callers-missing'}
   continue
  }
  $record.status='FINAL_ACTUAL_CALL_PRESERVATION_VERIFIED';$record.finalValidated=$true
  $record.finalAnalysis=@($final.functions|ForEach-Object{@{symbol=$_.symbol;unit=$_.unit;abi=$_.abi;writes=$_.writes;reason=$_.reason}})
  break
  }
 } catch {
  if($_.Exception.Message -match 'PAUSE_REQUESTED'){throw}
  $record.status='FALLBACK_ORIGINAL_OBJECTS';$record.fallback=$true;$record.reason=$_.Exception.Message
  $rejected=Join-Path $Root 'call-clobber-rejected';[IO.Directory]::CreateDirectory($rejected)|Out-Null
  foreach($f in $saved){$p=Join-Path $Root $f.path;if(Test-Path -LiteralPath $p -PathType Leaf){[IO.File]::Copy($p,(Join-Path $rejected $f.path),$false)}}
  foreach($f in $saved){
   $source=Join-Path $pass $f.path;if((Hash-File $source) -cne $f.sha256){throw 'clobber-original-evidence-changed'}
   [IO.File]::Copy($source,(Join-Path $Root $f.path),$true)
  }
 }
 return $record
}
