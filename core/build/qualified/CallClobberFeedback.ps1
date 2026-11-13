# 推导普通 bank-0 R0..R31、DPTR/DPXL 的 may-write 上界。
# 未知目标、别名、指令和间接调用逐函数退回全破坏。
. (Join-Path $PSScriptRoot 'CalleePreservation.ps1')
. (Join-Path $PSScriptRoot 'CallAddressFacts.ps1')
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
# DATA 8..31 是 bank 1..3 的低寄存器，不是 CPU 中的 R8..R31。
# A/B 的 SFR 与位别名对应 R11/R10。
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
  if($i -in @(129,208,209) -or $i -eq $State.sph){throw 'stack-or-register-bank-write'}
  if($i -in @(130,131,132,133)){$State.low=$null;$State.high=$null}
  if($i -in @(130,131)){$State.dpxWrites=1 -bor [long]$State['dpxWrites']}
  if($i -in @(132,133)){$State.dpxWrites=2 -bor [long]$State['dpxWrites']}
  if($i -lt 8){$mask=$mask -bor ([long]1 -shl [int]$i)}
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
 $object=Get-Mx251ObjectReference $t $State.objects
 if($null -ne $object){
  if($Bit -and $object.space -ceq 'bit' -and $object.offset -eq 0){return 0}
  if(-not $Bit -and $object.space -ceq 'data' -and $object.offset -ge 0 -and $object.offset+$Width -le $object.size){return 0}
 }
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
  if($lower -in @('dptr','dpl','dph','wr58')){$State.low=$null;$State.dpxWrites=1 -bor [long]$State['dpxWrites']}
  if($lower -in @('dpxl','wr56')){$State.high=$null;$State.dpxWrites=2 -bor [long]$State['dpxWrites']}
  if($lower -in @('dpx','dr56')){$State.low=$null;$State.high=$null;$State.dpxWrites=3}
  return $reg.mask
 }
 if($lower -cmatch '^@spx(?:[+-](?:0x[0-9a-f]+|[0-9]+))?$'){return 0}
 if(Test-Mx251FactStore $lower $Width $State.addressFacts){return 0}
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
function Get-Mx251BankBody([string]$Body,$Aliases,$Objects=$null,[switch]$WithAddressFacts,[ValidateSet('legacy-v1','sph85-v1')][string]$SfrLayout='legacy-v1') {
 $labels=New-Mx251OrdinalMap;$calls=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 foreach($m in [regex]::Matches($Body,'(?m)^\s*([A-Za-z_.$0-9]+):{1,2}\s*$')){$labels[$m.Groups[1].Value]=$true}
 # Call discovery is independent of the caller's own write summary. An
 # indirect store or jump table can make that summary unknown without
 # preventing the caller from using a callee's known preservation facts.
 foreach($raw in $Body.Split("`n")) {
  $line=($raw -split ';',2)[0].Trim()
  $m=[regex]::Match($line,'^(ecall|lcall|acall|ljmp|ejmp)\s+([A-Za-z_.$][A-Za-z_.$0-9]*)$')
  if($m.Success -and ($m.Groups[1].Value -in @('ecall','lcall','acall') -or -not $labels.ContainsKey($m.Groups[2].Value))){$null=$calls.Add($m.Groups[2].Value)}
 }
 $facts=if($WithAddressFacts){Get-Mx251CfgAddressFacts $Body $Aliases $Objects $(if($SfrLayout -ceq 'sph85-v1'){133}else{190})}else{@{}}
 $lineIndex=-1
 $mask=0;$count=0;$state=@{low=$null;high=$null;dpxWrites=0;sph=$(if($SfrLayout -ceq 'sph85-v1'){133}else{190});constants=@{};objects=$Objects};$reason=$null
 try {
  foreach($raw in $Body.Split("`n")) {
   ++$lineIndex;$state.addressFacts=$facts[$lineIndex]
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
   if($op -in @('mul','div') -and $args.Count -eq 2) {
    $dest=Get-Mx251BankRegister $args[0];$src=Get-Mx251BankRegister $args[1]
    if(-not $dest.known -or -not $src.known -or -not $dest.mask -or -not $src.mask -or $dest.width -ne $src.width -or $dest.width -gt 2){throw 'invalid-register-multiply-divide'}
    $width=2*$dest.width;$first=$dest.first-($dest.first%$width);[long]$written=0
    for($i=$first;$i -lt $first+$width;$i++){$written=$written -bor ([long]1 -shl $i)}
    $mask=$mask -bor $written;Clear-Mx251BankConstants $state $written;continue
   }
   throw ('unknown-instruction:'+ $op)
  }
 } catch {$reason=$_.Exception.Message;$mask=4294967295L;$state.dpxWrites=3}
 if($reason -eq 'unknown-indirect-write' -and -not $WithAddressFacts -and $env:MX251_CFG_ADDRESS_EFFECTS -ne '0'){return (Get-Mx251BankBody $Body $Aliases $Objects -WithAddressFacts -SfrLayout $SfrLayout)}
 return @{writes=$mask;dpxWrites=$state.dpxWrites;calls=@($calls);reason=$reason;instructions=$count;failureLine=$(if($reason){$line}else{$null})}
}
# Object references retain an extent instead of receiving a made-up numeric
# address. This also distinguishes a bit object from an SFR bit alias.
function Get-Mx251ObjectReference([string]$Text,$Objects,[int]$Depth=0) {
 if($null -eq $Objects -or $Depth -gt 8){return $null}
 $t=$Text.Trim()
 while($t.StartsWith('(') -and $t.EndsWith(')')){
  $nest=0;$outer=$true
  for($i=0;$i -lt $t.Length-1;$i++){
   if($t[$i] -eq '('){$nest++}elseif($t[$i] -eq ')'){$nest--}
   if($nest -eq 0){$outer=$false;break}
  }
  if(-not $outer){break};$t=$t.Substring(1,$t.Length-2).Trim()
 }
 if($Objects.ContainsKey($t)){
  $o=$Objects[$t].Clone();$o.offset=0;return $o
 }
 $m=[regex]::Match($t,'^(.+?)\s*([+-])\s*(0x[0-9a-fA-F]+|[0-9]+)$')
 if(-not $m.Success){return $null}
 $o=Get-Mx251ObjectReference $m.Groups[1].Value $Objects ($Depth+1)
 if($null -eq $o){return $null}
 $delta=Get-Mx251Numeric $m.Groups[3].Value
 if($m.Groups[2].Value -ceq '-'){$delta=-$delta}
 $o.offset+=$delta;return $o
}
function Get-Mx251BankObjects([string]$Root,$Config,$MemoryLayout=$null) {
 $units=New-Mx251OrdinalMap;$owners=New-Mx251OrdinalMap;$exports=New-Mx251OrdinalMap;$bank0=$false
 foreach($u in $Config.units){
  $rel=[IO.File]::ReadAllText((Join-Path $Root ($u.name+'.rel')))
  foreach($m in [regex]::Matches($rel,'(?m)^S ([^\s]+) Def[0-9A-Fa-f]+\s*$')){
   $s=$m.Groups[1].Value;if(-not $owners.ContainsKey($s)){$owners[$s]=@()};$owners[$s]+=@($u.name)
  }
  if([regex]::IsMatch($rel,'(?m)^A REG_BANK_0 size 8 flags 4 addr 0\s*$')){$bank0=$true}
  $units[$u.name]=New-Mx251OrdinalMap
  if([IO.Path]::GetExtension($u.file) -cne '.c'){continue}
  $asm=[IO.File]::ReadAllText((Join-Path $Root ($u.name+'.asm')))
  # The metadata is emitted beside the actual allocation, after dead locals
  # and extern declarations have been removed. Check the allocation and area
  # together; do not promote a declaration, __at alias or hand-written label.
  $area=''
  foreach($line in $asm.Split("`n")){
   if($line -cmatch '^\s*\.area\s+([^\s]+)'){$area=$Matches[1]}
   if($line -cnotmatch '^; mx251-object schema=1 symbol=([^\s]+) space=(data|bit|xdata) size=([0-9]+)\s*$'){continue}
   $s=$Matches[1];$space=$Matches[2];$size=[int]$Matches[3]
   if(($space -ceq 'data' -and $area -cne 'DSEG') -or ($space -ceq 'bit' -and $area -cne 'BSEG') -or ($space -ceq 'xdata' -and $area -cne 'XSEG')){continue}
   if(-not [regex]::IsMatch($asm,'(?m)^'+[regex]::Escape($s)+':{1,2}\s*\r?\n\s*\.ds\s+'+$size+'\s*\r?\n; mx251-object schema=1 symbol='+[regex]::Escape($s)+' ')){continue}
   $exported=[regex]::IsMatch($asm,'(?m)^\s*\.globl\s+'+[regex]::Escape($s)+'\s*$')
   $o=@{space=$space;size=$size;unit=$u.name;exported=$exported}
   if($space -ceq 'xdata'){
    if($null -eq $MemoryLayout -or $size -gt $MemoryLayout.xramBytes -or $MemoryLayout.xramBase -lt 256 -or $MemoryLayout.xramBase+$MemoryLayout.xramBytes -gt 16777216){continue}
    $o.addressLow=[long]$MemoryLayout.xramBase;$o.addressHigh=[long]$MemoryLayout.xramBase+$MemoryLayout.xramBytes-$size
   }
   $units[$u.name][$s]=$o
   if($exported){if(-not $exports.ContainsKey($s)){$exports[$s]=@()};$exports[$s]+=@($o)}
  }
 }
 foreach($u in $Config.units){
  $objects=$units[$u.name]
  foreach($s in $exports.Keys){
   if($exports[$s].Count -ne 1 -or -not $owners.ContainsKey($s) -or $owners[$s].Count -ne 1 -or $owners[$s][0] -cne $exports[$s][0].unit){
    if($objects.ContainsKey($s) -and $objects[$s].exported){$objects.Remove($s)};continue
   }
   if(-not $objects.ContainsKey($s)){$objects[$s]=$exports[$s][0]}
  }
  # The linker allocates DSEG around REG_BANK_0 and below 0x80. Without its
  # reservation a DATA object could overlap R0..R7, so use numeric effects.
  if(-not $bank0){foreach($s in @($objects.Keys)){if($objects[$s].space -ceq 'data'){$objects.Remove($s)}}}
 }
 return ,$units
}
function Get-Mx251BankFunctions([string]$Root,$Config,$LibraryFunctions=@(),$MemoryLayout=$null) {
 $unitObjects=Get-Mx251BankObjects $Root $Config $MemoryLayout
 $symbols=New-Mx251OrdinalMap;$all=@()
 foreach($unit in $Config.units) {
  if([IO.Path]::GetExtension($unit.file) -cne '.c'){continue}
  $path=Join-Path $Root ($unit.name+'.asm');$text=[IO.File]::ReadAllText($path);$aliases=New-Mx251OrdinalMap
  $layoutMatches=[regex]::Matches($text,'(?m)^; mx251-sfr-layout schema=1 contract=([^\s]+)\s*$')
  $sfrLayout='legacy-v1'
  if($layoutMatches.Count -gt 1){throw 'Multiple SFR layout records'}
  if($layoutMatches.Count -eq 1){$sfrLayout=$layoutMatches[0].Groups[1].Value}
  if($sfrLayout -cnotin @('legacy-v1','sph85-v1')){throw 'Unknown emitted SFR layout'}
  foreach($m in [regex]::Matches($text,'(?m)^\s*([A-Za-z_.$][A-Za-z_.$0-9]*)\s*=\s*(0x[0-9a-fA-F]+|[0-9]+)\s*$')){
   $key=$m.Groups[1].Value;$value=Get-Mx251Numeric $m.Groups[2].Value
   if($aliases.ContainsKey($key) -and $aliases[$key] -ne $value){$aliases[$key]=$null}else{$aliases[$key]=$value}
  }
  foreach($key in $aliases.Keys){$unitObjects[$unit.name].Remove($key)}
  foreach($m in [regex]::Matches($text,'(?ms)^; mx251-function begin schema=1 symbol=([^\s]+) isr=([01]) addressTaken=([01]) opaque=([01]) indirect=([01])\s*\r?\n(.*?)^; mx251-function end schema=1 symbol=\1\s*\r?$')) {
   $symbol=$m.Groups[1].Value;$body=$m.Groups[6].Value
   $contract=[regex]::Matches($body,'(?m)^;\s*mx251-bank-contract schema=1 symbol='+[regex]::Escape($symbol)+' abi=([24]) bank=([0-9]+) naked=([01])\s*$')
   $exported=[regex]::IsMatch($text,'(?m)^\s*\.globl\s+'+[regex]::Escape($symbol)+'\s*$')
   $reason=$null;$abi=0
   if($m.Groups[2].Value -ne '0' -or $m.Groups[4].Value -ne '0' -or $m.Groups[5].Value -ne '0'){$reason='special-or-opaque-function'}
   elseif($contract.Count -ne 1){$reason='missing-function-contract'}
   elseif($contract[0].Groups[2].Value -ne '0' -or $contract[0].Groups[3].Value -ne '0'){$reason='special-function-abi'}
   else{$abi=[int]$contract[0].Groups[1].Value}
   $objects=$unitObjects[$unit.name]
   $analysis=if($reason){@{writes=4294967295L;dpxWrites=3;calls=@();reason=$reason;instructions=0;failureLine=$null}}else{Get-Mx251BankBody $body $aliases $objects -SfrLayout $sfrLayout}
   # An ISR is never a normal callable definition. Its direct C calls can
   # still consume callee summaries, including when the unit only has ISRs.
   # Inline assembly does not hide direct calls. The compiler decides whether
   # that ISR body can use the facts; its callable ABI remains unknown here.
   if($m.Groups[2].Value -eq '1' -and $m.Groups[5].Value -eq '0' -and
      $contract.Count -eq 1 -and $contract[0].Groups[1].Value -eq '2' -and
      $contract[0].Groups[2].Value -eq '0' -and $contract[0].Groups[3].Value -eq '0') {
    $isrCalls=Get-Mx251BankBody $body $aliases $objects -SfrLayout $sfrLayout
    $analysis.calls=@($isrCalls.calls)
   }
   $row=@{symbol=$symbol;unit=$unit.name;abi=$abi;exported=$exported;localWrites=$analysis.writes;writes=$analysis.writes;calls=@($analysis.calls);reason=$analysis.reason;failureLine=$analysis.failureLine;instructions=$analysis.instructions;body=$body;aliases=$aliases;objects=$objects;sfrLayout=$sfrLayout}
   $row.localDpxWrites=$analysis.dpxWrites;$row.dpxWrites=$analysis.dpxWrites
   $row.preserved=if($abi -in @(2,4)){Get-Mx251CalleePreservation $body $symbol}else{0L}
   $row.writes=$row.localWrites -band (4294967295L -bxor $row.preserved)
   if(-not $symbols.ContainsKey($symbol)){$symbols[$symbol]=@()};$symbols[$symbol]+=@($row);$all+=@($row)
  }
 }
 # Library rows describe the actual selected archive's instructions. Resolve
 # their dependencies through the same graph as application definitions.
 foreach($f in $LibraryFunctions) {
  $row=@{};foreach($key in $f.Keys){$row[$key]=$f[$key]}
  $row.writes=$row.localWrites
  # Existing library contracts describe GPRs only. Do not infer DPX effects
  # from a missing field in an older, hash-bound runtime manifest.
  $row.localDpxWrites=3;$row.dpxWrites=3
  $row.preserved=0L
  if(-not $symbols.ContainsKey($row.symbol)){$symbols[$row.symbol]=@()}
  $symbols[$row.symbol]+=@($row);$all+=@($row)
 }
 # 单调并集覆盖直接调用闭包；未知定义、局部重名或特殊 ABI 都贡献 top。
 do {
  $changed=$false
  foreach($row in $all) {
   $mask=$row.localWrites;$dpx=$row.localDpxWrites
   foreach($callee in $row.calls) {
    $definitions=@(if($symbols.ContainsKey($callee)){$symbols[$callee]})
    $local=@($definitions|Where-Object{$_.unit -ceq $row.unit -and -not $_.exported})
    $targets=@(if($local.Count){$local}else{$definitions|Where-Object{$_.exported}})
    if($targets.Count -ne 1 -or $targets[0].abi -notin @(2,4)){$mask=4294967295L;$dpx=3;break}
    $mask=$mask -bor $targets[0].writes
    $dpx=$dpx -bor $targets[0].dpxWrites
   }
   if($row.preserved){$mask=$mask -band (4294967295L -bxor [long]$row.preserved)}
   if($mask -ne $row.writes){$row.writes=$mask;$changed=$true}
   if($dpx -ne $row.dpxWrites){$row.dpxWrites=$dpx;$changed=$true}
  }
 } while($changed)
 return @{symbols=$symbols;functions=$all}
}

function Get-Mx251BindingKey($Bindings,[string]$Unit,[string]$Symbol) {
 $local=$Unit+':'+$Symbol
 if($Bindings.ContainsKey($local)){return $local}
 if($Bindings.ContainsKey($Symbol)){return $Symbol}
 return $null
}

function Invoke-Mx251CallClobberFeedback([string]$Root,$Config,$Inputs,[string]$CompilerSha,[string]$ProfileSha,[switch]$ScopedLocals,$LibraryFunctions=@(),[switch]$CalleePreservation,$MemoryLayout=$null) {
 $record=[ordered]@{schema=1;status='NO_ELIGIBLE_CLOSURE';compilerSha256=$CompilerSha;profileLockSha256=$ProfileSha;inputs=$Inputs;bindings=@();recompiledUnits=@();finalValidated=$false;fallback=$false}
 if(@($Config.archives).Count){$record.status='FALLBACK_PROJECT_ARCHIVE_BINDING_UNPROVEN';return $record}
 # 绑定实际对象定义，而非仅凭源函数名；同名或未定义目标不进入反馈。
 $owners=New-Mx251OrdinalMap
 foreach($u in $Config.units){
  $text=[IO.File]::ReadAllText((Join-Path $Root ($u.name+'.rel')))
  foreach($m in [regex]::Matches($text,'(?m)^S ([^\s]+) Def[0-9A-Fa-f]+\s*$')){
   $name=$m.Groups[1].Value;if(-not $owners.ContainsKey($name)){$owners[$name]=@()};$owners[$name]+=@($u.name)
  }
 }
 # A project definition takes precedence over a library definition, including
 # assembly definitions for which the compiler has no function metadata.
 $LibraryFunctions=@($LibraryFunctions|Where-Object{-not $owners.ContainsKey($_.symbol)})
 foreach($f in $LibraryFunctions){$owners[$f.symbol]=@($f.unit)}
 $scan=Get-Mx251BankFunctions $Root $Config $LibraryFunctions $MemoryLayout
 $preservation=@{selected=@();files=(New-Mx251OrdinalMap)}
 if($CalleePreservation) {
  $preservation=Select-Mx251CalleePreservation $Root $scan $owners $Config
  $record.calleePreservation=@{selected=$preservation.selected;files=@()}
  foreach($u in $Config.units) {
   if(-not $preservation.files.ContainsKey($u.name)){continue}
   $preserveFile=$preservation.files[$u.name]
   $record.calleePreservation.files+=@{unit=$u.name;path=$preserveFile;sha256=(Hash-File $preserveFile)}
   $args=@(Flags $u.backend $u.stackAuto $u.defines)+@('--mcs251-callee-preserve',$preserveFile,'-c',$u.file,'-o',($u.name+'.rel'))
   Run-Tool ('preserve-'+$u.name) $compilerExe $args;Check-Assembly (Join-Path $Root ($u.name+'.asm'))
  }
  if($preservation.files.Count){$scan=Get-Mx251BankFunctions $Root $Config $LibraryFunctions $MemoryLayout}
 }
 $bindings=@()
 foreach($key in ($scan.symbols.Keys|Sort-Object -CaseSensitive)) {
  $defs=@($scan.symbols[$key]);$defs=@($defs|Where-Object{$_.exported})
  if($defs.Count -eq 1 -and $defs[0].abi -in @(2,4) -and $defs[0].writes -ne 4294967295L -and
     $owners.ContainsKey($key) -and $owners[$key].Count -eq 1 -and $owners[$key][0] -ceq $defs[0].unit){
   $bindings+=@{symbol=$key;key=$key;local=$false;unit=$defs[0].unit;abi=$defs[0].abi;writes=$defs[0].writes;dpxWrites=$defs[0].dpxWrites}
  }
 }
 if($ScopedLocals) {
  foreach($f in $scan.functions) {
   if($f.exported -or $f.abi -notin @(2,4) -or $f.writes -eq 4294967295L){continue}
   $same=@($scan.symbols[$f.symbol]|Where-Object{$_.unit -ceq $f.unit})
   if($same.Count -eq 1){$bindings+=@{symbol=$f.symbol;key=($f.unit+':'+$f.symbol);local=$true;unit=$f.unit;abi=$f.abi;writes=$f.writes;dpxWrites=$f.dpxWrites}}
  }
 }
 $record.bindings=$bindings
 $record.analysis=@($scan.functions|ForEach-Object{@{symbol=$_.symbol;unit=$_.unit;abi=$_.abi;localWrites=$_.localWrites;writes=$_.writes;localDpxWrites=$_.localDpxWrites;dpxWrites=$_.dpxWrites;calls=$_.calls;reason=$_.reason;instructions=$_.instructions}})
 if(-not $bindings.Count){return $record}
 $byName=New-Mx251OrdinalMap;foreach($b in $bindings){$byName[$b.key]=$b}
 $units=@()
 foreach($u in $Config.units){
  if([IO.Path]::GetExtension($u.file) -cne '.c' -or $u.backend -ceq 'legacy'){continue}
  $needed=$false
  foreach($f in @($scan.functions|Where-Object{$_.unit -ceq $u.name})) {
   foreach($callee in $f.calls){if(Get-Mx251BindingKey $byName $f.unit $callee){$needed=$true;break}}
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
  $lines=@('MX251-CALL-CLOBBERS-3')+@($bindings|Where-Object{-not $_.local -and -not $disabled.Contains($_.key)}|ForEach-Object{('global '+$_.symbol+' '+$_.abi+' {0:x8} {1}' -f [long]$_.writes,$_.dpxWrites)})
  [IO.File]::WriteAllText($path,($lines -join "`n")+"`n",[Text.UTF8Encoding]::new($false));$record.feedbackSha256=Hash-File $path
  $record.feedbackFiles=@()
  foreach($u in $(if($ScopedLocals){$units}else{@()})){
   $unitPath=Join-Path $Root ('call-clobbers-'+$u.name+'.txt')
   $unitLines=@('MX251-CALL-CLOBBERS-3')+@($bindings|Where-Object{(-not $_.local -or $_.unit -ceq $u.name) -and -not $disabled.Contains($_.key)}|ForEach-Object{($(if($_.local){'local'}else{'global'})+' '+$_.symbol+' '+$_.abi+' {0:x8} {1}' -f [long]$_.writes,$_.dpxWrites)})
   [IO.File]::WriteAllText($unitPath,($unitLines -join "`n")+"`n",[Text.UTF8Encoding]::new($false))
   $record.feedbackFiles+=@{path=$unitPath;sha256=(Hash-File $unitPath)}
  }
  foreach($u in $nextUnits){
   $unitFacts=if($ScopedLocals){'call-clobbers-'+$u.name+'.txt'}else{'call-clobbers.txt'}
   $args=@(Flags $u.backend $u.stackAuto $u.defines)+@('--mcs251-leaf-effects',$unitFacts,'-c',$u.file,'-o',($u.name+'.rel'))
   if($preservation.files.ContainsKey($u.name)){$args+=@('--mcs251-callee-preserve',$preservation.files[$u.name])}
   Run-Tool ('clobber-'+$u.name) $compilerExe $args;Check-Assembly (Join-Path $Root ($u.name+'.asm'))
   $record.recompiledUnits+=@($u.name)
  }
  if((Hash-File $compilerExe) -cne $CompilerSha -or (Hash-File $lockPath) -cne $ProfileSha -or (Hash-File $path) -cne $record.feedbackSha256){throw 'clobber-feedback-identity-changed'}
  foreach($f in $record.feedbackFiles){if((Hash-File $f.path) -cne $f.sha256){throw 'clobber-unit-feedback-changed'}}
  foreach($entry in $Inputs.GetEnumerator()){if((Hash-File (Join-Path $Root $entry.Key)) -cne $entry.Value.sha256){throw 'clobber-feedback-source-changed'}}
  $final=Get-Mx251BankFunctions $Root $Config $LibraryFunctions $MemoryLayout
  $expanded=@()
  foreach($b in $bindings){
   if(-not $final.symbols.ContainsKey($b.symbol)){throw 'clobber-final-definition-missing'}
   $defs=@($final.symbols[$b.symbol]|Where-Object{if($b.local){-not $_.exported -and $_.unit -ceq $b.unit}else{$_.exported}})
   if($defs.Count -ne 1 -or $defs[0].unit -cne $b.unit -or $defs[0].abi -ne $b.abi){throw ('clobber-final-definition-changed:'+ $b.symbol)}
   if(($defs[0].writes -band (4294967295L -bxor $b.writes)) -or ($defs[0].dpxWrites -band (3 -bxor $b.dpxWrites))){$expanded+=@{symbol=$b.key;before=$b.writes;after=$defs[0].writes;beforeDpx=$b.dpxWrites;afterDpx=$defs[0].dpxWrites}}
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
    $boundary=Get-Mx251BankBody $f.body.Substring($start,$m.Index-$start) $f.aliases $f.objects -SfrLayout $f.sfrLayout
    if($boundary.calls.Count -ne 1 -or $boundary.calls[0] -cne $m.Groups[2].Value){$boundary.writes=4294967295L;$boundary.reason='unexpected-call-boundary'}
    $uses+=@{caller=$f.symbol;unit=$f.unit;callee=$m.Groups[2].Value;removed=[Convert]::ToInt32($m.Groups[3].Value,16);kind='ordinary';boundaryWrites=$boundary.writes;boundaryReason=$boundary.reason}
    $pending=$null
   }
   if($null -ne $pending){throw 'clobber-unclosed-ordinary-save'}
   foreach($m in [regex]::Matches($f.body,'(?m)^; mx251-machine-call callee=([^\s]+) writes=([0-9a-f]{8})\s*$')){
    $mask=[Convert]::ToUInt32($m.Groups[2].Value,16)
    $uses+=@{caller=$f.symbol;unit=$f.unit;callee=$m.Groups[1].Value;removed=(4294967295L -bxor $mask);kind='machine'}
   }
   foreach($m in [regex]::Matches($f.body,'(?m)^; mx251-isr-call callee=([^\s]+) omitted=([0-9a-f]{8})\s*$')){
    $mask=[Convert]::ToUInt32($m.Groups[2].Value,16)
    $uses+=@{caller=$f.symbol;unit=$f.unit;callee=$m.Groups[1].Value;removed=[long]$mask;kind='isr'}
   }
   foreach($m in [regex]::Matches($f.body,'(?m)^; mx251-isr-dpx-call callee=([^\s]+) omitted=([1-3])\s*$')){
    $uses+=@{caller=$f.symbol;unit=$f.unit;callee=$m.Groups[1].Value;removed=0L;removedDpx=[int]$m.Groups[2].Value;kind='isr-dpx'}
   }
   foreach($m in [regex]::Matches($f.body,'(?m)^; mx251-call-save caller=([^\s]+) site=([0-9]+) callee=([^\s]+) omitted=([0-9a-f]{8}) groups=([0-9]+) bytes=([0-9]+) verified=([01])\s*$')){
    if($m.Groups[1].Value -cne $f.symbol -or $m.Groups[7].Value -cne '1'){throw 'clobber-call-setup-proof-missing'}
    $mask=[Convert]::ToUInt32($m.Groups[4].Value,16)
    $uses+=@{caller=$f.symbol;unit=$f.unit;callee=$m.Groups[3].Value;removed=[long]$mask;kind='wide';site=[int]$m.Groups[2].Value}
   }
  }
  $record.actualUses=$uses
  $violations=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  foreach($use in $uses){
   $key=Get-Mx251BindingKey $byName $use.unit $use.callee
   if(-not $key -or -not $final.symbols.ContainsKey($use.callee)){throw 'clobber-unbound-actual-call'}
   $binding=$byName[$key];$use.bindingKey=$key
   $defs=@($final.symbols[$use.callee]|Where-Object{if($binding.local){-not $_.exported -and $_.unit -ceq $use.unit}else{$_.exported}})
   if($defs.Count -ne 1 -or $defs[0].abi -ne $binding.abi){throw ('clobber-actual-binding-violated:'+ $use.callee)}
   if(($defs[0].writes -band $use.removed) -or ($use.kind -ceq 'ordinary' -and ($use.boundaryWrites -band $use.removed))){$null=$violations.Add($key)}
   if($use.kind -ceq 'isr-dpx' -and ($defs[0].dpxWrites -band $use.removedDpx)){$null=$violations.Add($key)}
   $use.finalCalleeWrites=$defs[0].writes
   $use.finalCalleeDpxWrites=$defs[0].dpxWrites
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
    $binding=$byName[$growth.symbol];$before=$binding.writes;$beforeDpx=$binding.dpxWrites
    $binding.writes=$binding.writes -bor $growth.after
    $binding.dpxWrites=$binding.dpxWrites -bor $growth.afterDpx
    if($binding.writes -ne $before -or $binding.dpxWrites -ne $beforeDpx){$repairProgress=$true;$null=$repairTargets.Add($growth.symbol)}
    $record.repairBounds+=@{symbol=$growth.symbol;before=$before;after=$binding.writes;beforeDpx=$beforeDpx;afterDpx=$binding.dpxWrites}
   }
   foreach($name in $violations){
    # 普通调用参数准备/结果搬运的破坏并不属于 callee。不能靠扩大 callee
    # 写集合伪造其来源；此类冲突和未解释冲突继续撤销该目标的反馈。
    $boundaryConflict=@($uses|Where-Object{$_.bindingKey -ceq $name -and $_.kind -ceq 'ordinary' -and ($_.boundaryWrites -band $_.removed)}).Count
    if($boundaryConflict -or -not $repairTargets.Contains($name)){if($disabled.Add($name)){$repairProgress=$true;$newDisabled+=@($name)}}
    $null=$repairTargets.Add($name)
   }
   $record.repairHistory+=@{attempt=$attempt;expanded=$record.repairBounds;disabledAdded=$newDisabled;progress=$repairProgress}
   if(-not $repairProgress){throw 'clobber-repair-no-progress'}
   # 只重新编译引用变化事实的单元；最终仍核对整个调用闭包。
   $nextUnits=@($units|Where-Object{
    $name=$_.name;$needed=$false
    foreach($f in @($scan.functions|Where-Object{$_.unit -ceq $name})){
     foreach($callee in $f.calls){if($repairTargets.Contains((Get-Mx251BindingKey $byName $f.unit $callee))){$needed=$true;break}}
    }
    $needed
   })
   if(-not $nextUnits.Count){throw 'clobber-repair-callers-missing'}
   continue
  }
  $record.status='FINAL_ACTUAL_CALL_PRESERVATION_VERIFIED';$record.finalValidated=$true
  $record.finalAnalysis=@($final.functions|ForEach-Object{@{symbol=$_.symbol;unit=$_.unit;abi=$_.abi;writes=$_.writes;dpxWrites=$_.dpxWrites;reason=$_.reason}})
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
