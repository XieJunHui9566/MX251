# 有限不可变格式的附加能力；公开变参 ABI 不变，槽宽由调用对象逐项核对。
function Get-Mx251ExtendedFormatPlan([string]$Format) {
 if($Format.Length -gt 32767 -or @($Format.ToCharArray()|Where-Object{[int]$_ -lt 1 -or [int]$_ -gt 255}).Count){throw 'format-outside-byte-bound'}
 $tokens=@();$maximum=0
 for($i=0;$i -lt $Format.Length;) {
  if($Format[$i] -ne '%'){$maximum++;$i++;continue}
  if($i+1 -lt $Format.Length -and $Format[$i+1] -eq '%'){$maximum++;$i+=2;continue}
  $f=[regex]::Match($Format.Substring($i),'^%(0?)([1-9][0-9]*)?(?:\.([0-9]*))?([fF])')
  if($f.Success){
   $width=if($f.Groups[2].Success){[int]$f.Groups[2].Value}else{0}
   $precision=if($f.Groups[3].Success){if($f.Groups[3].Value){[int]$f.Groups[3].Value}else{0}}else{6}
   if($width -gt 255 -or $precision -gt 9){throw 'format-float-width-or-precision-outside-contract'}
   $flags=if($f.Groups[1].Value){4}else{0};if($f.Groups[4].Value -ceq 'F'){$flags=$flags -bor 2}
   $maximum+=[Math]::Max($width,40+$precision+[int]($precision -ne 0))
   $tokens+=@{offset=$i;raw=$f.Value;radix=1;width=$width;flags=$flags;signed=$true;kind='float';bits=32;precision=$precision;byteConversion=$false;argumentBytes=4}
   $i+=$f.Length;continue
  }
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
  $precision=if($t.kind -ceq 'float'){$t.precision}else{0}
  return ($indent+'cursor+='+($t.raw.Length-1)+"u;`n"+$indent+'bits='+$t.bits+'u; precision='+$precision+'u; radix='+$t.radix+'u; width='+$t.width+'u; flags='+$t.flags+"u;`n")
 }
 $source=''
 for($i=0;$i -lt $Tree.branches.Count;$i++){
  $b=$Tree.branches[$i];$head=if($i -eq $Tree.branches.Count-1){'else'}elseif($i -eq 0){'if'}else{'else if'}
  if($head -cne 'else'){$head+=' (cursor['+$Tree.offset+'] == '+$b.byte+'u)'}
  $source+=$indent+$head+" {`n"+(Write-Mx251ExtendedFormatDecision $b.tree ($Depth+1))+$indent+"}`n"
 }
 return $source
}
function New-Mx251HexFormatKernel($Tokens,[int]$Bits) {
 # The format contract already truncates b conversions to eight bits.
 # Emit high nibbles directly; no digit array or reverse traversal is needed.
 $actualBits=if($Bits -eq 16 -and @($Tokens|Where-Object{$_['byteConversion'] -eq $true}).Count -eq @($Tokens).Count){8}else{$Bits}
 $type='uint'+$actualBits+'_t';$digits=$actualBits/4;$high=$actualBits-4
 $fixed=-not @($Tokens|Where-Object{($_['flags'] -band 4) -eq 0 -or $_['width'] -lt $digits}).Count
 $scan=if($fixed){''}else{"    while(n>1u && (value>>$high)==0u) { value=($type)(value<<4); --n; }"}
 return @"
static unsigned char mx_number($type value, unsigned char radix,
    unsigned char width, unsigned char flags)
{
    unsigned char n=$($digits)u, c, total, pad;
$scan
    total=n;
    pad=width>total?(unsigned char)(width-total):0;
    total=(unsigned char)(total+pad);
    while(pad) {
        if (flags&4u) putchar('0'); else putchar(' ');
        --pad;
    }
    do {
        c=(unsigned char)(value>>$high);
        if(c>9u) c=(unsigned char)(c+((flags&2u)?('a'-10):('A'-10)));
        else c=(unsigned char)(c+'0');
        putchar(c);
        value=($type)(value<<4);
    } while(--n);
    return total;
}
"@
}
function New-Mx251IntegerFormatKernel([string]$Kernel,$Tokens,[int]$Bits=16,[switch]$NativeDivision) {
 $radices=@($Tokens|ForEach-Object{$_.radix}|Sort-Object -Unique)
 # A varying alphabet adds live state across each output call; the buffered
 # kernel is smaller for that case. Constant case uses the direct nibble walk.
 if($radices.Count -eq 1 -and $radices[0] -eq 16 -and
    @(Get-Mx251IntegerFormatFlagValues $Tokens 2).Count -le 1){return (New-Mx251HexFormatKernel $Tokens $Bits)}
 $body=$Kernel.Substring($Kernel.IndexOf('static unsigned char mx_number'))
 if($Bits -eq 32){
  $body=$body.Replace('uint16_t','uint32_t').Replace('digits[6]','digits[11]').Replace('0x8000u','0x80000000UL').Replace('bit=16','bit=32').Replace('value>>15','value>>31')
  if($NativeDivision){
   # The remainder is 0..9, so its low-byte identity is also the full result.
   # One runtime division suffices; no long multiply or second division.
   $division=@'
        else {
            unsigned char low=(unsigned char)value;
            uint32_t quotient=value/10UL;
            rem=(unsigned char)(low-(unsigned char)quotient*10u);
            value=quotient;
        }
'@
   $body=[regex]::Replace($body,'        else \{\s+rem=0; bit=32;.*?\} while \(--bit\);\s+\}', $division, [Text.RegularExpressions.RegexOptions]::Singleline)
  }
 }
 if(16 -notin $radices){$body=$body.Replace('radix==16u','0').Replace('if (rem>9u)','if (0)')}
 if(8 -notin $radices){$body=$body.Replace('radix==8u','0')}
 elseif(10 -notin $radices){$body=$body.Replace('radix==8u','1')}
 return $body
}
function Get-Mx251IntegerFormatFlagValues($Tokens,[int]$Flag) {
 # Letter case has no observable effect on decimal or octal digits.
 if($Flag -eq 2){$Tokens=@($Tokens|Where-Object{$_.radix -eq 16})}
 if(-not $Tokens.Count){return 0}
 return @($Tokens|ForEach-Object{[int](($_.flags -band $Flag) -ne 0)}|Sort-Object -Unique)
}
function Write-Mx251FormatEntry([string]$Api) {
 $buffer=$Api -ceq 'sprintf'
 $signature=if($buffer){'char *buffer, const char *format, ...'}else{'const char *format, ...'}
 $arguments=if($buffer){'buffer,'}else{''}
 # Only va_start needs a variadic entry. The format walker can use the wide
 # backend with an explicit va_list, while the public ABI remains unchanged.
 return @"
int $Api($signature)
{
    va_list ap;
    int count;
    va_start(ap,format);
    count=mx_${Api}_body(${arguments}(const unsigned char __code *)format,ap);
    va_end(ap);
    return count;
}

"@
}
function New-Mx251ExtendedFormatSource($Demand,[string]$Kernel) {
 $result="#include <stdint.h>`n#include <stdarg.h>`n#include <stdio.h>`n"
 foreach($api in @('printf','sprintf')){
  $calls=@($Demand.calls|Where-Object{$_.api -ceq $api});if(-not $calls.Count){continue}
  $ops=@($calls|ForEach-Object{$_.plan.tokens}|ForEach-Object{$_});$buffer=$api -ceq 'sprintf';$functions=@{}
  foreach($bits in @(16,32)){
   $numeric=@($ops|Where-Object{$_.kind -ceq 'number' -and $_.bits -eq $bits});if(-not $numeric.Count){continue}
   $prefix='mx_'+$api+'_n'+$bits
   $body=(New-Mx251IntegerFormatKernel $Kernel $numeric $bits -NativeDivision:([bool]$Demand.nativeIntegerDivision)).Replace('mx_number',$prefix)
   if($buffer){
    $body=[regex]::Replace($body,'(uint(?:8|16|32)_t value,)','char *destination, $1')
    $body=$body.Replace("putchar(' ')","*destination++=' '").Replace("putchar('-')","*destination++='-'").Replace("putchar('0')","*destination++='0'").Replace('putchar(digits[--n])','*destination++=(char)digits[--n]').Replace('putchar(c)','*destination++=(char)c')
   }
   foreach($pair in @(@(1,'(flags & 1u)'),@(2,'(flags&2u)'),@(4,'(flags&4u)'))){
    $values=@(Get-Mx251IntegerFormatFlagValues $numeric $pair[0])
    if($values.Count -eq 1){$body=$body.Replace($pair[1],('('+[string]$values[0]+')'))}
   }
   $extra=@();$constants=''
   foreach($field in @('radix','width','flags')){
    $values=@($numeric|ForEach-Object{$_[$field]}|Sort-Object -Unique)
    if($field -ceq 'flags' -and [regex]::Matches($body,'\bflags\b').Count -eq 1){$values=@(0)}
    if($values.Count -eq 1){$body=[regex]::Replace($body,',\s*unsigned char '+$field+'\b','');$constants+='    const unsigned char '+$field+'='+$values[0]+"u;`n"}else{$extra+=@($field)}
   }
   if($constants){$first=$body.IndexOf('{')+1;$body=$body.Substring(0,$first)+"`n"+$constants+$body.Substring($first)}
   $result+=$body+"`n";$functions[$bits]=@{name=$prefix;extra=$extra}
  }
  $floating=@($ops|Where-Object{$_.kind -ceq 'float'})
  $floatArgs=@()
  if($floating.Count){
   $body=[IO.File]::ReadAllText((Join-Path $PSScriptRoot 'FloatFormatKernel.c.template')).Replace('mx_float',('mx_'+$api+'_float'))
   if($buffer){
    $body=$body.Replace('uint32_t raw,','char *destination, uint32_t raw,')
    $body=$body.Replace("putchar(' ')","*destination++=' '").Replace("putchar('-')","*destination++='-'").Replace("putchar('0')","*destination++='0'").Replace("putchar('.')","*destination++='.'").Replace('putchar(digits[--n])','*destination++=(char)digits[--n]')
   }
   $constants=''
   foreach($field in @('precision','width','flags')){
    $values=@($floating|ForEach-Object{$_[$field]}|Sort-Object -Unique)
    if($values.Count -eq 1){$body=[regex]::Replace($body,',\s*unsigned char '+$field+'\b','');$constants+='    unsigned char '+$field+'='+$values[0]+"u;`n"}else{$floatArgs+=@($field)}
   }
   $first=$body.IndexOf('{')+1;$body=$body.Substring(0,$first)+"`n"+$constants+$body.Substring($first)
   $result+=$body+"`n"
  }
  $signature=if($buffer){'char *buffer, const unsigned char __code *cursor, va_list ap'}else{'const unsigned char __code *cursor, va_list ap'}
  $result+='static int mx_'+$api+'_body('+$signature+")`n{`n"
  if($buffer){$result+="    char *destination=buffer;`n"}
  $result+="    unsigned char c, width, flags, radix, bits, emitted, pad, precision;`n    uint16_t value16;`n    uint32_t value32;`n    union { float f; uint32_t u; } floating;`n    int count=0;`n    while ((c=*cursor++)!=0) {`n"
  $sink=if($buffer){'*destination++=(char)c;'}else{'putchar(c);'}
  $space=if($buffer){"*destination++=' ';"}else{"putchar(' ');"}
  if($ops.Count){
   $tree=New-Mx251FormatDecision $ops;$Demand['decisions_'+$api]=$tree
   $result+="        if(c!='%') { $sink ++count; continue; }`n        if(*cursor=='%') { ++cursor; $sink ++count; continue; }`n"
   $result+=Write-Mx251ExtendedFormatDecision $tree
   if($floating.Count){
    $args=if($buffer){'destination,floating.u'}else{'floating.u'};foreach($field in $floatArgs){$args+=','+$field}
    $result+="        if(radix==1u) {`n            floating.f=(float)va_arg(ap,double);`n            emitted=mx_"+$api+'_float('+$args+");`n"
    if($buffer){$result+="            destination+=emitted;`n"}
    $result+="            count+=emitted; continue;`n        }`n"
   }
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
  $result+="    return count;`n}`n"+(Write-Mx251FormatEntry $api)
 }
 return $result
}
