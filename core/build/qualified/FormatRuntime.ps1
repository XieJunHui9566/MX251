# 普通 ABI 的闭合整数格式运行库。原应用对象不改；未知条件保留完整库。
function Get-Mx251FormatPlan([string]$Format) { return (Get-Mx251ExtendedFormatPlan $Format) }

function Read-Mx251FormatConstants([string[]]$Lines) {
 $table=[hashtable]::new([StringComparer]::Ordinal);$area=''
 foreach($raw in $Lines) {
  $line=$raw.Trim()
  if($line -cmatch '^\.area\s+(\S+)'){$area=$Matches[1];continue}
  if($area -ceq 'CONST' -and $line -cmatch '^; mx251-string-literal schema=1 symbol=([A-Za-z_.$][A-Za-z_.$0-9]*) bytes=((?:[0-9a-f]{2})+)$'){
   $label=$Matches[1];$bytes=[Convert]::FromHexString($Matches[2]);$end=[Array]::IndexOf($bytes,[byte]0)
   if($end -lt 0 -or $end -gt 32767){continue}
   $prefix=if($end){$bytes[0..($end-1)]}else{@()}
   if($table.ContainsKey($label)){throw 'duplicate-literal-metadata'}
   # Latin-1 仅作字节到字符串的一一映射，不转码项目的中文/UTF-8 数据。
   $table[$label]=[Text.Encoding]::Latin1.GetString([byte[]]$prefix)
  }
 }
 return ,$table
}

function Read-Mx251FormatArgument([string]$Text) {
 $lines=@($Text.Split("`n")|ForEach-Object{($_ -split ';',2)[0].Trim()}|Where-Object{$_})
 $bytes=0
 foreach($line in $lines){
  if($line -cmatch '^pushw\s+#[^\r\n]+$'){$bytes+=2;continue}
  if($line -cmatch '^push\s+([A-Za-z0-9_.$() +><-]+)$'){
   $operand=$Matches[1];$bytes+=if($operand -cmatch '^dr[0-9]+$'){4}elseif($operand -cmatch '^wr[0-9]+$'){2}else{1};continue
  }
  # 只接受直线取参数。未知的栈写、控制流或临时 push/pop 不猜作参数槽。
  if($line -cnotmatch '^(mov|movx|movc|movz|movs|add|addc|sub|subb|anl|orl|xrl|clr|cpl|swap|inc|dec)\s+') {throw ('unsupported-argument-setup:'+ $line)}
  if($line -match '^(?:\w+)\s+(?:spx|sp|sph|spxl|0x81|0x85)\s*(?:,|$)'){throw 'argument-stack-write'}
 }
 $joined=$lines -join "`n";$symbol=$null
 $m=[regex]::Match($joined,'^mov\s+a,#\((?<s>[A-Za-z_.$][A-Za-z_.$0-9]*) >> 16\)\npush\s+acc\npushw\s+#\k<s>\nmov\s+a,#\k<s>$')
 if(-not $m.Success){$m=[regex]::Match($joined,'^mov\s+a,#\((?<s>[A-Za-z_.$][A-Za-z_.$0-9]*) >> 16\)\npush\s+acc\nmov\s+a,#\(\k<s> >> 8\)\npush\s+acc\nmov\s+a,#\k<s>\npush\s+acc$')}
 if($m.Success){$symbol=$m.Groups['s'].Value}
 return @{bytes=$bytes;constantSymbol=$symbol}
}

function Get-Mx251FormatHome([string]$Map) {
 $m=[regex]::Matches([IO.File]::ReadAllText($Map),'(?m)^HOME\s+([0-9A-F]{8})\s+([0-9A-F]{8})\s+=')
 if($m.Count -ne 1){throw 'format-home-layout-unproved'}
 return @{base=[Convert]::ToInt64($m[0].Groups[1].Value,16);size=[Convert]::ToInt64($m[0].Groups[2].Value,16)}
}

function Get-Mx251FormatRawVectors([string]$Root,$Unit,$formatHome,$Slots) {
 $path=Join-Path $Root $Unit.file;$rel=Read-Mx251GcRel ([IO.File]::ReadAllBytes((Join-Path $Root ($Unit.name+'.rel')))) $Unit.name
 if(@($rel.Definitions|Where-Object{$_ -cne '.__.ABS.'}).Count -or $rel.References.Count){throw 'format-raw-symbol-entrance'}
 $area='';$at=$null;$jumps=@();$reserved=0
 foreach($raw in [IO.File]::ReadAllLines($path)){
  $code=($raw -split ';',2)[0].Trim();if(-not $code){continue}
  if($code -cmatch '^\.module\s+[A-Za-z_.$][A-Za-z_.$0-9]*$'){continue}
  if($code -cmatch '^\.area\s+([A-Za-z_.$][A-Za-z_.$0-9]*)\s+\(ABS,CODE\)$'){$area='absolute';$at=$null;continue}
  if($code -cmatch '^\.area\s+HOME\s+\(CODE\)$'){$area='home';$at=$null;continue}
  if($area -ceq 'absolute' -and $code -cmatch '^\.org\s+(0x[0-9a-fA-F]+|[0-9]+)$'){$at=if($Matches[1].StartsWith('0x')){[Convert]::ToInt64($Matches[1].Substring(2),16)}else{[long]$Matches[1]};continue}
  if($area -ceq 'home' -and $code -cmatch '^\.ds\s+(0x[0-9a-fA-F]+|[0-9]+)$'){$reserved+=if($Matches[1].StartsWith('0x')){[Convert]::ToInt64($Matches[1].Substring(2),16)}else{[long]$Matches[1]};continue}
  if($area -ceq 'absolute' -and $null -ne $at -and $code -cmatch '^ljmp\s+(0x[0-9a-fA-F]+|[0-9]+)$'){
   $target=if($Matches[1].StartsWith('0x')){[Convert]::ToInt64($Matches[1].Substring(2),16)}else{[long]$Matches[1]}
   $offset=$target-$formatHome.base-3
   if($at -lt $formatHome.base -or $at+3 -gt $formatHome.base+$formatHome.size -or $offset -lt 0 -or $offset%8 -or -not $Slots.Contains([int]($offset/8)) -or ($at -shr 16) -ne ($target -shr 16)){throw 'format-raw-jump-outside-registered-vector'}
   $jumps+=@{source=$at;target=$target;slot=[int]($offset/8)};$at+=3;continue
  }
  throw 'format-unknown-raw-instruction'
 }
 if(-not $jumps.Count -or $reserved -gt $formatHome.size){throw 'format-raw-reservation-unproved'}
 return @{unit=$Unit.name;assembly=$Unit.file;sha256=(Hash-File $path);jumps=$jumps;reserved=$reserved}
}

function Get-Mx251FormatDemand([string]$Root,$Config) {
 if($Config.runtime -cne 'standard' -or $Config.explicitAbi -or @($Config.archives).Count){throw 'nonstandard-runtime-or-project-archive'}
 $calls=@();$fingerprints=@();$rawVectors=@();$indirectEvidence=@();$apiPattern='(?<![A-Za-z0-9_.$])_+(?:printf|vprintf|sprintf|vsprintf|snprintf|vsnprintf|fprintf|vfprintf|print_format)(?![A-Za-z0-9_.$])'
 $formatHome=Get-Mx251FormatHome (Join-Path $Root 'linked.partial.map')
 $slots=[Collections.Generic.HashSet[int]]::new()
 foreach($u in $Config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.c'}){
  foreach($m in [regex]::Matches([IO.File]::ReadAllText((Join-Path $Root ($u.name+'.asm'))),'(?m)^; mx251-irq-definition schema=2 symbol=\S+ slot=([0-9]+) private=[01]\s*$')){$null=$slots.Add([int]$m.Groups[1].Value)}
 }
 foreach($u in $Config.units){
  if([IO.Path]::GetExtension($u.file) -cne '.c'){
   $rawVectors+=@(Get-Mx251FormatRawVectors $Root $u $formatHome $slots)
   $fingerprints+=@{path=$u.file;sha256=(Hash-File (Join-Path $Root $u.file))}
   $fingerprints+=@{path=$u.name+'.rel';sha256=(Hash-File (Join-Path $Root ($u.name+'.rel')))}
   continue
  }
  if(-not $u.stackAuto -or $u.backend -ceq 'legacy'){throw 'format-unproved-unit-abi'}
  $path=Join-Path $Root ($u.name+'.asm');$text=[IO.File]::ReadAllText($path)
  $fingerprints+=@{path=$u.name+'.asm';sha256=(Hash-File $path)}
  $fingerprints+=@{path=$u.name+'.rel';sha256=(Hash-File (Join-Path $Root ($u.name+'.rel')))}
  $lines=$text -split '\r?\n';$constants=Read-Mx251FormatConstants $lines
  foreach($raw in $lines){
   $code=($raw -split ';',2)[0].Trim()
   if($code -cmatch '^\.(?:globl|ascii)\s+'){continue}
   if([regex]::IsMatch($code,$apiPattern) -and $code -cnotmatch '^ecall\s+_(printf|sprintf)$'){throw 'format-address-escape-or-unknown-entry'}
  }
  $metadata=[regex]::Matches($text,'(?m)^; mx251-function begin schema=1 symbol=(\S+) isr=([01]) addressTaken=([01]) opaque=([01]) indirect=([01])\s*$')
  if(-not $metadata.Count -or @($metadata|Where-Object{$_.Groups[4].Value -ne '0'}).Count){throw 'format-opaque-unit'}
  $indirectEvidence+=@(Get-Mx251FormatIndirectEvidence $text | ForEach-Object{$_['unit']=$u.name;$_})
  $inside=$false;$groupText='';$groups=@();$caller=$null;$callerAbi=$false
  foreach($raw in $lines){
   if($raw -cmatch '^; mx251-function begin schema=1 symbol=(\S+)'){$caller=$Matches[1];$callerAbi=$false;$groups=@()}
   if($raw -cmatch '^;\s*mx251-bank-contract schema=1 symbol=(\S+) abi=2 bank=0 naked=0\s*$'){$callerAbi=$Matches[1] -ceq $caller}
   if($raw -cmatch '^;\s*MX251 argument region begin\s*$'){
    if($inside){throw 'nested-argument-region'};$inside=$true;$groupText='';continue
   }
   if($raw -cmatch '^;\s*MX251 argument region end\s*$'){
    if(-not $inside){throw 'argument-end-without-begin'}
    try{$groups+=@(Read-Mx251FormatArgument $groupText)}catch{$groups+=@{bytes=-1;constantSymbol=$null;reason=$_.Exception.Message}}
    $inside=$false;continue
   }
   if($inside){$groupText+=$raw+"`n";continue}
   $code=($raw -split ';',2)[0].Trim();if(-not $code){continue}
   if($code -cmatch '^\.globl\s+\S+$'){continue}
   if([regex]::IsMatch($code,$apiPattern)){
    if($code -cnotmatch '^ecall\s+_(printf|sprintf)$'){throw 'format-address-escape-or-unknown-entry'}
    $api=$Matches[1];$fixed=if($api -ceq 'sprintf'){2}else{1}
    if(-not $callerAbi -or $groups.Count -lt $fixed){throw 'format-missing-abi-or-arguments'}
    $fg=$groups[$groups.Count-$fixed]
    if($fg.bytes -ne 3 -or -not $fg.constantSymbol -or -not $constants.ContainsKey($fg.constantSymbol)){throw 'format-not-immutable-code-literal'}
    if($fixed -eq 2 -and $groups[-1].bytes -ne 3){throw 'format-buffer-pointer-width'}
    $plan=Get-Mx251FormatPlan $constants[$fg.constantSymbol]
    if($groups.Count -ne $fixed+$plan.tokens.Count){throw 'format-argument-count-mismatch'}
    # C 变参按逆序入栈；按格式的正序消费。不能把混合 2/4 字节槽错配。
    for($i=0;$i -lt $plan.tokens.Count;$i++){
     $argument=$groups[$plan.tokens.Count-1-$i]
     if($argument.bytes -ne $plan.tokens[$i].argumentBytes){throw 'format-argument-width-mismatch'}
    }
    $calls+=@{unit=$u.name;caller=$caller;api=$api;constantSymbol=$fg.constantSymbol;plan=$plan;argumentBytes=@($groups|ForEach-Object{$_.bytes})}
    $groups=@();continue
   }
   if($code -match '^(?:ecall|lcall|acall|ejmp|ljmp|sjmp|j\w+|ret|eret|reti)\b' -or $code -match ':$'){$groups=@();continue}
   if($groups.Count -and $code -match '^(push|pushw|pop)\b'){$groups+=@{bytes=-1;constantSymbol=$null;reason='interleaved-argument-stack-change'}}
  }
  if($inside){throw 'unclosed-argument-region'}
 }
 if(-not $calls.Count){throw 'no-constant-format-calls'}
 return @{calls=$calls;inputs=$fingerprints;indirectEvidence=$indirectEvidence;rawVectors=$rawVectors;home=$formatHome;publicAbi=2;genericPointerBytes=3;integerBytes=2}
}

function New-Mx251FormatDecision($Tokens) {
 $rows=@($Tokens|Group-Object -Property raw -CaseSensitive|ForEach-Object{$_.Group[0]})
 if(-not $rows.Count -or $rows.Count -gt 127){throw 'format-conversion-tree-capacity'}
 if($rows.Count -eq 1){return @{leaf=$rows[0]}}
 $limit=($rows|ForEach-Object{$_.raw.Length-1}|Measure-Object -Minimum).Minimum
 $best=$null
 for($offset=0;$offset -lt $limit;$offset++){
  $groups=@($rows|Group-Object -Property { [int]$_.raw[$offset+1] })
  if($groups.Count -le 1){continue}
  $maximum=($groups|ForEach-Object{$_.Count}|Measure-Object -Maximum).Maximum
  if($null -eq $best -or $maximum -lt $best.maximum -or ($maximum -eq $best.maximum -and $groups.Count -lt $best.count)){$best=@{offset=$offset;groups=$groups;maximum=$maximum;count=$groups.Count}}
 }
 if($null -eq $best){throw 'format-no-bounded-discriminator'}
 $branches=@()
 foreach($group in @($best.groups|Sort-Object -Property {[int]$_.Name})){$branches+=@{byte=[int]$group.Name;tree=(New-Mx251FormatDecision @($group.Group))}}
 return @{offset=$best.offset;limit=$limit;branches=$branches}
}

function Write-Mx251FormatDecision($Tree,[int]$Depth=2) {
 $indent='    '*$Depth
 if($Tree.ContainsKey('leaf')){
  $t=$Tree.leaf
  return ($indent+'cursor+='+($t.raw.Length-1)+"u;`n"+$indent+'radix='+$t.radix+'u; width='+$t.width+'u; flags='+$t.flags+"u;`n")
 }
 $source=''
 for($i=0;$i -lt $Tree.branches.Count;$i++){
  $b=$Tree.branches[$i]
  $head=if($i -eq $Tree.branches.Count-1){'else'}elseif($i -eq 0){'if'}else{'else if'}
  if($head -cne 'else'){$head+=' (cursor['+$Tree.offset+'] == '+$b.byte+'u)'}
  $source+=$indent+$head+" {`n"+(Write-Mx251FormatDecision $b.tree ($Depth+1))+$indent+"}`n"
 }
 return $source
}

function New-Mx251FormatSource($Demand,[string]$Kernel) {
 if(@($Demand.calls|ForEach-Object{$_.plan.tokens}|Where-Object{$_.bits -eq 32 -or $_.kind -ceq 'character'}).Count){return (New-Mx251ExtendedFormatSource $Demand $Kernel)}
 $result='#include <stdint.h>'+"`n"+'#include <stdarg.h>'+"`n"+'#include <stdio.h>'+"`n"
 foreach($api in @('printf','sprintf')){
  $calls=@($Demand.calls|Where-Object{$_.api -ceq $api});if(-not $calls.Count){continue}
  $ops=@($calls|ForEach-Object{$_.plan.tokens}|ForEach-Object{$_})
  $prefix='mx_'+$api;$buffer=$api -ceq 'sprintf'
  if($ops.Count){
   $body=$Kernel.Substring($Kernel.IndexOf('static unsigned char mx_number'))
   $body=$body.Replace('mx_number',$prefix+'_number')
   if($buffer){
    $body=$body.Replace('uint16_t value,','char *destination, uint16_t value,')
    $body=$body.Replace("putchar(' ')","*destination++=' '").Replace("putchar('-')","*destination++='-'").Replace("putchar('0')","*destination++='0'").Replace('putchar(digits[--n])','*destination++=(char)digits[--n]')
   }
   # 复用已评审的能力裁剪：不可能出现的符号/字母大小写/填充分支删除。
   foreach($pair in @(@(1,'(flags & 1u)'),@(2,'(flags&2u)'),@(4,'(flags&4u)'))){
    $values=@($ops|ForEach-Object{[int](($_.flags -band $pair[0]) -ne 0)}|Sort-Object -Unique)
    if($values.Count -eq 1){$body=$body.Replace($pair[1],('('+[string]$values[0]+')'))}
   }
   # 与原生成器相同：单一能力化为常量，多种能力保留形参。
   $extra=@();$constants=''
   foreach($field in @('radix','width','flags')){
    $values=@($ops|ForEach-Object{$_[$field]}|Sort-Object -Unique)
    if($values.Count -eq 1){$body=[regex]::Replace($body,',\s*unsigned char '+$field+'\b','');$constants+='    const unsigned char '+$field+'='+$values[0]+"u;`n"}else{$extra+=@($field)}
   }
   if($constants){$first=$body.IndexOf('{')+1;$body=$body.Substring(0,$first)+"`n"+$constants+$body.Substring($first)}
   $result+=$body+"`n"
  } else {$extra=@()}
  $signature=if($buffer){'char *buffer, const char *format, ...'}else{'const char *format, ...'}
  $result+='int '+$api+'('+$signature+")`n{`n"
  if($buffer){$result+="    char *destination=buffer;`n"}
  $result+="    const unsigned char __code *cursor=(const unsigned char __code *)format;`n    unsigned char c, width, flags, radix, emitted;`n    uint16_t value;`n    int count=0;`n    va_list ap;`n    va_start(ap,format);`n    while ((c=*cursor++)!=0) {`n"
  $sink=if($buffer){'*destination++=(char)c;'}else{'putchar(c);'}
  if($ops.Count){
   $tree=New-Mx251FormatDecision $ops
   $Demand['decisions_'+$api]=$tree
   $result+="        if(c!='%') { $sink ++count; continue; }`n        if(*cursor=='%') { ++cursor; $sink ++count; continue; }`n"
   $result+=Write-Mx251FormatDecision $tree
   $result+="        if(flags&1u) value=(uint16_t)va_arg(ap,int); else value=va_arg(ap,unsigned int);`n"
   # 原调用对象必须给出精确 2 字节槽；先截低 8 位，再按格式进行符号扩展。
   if(@($ops|Where-Object{$_.byteConversion}).Count){
    $result+="        if(flags&8u) { if(flags&1u) value=(uint16_t)(int16_t)(int8_t)(uint8_t)value; else value=(uint8_t)value; }`n"
   }
   $args=if($buffer){'destination,value'}else{'value'}
   foreach($field in $extra){$args+=','+$field}
   $result+='        emitted='+$prefix+'_number('+$args+");`n"
   if($buffer){$result+="        destination+=emitted;`n"}
   $result+="        count+=emitted;`n"
  } else {
   $result+="        if(c=='%') ++cursor;`n        $sink ++count;`n"
  }
  $result+="    }`n"
  if($buffer){$result+="    *destination=0;`n"}
  $result+="    va_end(ap);`n    return count;`n}`n"
 }
 return $result
}

function Get-Mx251FormatLibraries([string]$Map,[string]$ToolRoot,$Contract,[switch]$Final) {
 $text=[IO.File]::ReadAllText($Map)
 $section=[regex]::Match($text,'(?s)Libraries Linked\s+\[ object file \](.*?)(?:User Base Address Definitions|$)')
 if(-not $section.Success){throw 'format-library-inventory-missing'}
 $rows=[regex]::Matches($section.Groups[1].Value,'(?m)^([^\r\n]+\.lib)\r?\n\s+\[\s*([^\]\s]+)\s*\]')
 if(-not $rows.Count){throw 'format-library-inventory-empty'}
 $cache=@{};$selected=@();$protected=@('vprintf.rel','sprintf.rel','printf_large.rel')
 foreach($m in $rows){
  $path=[IO.Path]::GetFullPath($m.Groups[1].Value.Trim());$member=$m.Groups[2].Value
  $relative=[IO.Path]::GetRelativePath($ToolRoot,$path).Replace('\','/')
  if(-not $Contract.archives.Contains($relative) -or (Hash-File $path) -cne $Contract.archives[$relative]){throw 'format-runtime-content-not-qualified'}
  if(-not $cache.ContainsKey($path)){$cache[$path]=@(Read-Mx251GcArchive $path)}
  $found=@($cache[$path]|Where-Object{$_.name -ceq $member})
  if($found.Count -ne 1){throw 'format-library-member-ambiguous'}
  if($Final -and $member -cin $protected){throw 'format-original-provider-still-reachable'}
  if($member -cnotin $protected){
   foreach($name in @($found[0].rel.References)+@($found[0].rel.Definitions)){
    if($name -cmatch '^_+(printf|vprintf|sprintf|vsprintf|snprintf|vsnprintf|fprintf|vfprintf|print_format)$'){throw 'format-unknown-library-entry'}
   }
  }
  $selected+=@{archive=$relative;member=$member;sha256=$Contract.archives[$relative]}
 }
 return @($selected)
}

function Get-Mx251FormatHex([string]$Path) {
 $seen=[Collections.Generic.Dictionary[long,byte]]::new();$base=0L;$eof=$false
 foreach($line in [IO.File]::ReadAllLines($Path)){
  if($eof -or $line -cnotmatch '^:([0-9A-Fa-f]{2})+$'){throw 'format-malformed-hex'}
  $b=[Convert]::FromHexString($line.Substring(1));$sum=0;foreach($v in $b){$sum+=$v}
  if($b.Length -ne $b[0]+5 -or ($sum -band 255)){throw 'format-hex-checksum'}
  $at=$b[1]*256+$b[2]
  switch($b[3]){
   0 {for($i=0;$i -lt $b[0];$i++){if(-not $seen.TryAdd($base+$at+$i,$b[4+$i])){throw 'format-overlapping-hex'}}}
   1 {if($b[0] -ne 0 -or $at){throw 'format-invalid-eof'};$eof=$true}
   2 {if($b[0] -ne 2){throw 'format-invalid-segment'};$base=([long]$b[4]*256+$b[5])*16}
   4 {if($b[0] -ne 2){throw 'format-invalid-linear'};$base=([long]$b[4]*256+$b[5])*65536}
   default {throw 'format-unsupported-hex'}
  }
 }
 if(-not $eof){throw 'format-missing-eof'}
 return ,$seen
}

function Invoke-Mx251FormatRuntime([string]$Root,$Config,[string]$ToolRoot,[string[]]$LinkArguments) {
 $record=[ordered]@{schema=1;status='FALLBACK_FULL_RUNTIME';applied=$false;hardwareExecuted=$false}
 $saved=@();$baselineCommand=$null;$trialCommand=$null;$accepted=$false
 try {
  . (Join-Path $PSScriptRoot 'FunctionGc.ps1')
  $contractPath=Join-Path $PSScriptRoot 'FormatRuntimeContract.json'
  $contract=Get-Content -LiteralPath $contractPath -Raw|ConvertFrom-Json -AsHashtable
  $kernelPath=Join-Path $PSScriptRoot 'IntegerFormatKernel.c.template'
  if($contract.schema -ne 1 -or $contract.contract -cne 'mx251.ordinary-fixed-integer-format.v2' -or (Hash-File $kernelPath) -cne $contract.kernelSha256){throw 'format-runtime-contract-mismatch'}
  if((Hash-File (Join-Path $PSScriptRoot 'ExtendedFormat.ps1')) -cne $contract.extendedGeneratorSha256){throw 'format-generator-contract-mismatch'}
  $record.contractSha256=Hash-File $contractPath
  $demand=Get-Mx251FormatDemand $Root $Config;$record.demand=$demand
  $record.parentLibraries=@(Get-Mx251FormatLibraries (Join-Path $Root 'linked.partial.map') $ToolRoot $contract)
  if(-not @($record.parentLibraries|Where-Object{$_.member -ceq 'printf_large.rel'}).Count){throw 'format-full-provider-not-selected'}
  $parentImage=Get-Mx251FormatHex (Join-Path $Root 'linked.partial.hex');$record.parentBytes=$parentImage.Count
  $source=New-Mx251FormatSource $demand ([IO.File]::ReadAllText($kernelPath))
  $sourcePath=Join-Path $Root 'format-runtime.c';[IO.File]::WriteAllText($sourcePath,$source,[Text.UTF8Encoding]::new($false))
  $record.sourceSha256=Hash-File $sourcePath
  # 库不继承项目宏或包含目录，避免同名头文件/宏改变公开运行库契约。
  $compileArgs=@('-mmcs251',('--model-'+$Config.model),'--stack-auto','--opt-code-size','--std-sdcc11','--nostdinc',('-I'+(Join-Path $ToolRoot 'include')),('-I'+(Join-Path $ToolRoot 'include/mcs51')),'--mcs251-wide-ordinary','--mcs251-function-metadata','-c','format-runtime.c','-o','format-runtime.rel')
  Run-Tool 'format-runtime' $compilerExe $compileArgs
  Check-Assembly (Join-Path $Root 'format-runtime.asm')
  $backup=Join-Path $Root 'format-baseline';[IO.Directory]::CreateDirectory($backup)|Out-Null
  foreach($p in Get-ChildItem -LiteralPath $Root -File|Where-Object{$_.Name -like 'linked.partial.*' -or $_.Extension -ceq '.rst'}){
   [IO.File]::Copy($p.FullName,(Join-Path $backup $p.Name),$false);$saved+=@{name=$p.Name;sha256=(Hash-File $p.FullName)}
  }
  $record.baselineFiles=$saved
  if($LinkArguments[-2] -cne '-o' -or $LinkArguments[-1] -cne 'linked.partial.hex'){throw 'format-unexpected-link-output'}
  $trialArgs=@($LinkArguments[0..($LinkArguments.Count-3)])+@('format-runtime.rel','-o','linked.partial.hex')
  $baselineCommand=@($commands|Where-Object{$_.label -ceq 'link'})
  if($baselineCommand.Count -ne 1){throw 'format-baseline-link-ambiguous'}
  $baselineCommand=$baselineCommand[0]
  Run-Tool 'format-link' $compilerExe $trialArgs
  $trialCommand=@($commands|Where-Object{$_.label -ceq 'format-link'})[-1]
  $record.candidateLibraries=@(Get-Mx251FormatLibraries (Join-Path $Root 'linked.partial.map') $ToolRoot $contract -Final)
  $candidateImage=Get-Mx251FormatHex (Join-Path $Root 'linked.partial.hex');$record.candidateBytes=$candidateImage.Count
  if($demand.rawVectors.Count){
   $formatHome=Get-Mx251FormatHome (Join-Path $Root 'linked.partial.map')
   if($formatHome.base -ne $demand.home.base -or $formatHome.size -ne $demand.home.size){throw 'format-raw-home-layout-changed'}
   foreach($raw in $demand.rawVectors){foreach($jump in $raw.jumps){
    $expected=@(2,(($jump.target -shr 8) -band 255),($jump.target -band 255))
    for($i=0;$i -lt 3;$i++){
     $at=$jump.source+$i
     if(-not $parentImage.ContainsKey($at) -or -not $candidateImage.ContainsKey($at) -or $parentImage[$at] -ne $expected[$i] -or $candidateImage[$at] -ne $expected[$i]){throw 'format-raw-jump-bytes-changed'}
    }
    if(-not $parentImage.ContainsKey($jump.target) -or -not $candidateImage.ContainsKey($jump.target) -or $parentImage[$jump.target] -ne 138 -or $candidateImage[$jump.target] -ne 138){throw 'format-raw-target-not-c-vector'}
   }}
   $record.rawVectorLayoutVerified=$true
  }
  if($record.candidateBytes -ge $record.parentBytes){throw 'format-not-profitable'}
  foreach($p in $demand.inputs){if((Hash-File (Join-Path $Root $p.path)) -cne $p.sha256){throw 'format-application-object-changed'}}
  if((Hash-File $contractPath) -cne $record.contractSha256 -or (Hash-File $sourcePath) -cne $record.sourceSha256){throw 'format-input-drift'}
  $baselineCommand.label='format-baseline-link';$trialCommand.label='link';$trialCommand.actualLogLabel='format-link'
  Write-Json (Join-Path $Root 'commands.json') @($commands.ToArray())
  $record.status='APPLIED_CLOSED_FIXED_INTEGER_FORMAT_RUNTIME';$record.applied=$true;$record.savedBytes=$record.parentBytes-$record.candidateBytes
  $record.providerSha256=Hash-File (Join-Path $Root 'format-runtime.rel');$accepted=$true
 } catch {
  if($_.Exception.Message -match 'PAUSE_REQUESTED'){throw}
  $record.reason=$_.Exception.Message
  $record.diagnostic=$_.ScriptStackTrace
 } finally {
  if(-not $accepted -and $saved.Count){
   foreach($p in $saved){
    $backupFile=Join-Path (Join-Path $Root 'format-baseline') $p.name
    if((Hash-File $backupFile) -cne $p.sha256){throw 'format-baseline-drift'}
    [IO.File]::Copy($backupFile,(Join-Path $Root $p.name),$true)
   }
  }
 }
 return $record
}

. (Join-Path $PSScriptRoot 'ExtendedFormat.ps1')

. (Join-Path $PSScriptRoot 'IndirectFormat.ps1')
