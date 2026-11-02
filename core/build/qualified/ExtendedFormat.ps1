# 有限不可变格式的附加能力；公开变参 ABI 不变，槽宽由调用对象逐项核对。
function Get-Mx251ExtendedFormatPlan([string]$Format) {
 if($Format.Length -gt 32767 -or @($Format.ToCharArray()|Where-Object{[int]$_ -lt 1 -or [int]$_ -gt 255}).Count){throw 'format-outside-byte-bound'}
 $tokens=@();$maximum=0
 for($i=0;$i -lt $Format.Length;) {
  if($Format[$i] -ne '%'){$maximum++;$i++;continue}
  if($i+1 -lt $Format.Length -and $Format[$i+1] -eq '%'){$maximum++;$i+=2;continue}
  $m=[regex]::Match($Format.Substring($i),'^%(0?)([1-9][0-9]*)?([lb]?)([diuoxXc])')
  if(-not $m.Success){throw 'format-conversion-outside-fixed-integer-contract'}
  $width=if($m.Groups[2].Success){[int]$m.Groups[2].Value}else{0}
  if($width -gt 255){throw 'format-width-overflow'}
  $letter=$m.Groups[4].Value;$byte=$m.Groups[3].Value -ceq 'b';$bits=if($m.Groups[3].Value -ceq 'l'){32}else{16};$kind=if($letter -ceq 'c'){'character'}else{'number'}
  if($kind -ceq 'character' -and ($bits -ne 16 -or $byte -or $m.Groups[1].Value)){throw 'format-character-outside-contract'}
  $radix=if($kind -ceq 'character'){0}elseif($letter -ceq 'o'){8}elseif($letter -cin @('x','X')){16}else{10}
  $signed=$letter -cin @('d','i');$flags=if($signed){1}else{0};if($byte){$flags=$flags -bor 8}
  if($letter -ceq 'x'){$flags=$flags -bor 2};if($m.Groups[1].Value){$flags=$flags -bor 4}
  $digits=if($kind -ceq 'character'){1}elseif($byte){if($radix -eq 16){2}elseif($signed){4}else{3}}elseif($bits -eq 32){if($radix -eq 16){8}elseif($radix -eq 8 -or $signed){11}else{10}}else{if($radix -eq 8 -or $signed){6}elseif($radix -eq 16){4}else{5}}
  $maximum+=[Math]::Max($width,$digits)
  $tokens+=@{offset=$i;raw=$m.Value;radix=$radix;width=$width;flags=$flags;signed=$signed;kind=$kind;bits=$bits;byteConversion=$byte;argumentBytes=($bits/8)}
  $i+=$m.Length
 }
 if($maximum -gt 32767){throw 'format-output-count-overflow'}
 return @{format=$Format;tokens=$tokens;maximumOutputBytes=$maximum}
}
function Write-Mx251ExtendedFormatDecision($Tree,[int]$Depth=2) {
 $indent='    '*$Depth
 if($Tree.ContainsKey('leaf')){
  $t=$Tree.leaf
  return ($indent+'cursor+='+($t.raw.Length-1)+"u;`n"+$indent+'bits='+$t.bits+'u; radix='+$t.radix+'u; width='+$t.width+'u; flags='+$t.flags+"u;`n")
 }
 $source=''
 for($i=0;$i -lt $Tree.branches.Count;$i++){
  $b=$Tree.branches[$i];$head=if($i -eq $Tree.branches.Count-1){'else'}elseif($i -eq 0){'if'}else{'else if'}
  if($head -cne 'else'){$head+=' (cursor['+$Tree.offset+'] == '+$b.byte+'u)'}
  $source+=$indent+$head+" {`n"+(Write-Mx251ExtendedFormatDecision $b.tree ($Depth+1))+$indent+"}`n"
 }
 return $source
}
function New-Mx251ExtendedFormatSource($Demand,[string]$Kernel) {
 $result="#include <stdint.h>`n#include <stdarg.h>`n#include <stdio.h>`n"
 foreach($api in @('printf','sprintf')){
  $calls=@($Demand.calls|Where-Object{$_.api -ceq $api});if(-not $calls.Count){continue}
  $ops=@($calls|ForEach-Object{$_.plan.tokens}|ForEach-Object{$_});$buffer=$api -ceq 'sprintf';$functions=@{}
  foreach($bits in @(16,32)){
   $numeric=@($ops|Where-Object{$_.kind -ceq 'number' -and $_.bits -eq $bits});if(-not $numeric.Count){continue}
   $prefix='mx_'+$api+'_n'+$bits;$body=$Kernel.Substring($Kernel.IndexOf('static unsigned char mx_number')).Replace('mx_number',$prefix)
   if($bits -eq 32){
    # 相同逐位商/余数递推拓宽到 32 位。只生成 printf 私有转换；
    # 不修改、替换任何数学库函数，不让 16 位路径承担 32 位开销。
    $body=$body.Replace('uint16_t','uint32_t').Replace('digits[6]','digits[11]').Replace('0x8000u','0x80000000UL').Replace('bit=16','bit=32').Replace('value>>15','value>>31')
   }
   if($buffer){
    $body=$body.Replace(('uint'+$bits+'_t value,'),('char *destination, uint'+$bits+'_t value,'))
    $body=$body.Replace("putchar(' ')","*destination++=' '").Replace("putchar('-')","*destination++='-'").Replace("putchar('0')","*destination++='0'").Replace('putchar(digits[--n])','*destination++=(char)digits[--n]')
   }
   foreach($pair in @(@(1,'(flags & 1u)'),@(2,'(flags&2u)'),@(4,'(flags&4u)'))){
    $values=@($numeric|ForEach-Object{[int](($_.flags -band $pair[0]) -ne 0)}|Sort-Object -Unique)
    if($values.Count -eq 1){$body=$body.Replace($pair[1],('('+[string]$values[0]+')'))}
   }
   $extra=@();$constants=''
   foreach($field in @('radix','width','flags')){
    $values=@($numeric|ForEach-Object{$_[$field]}|Sort-Object -Unique)
    if($values.Count -eq 1){$body=[regex]::Replace($body,',\s*unsigned char '+$field+'\b','');$constants+='    const unsigned char '+$field+'='+$values[0]+"u;`n"}else{$extra+=@($field)}
   }
   if($constants){$first=$body.IndexOf('{')+1;$body=$body.Substring(0,$first)+"`n"+$constants+$body.Substring($first)}
   $result+=$body+"`n";$functions[$bits]=@{name=$prefix;extra=$extra}
  }
  $signature=if($buffer){'char *buffer, const char *format, ...'}else{'const char *format, ...'}
  $result+='int '+$api+'('+$signature+")`n{`n"
  if($buffer){$result+="    char *destination=buffer;`n"}
  $result+="    const unsigned char __code *cursor=(const unsigned char __code *)format;`n    unsigned char c, width, flags, radix, bits, emitted, pad;`n    uint16_t value16;`n    uint32_t value32;`n    int count=0;`n    va_list ap;`n    va_start(ap,format);`n    while ((c=*cursor++)!=0) {`n"
  $sink=if($buffer){'*destination++=(char)c;'}else{'putchar(c);'}
  $space=if($buffer){"*destination++=' ';"}else{"putchar(' ');"}
  if($ops.Count){
   $tree=New-Mx251FormatDecision $ops;$Demand['decisions_'+$api]=$tree
   $result+="        if(c!='%') { $sink ++count; continue; }`n        if(*cursor=='%') { ++cursor; $sink ++count; continue; }`n"
   $result+=Write-Mx251ExtendedFormatDecision $tree
   if(@($ops|Where-Object{$_.kind -ceq 'character'}).Count){
    # NUL 是一次真实字符输出，不能与格式串终止或缓冲终止混淆。
    $result+="        if(radix==0u) {`n            c=(unsigned char)va_arg(ap,int);`n            pad=width>1u?(unsigned char)(width-1u):0;`n            emitted=width?width:1u;`n            while(pad) { $space --pad; }`n            $sink count+=emitted; continue;`n        }`n"
   }
   foreach($bits in @(16,32)){
    if(-not $functions.ContainsKey($bits)){continue}
    $fn=$functions[$bits];$signed=if($bits -eq 32){'long'}else{'int'};$unsigned='unsigned '+$signed
    $result+='        if(bits=='+$bits+"u) {`n"+'            if(flags&1u) value'+$bits+'=(uint'+$bits+'_t)va_arg(ap,'+$signed+'); else value'+$bits+'=(uint'+$bits+'_t)va_arg(ap,'+$unsigned+");`n"
    # b 是输出值宽度；普通 C 变参仍按已核对的 2 字节提升槽取值。
    if($bits -eq 16 -and @($ops|Where-Object{$_.byteConversion}).Count){
     $result+="            if(flags&8u) { if(flags&1u) value16=(uint16_t)(int16_t)(int8_t)(uint8_t)value16; else value16=(uint8_t)value16; }`n"
    }
    $args=if($buffer){'destination,value'+$bits}else{'value'+$bits};foreach($field in $fn.extra){$args+=','+$field}
    $result+='            emitted='+$fn.name+'('+$args+");`n"
    if($buffer){$result+="            destination+=emitted;`n"}
    $result+="            count+=emitted;`n        }`n"
   }
  }else{$result+="        if(c=='%') ++cursor;`n        $sink ++count;`n"}
  $result+="    }`n";if($buffer){$result+="    *destination=0;`n"}
  $result+="    va_end(ap);`n    return count;`n}`n"
 }
 return $result
}
