# 无参 void 回调与公开格式化入口的类型不相容。这里只证明它不能直接
# 调用该格式化入口，不推断回调目标、破坏集合或运行次数。整个镜像的
# 格式入口引用、参数、库成员和不透明汇编检查仍由原格式运行库完成。
function Get-Mx251FormatIndirectEvidence([string]$Text) {
 $rows=@();$covered=0;$owners=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 $pattern='(?ms)^; mx251-function begin schema=1 symbol=(?<name>\S+) isr=[01] addressTaken=[01] opaque=[01] indirect=(?<indirect>[01])\r?\n(?<body>.*?)^; mx251-function end schema=1 symbol=\k<name>\s*$'
 $blocks=[regex]::Matches($Text,$pattern)
 $begins=[regex]::Matches($Text,'(?m)^; mx251-function begin schema=1 ').Count
 $ends=[regex]::Matches($Text,'(?m)^; mx251-function end schema=1 ').Count
 if($begins -ne $blocks.Count -or $ends -ne $blocks.Count){throw 'format-indirect-function-boundary'}
 foreach($block in $blocks){
  $name=$block.Groups['name'].Value;$body=$block.Groups['body'].Value
  if(-not $owners.Add($name)){throw 'format-indirect-duplicate-function'}
  $certs=[regex]::Matches($body,'(?m)^; mx251-indirect-signature schema=1 owner=(\S+) calls=([1-9][0-9]*) abi2_void0=([01])\s*$')
  if($block.Groups['indirect'].Value -ceq '0'){
   if($certs.Count){throw 'format-indirect-unexpected-certificate'}
   if([regex]::IsMatch($body,'(?m)^\s*ecall\s+@')){throw 'format-indirect-unmarked-machine-call'}
   continue
  }
  if($certs.Count -ne 1 -or $certs[0].Groups[1].Value -cne $name -or $certs[0].Groups[3].Value -cne '1'){throw 'format-indirect-signature-not-disjoint'}
  $count=[int]$certs[0].Groups[2].Value
  $actual=0
  foreach($line in $body -split '\r?\n'){
   $code=($line -split ';',2)[0].Trim()
   if($code -cmatch '^(?:ecall|lcall|acall|ejmp|ljmp|sjmp)\s+@'){
    if($code -cnotmatch '^ecall\s+@(?:dr(?:0|4|8|12|16|20|24|28)|dpx)$'){throw 'format-indirect-machine-transport'}
    $actual++
   }
  }
  if($actual -ne $count){throw 'format-indirect-machine-count'}
  $covered+=$actual
  $rows+=@{owner=$name;calls=$count;signature='ABI2 void(void)';machineForm='ECALL @DR';targets='UNKNOWN_RETAIN_ALL';proof='Complete nonvariadic prototype incompatible with each protected formatting API. Existing whole-image address-reference guard still required.'}
 }
 if([regex]::Matches($Text,'(?m)^\s*ecall\s+@').Count -ne $covered){throw 'format-indirect-unowned-machine-call'}
 return @($rows)
}
