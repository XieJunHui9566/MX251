#requires -Version 7.0
# Conservative whole-function collection. Firmware is always reassembled by the caller.
# No SDK source rewriting, object surgery, linker invocation, or hardware access.

function Get-Mx251GcField($Object,[string[]]$Names,$Default=$null) {
 foreach($name in $Names) {
  if($Object -is [Collections.IDictionary]) { if($Object.Contains($name)){return $Object[$name]} }
  elseif($null -ne $Object.PSObject.Properties[$name]) {return $Object.$name}
 }
 return $Default
}
function Get-Mx251GcHash([string]$Path) {return (Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Get-Mx251GcText([string]$Line) {
 $quoted=$false;$quote=[char]0;$escape=$false;$out=[Text.StringBuilder]::new()
 foreach($c in $Line.ToCharArray()) {
  if($quoted) {
   if($escape){$escape=$false;continue}
   if($c -eq '\'){$escape=$true;continue}
   if($c -eq $quote){$quoted=$false}
   continue
  }
  if($c -eq ';'){break}
  if($c -eq '"' -or $c -eq "'"){$quoted=$true;$quote=$c;continue}
  $null=$out.Append($c)
 }
 if($quoted){throw 'Unterminated assembly string'}
 return $out.ToString().Trim()
}
function Get-Mx251GcTokens([string]$Text) {
 # Identifiers must not start inside a hexadecimal numeric literal.
 return @([regex]::Matches($Text,'(?<![A-Za-z0-9_.$])(?:[A-Za-z_.$][A-Za-z0-9_.$]*|[0-9]+\$)(?![A-Za-z0-9_.$])')|ForEach-Object{$_.Value})
}
function Get-Mx251GcBitOperands([string]$Instruction,[string]$Operand) {
 # Only bit-operand positions may use these assembler builtins. Keeping the
 # original token stream lets user definitions and REL references win below.
 # Source of the bounded names: sdas/as251/mcs251pst.c preDef; numeric grammar
 # and the 0..7 bound: mcs251mch.c parse_bit and mcs251adr.c S: handling.
 $parts=@($Operand.Split(',')|ForEach-Object{$_.Trim()})
 $positions=@();$allowInverted=$false
 if($Instruction -in @('jb','jnb','jbc') -and $parts.Count -eq 2){$positions=@(0)}
 elseif($Instruction -in @('clr','cpl','setb') -and $parts.Count -eq 1){$positions=@(0)}
 elseif($Instruction -in @('anl','orl') -and $parts.Count -eq 2 -and $parts[0] -match '^(c|cy)$'){$positions=@(1);$allowInverted=$true}
 elseif($Instruction -eq 'mov' -and $parts.Count -eq 2){
  if($parts[0] -match '^(c|cy)$'){$positions=@(1)}
  elseif($parts[1] -match '^(c|cy)$'){$positions=@(0)}
 }
 $known=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 $invalid=[Collections.Generic.List[string]]::new()
 foreach($position in $positions){
  $bitText=$parts[$position]
  if($bitText.StartsWith('/')){if(-not $allowInverted){$invalid.Add($bitText);continue};$bitText=$bitText.Substring(1)}
  if($bitText -match '^(?:(?:acc|a|b|p[0-3]|psw|scon|t2con|tcon)\.[0-7]|ie\.[0-57]|ip\.[0-5])$'){
   $null=$known.Add($bitText);continue
  }
  if($bitText -match '^(?<sfr>s:)?(?<base>0x[0-9a-f]+|[0-9]+)(?:\.(?<bit>[0-9]+))?$'){
   $number=$Matches.Clone();$valid=$true;$address=[uint64]0
   try{if($number.base.StartsWith('0x',[StringComparison]::OrdinalIgnoreCase)){$address=[Convert]::ToUInt64($number.base.Substring(2),16)}else{$address=[Convert]::ToUInt64($number.base,10)}}catch{$valid=$false}
   if($address -gt 255 -or ($number['sfr'] -and $address -lt 128)){$valid=$false}
   if($number['bit'] -and ($number['bit'].Length -gt 1 -or [int]$number['bit'] -gt 7)){$valid=$false}
   if(-not $valid){$invalid.Add($bitText)}elseif($number['sfr']){$null=$known.Add('s')}
  }
 }
 return @{knownTokens=$known;invalidConstants=@($invalid.ToArray())}
}
function Read-Mx251GcRel([byte[]]$Bytes,[string]$Label) {
 $text=[Text.Encoding]::ASCII.GetString($Bytes)
 $lines=$text -split '\r?\n';if($lines[0] -cne 'XH3'){throw "Unsupported REL format: $Label"}
 $defs=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 $refs=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 $symbols=[Collections.Generic.List[object]]::new();$areas=0;$header=$null;$pending=$false
 foreach($line in $lines|Select-Object -Skip 1) {
  if(-not $line -or $line.StartsWith(';')){continue}
  switch -Regex ($line) {
   '^H ([0-9A-F]+) areas ([0-9A-F]+) global symbols$' {if($header){throw "Duplicate REL header: $Label"};$header=@([Convert]::ToInt32($Matches[1],16),[Convert]::ToInt32($Matches[2],16));continue}
   '^S (\S+) (Def|Ref)[0-9A-F]+$' {$name=$Matches[1];$binding=$Matches[2];$symbols.Add(@{name=$name;binding=$binding});if($binding -eq 'Def'){$null=$defs.Add($name)}else{$null=$refs.Add($name)};continue}
   '^A \S+ size [0-9A-F]+ flags [0-9A-F]+ addr [0-9A-F]+$' {$areas++;continue}
   '^T(?: [0-9A-F]{2}){3,}$' {if($pending){throw "Unpaired REL text: $Label"};$pending=$true;continue}
   '^R(?: [0-9A-F]{2}){4,}$' {
    if(-not $pending){throw "REL relocation without text: $Label"};$pending=$false
    $r=@($line.Substring(2).Split(' ')|ForEach-Object{[Convert]::ToInt32($_,16)})
    if($r[0] -ne 0 -or $r[1] -ne 0 -or (($r[2]*256+$r[3]) -ge $areas)){throw "REL area index: $Label"}
    for($i=4;$i -lt $r.Count;) {
     $mode=$r[$i++];if(($mode -band 0xf0) -eq 0xf0){if($i -ge $r.Count){throw "REL extended mode: $Label"};$mode=(($mode -band 15)*256)+$r[$i++]}
     if($i+2 -ge $r.Count){throw "REL truncated relocation: $Label"}
     $i++;$index=$r[$i++]*256+$r[$i++]
     if(($mode -band 2) -and $index -ge $symbols.Count){throw "REL symbol index: $Label"}
     if(-not($mode -band 2) -and $index -ge $areas){throw "REL area target: $Label"}
    };continue
   }
   '^[MO] ' {continue}
   default {throw "Unsupported REL record: $Label : $line"}
  }
 }
 if($pending -or -not $header -or $header[0] -ne $areas -or $header[1] -ne $symbols.Count){throw "Incomplete REL: $Label"}
 return @{Definitions=$defs;References=$refs}
}
function Read-Mx251GcArchive([string]$Path) {
 $bytes=[IO.File]::ReadAllBytes($Path)
 if($bytes.Length -lt 8 -or [Text.Encoding]::ASCII.GetString($bytes,0,8) -cne "!<arch>`n"){throw "Unsupported archive format: $Path"}
 $out=[Collections.Generic.List[object]]::new();$pos=8;$longNames=$null
 while($pos -lt $bytes.Length) {
  if($pos+60 -gt $bytes.Length){throw "Truncated archive header: $Path"}
  $h=[Text.Encoding]::ASCII.GetString($bytes,$pos,60)
  if($h.Substring(58,2) -cne ('`'+"`n")){throw "Archive header trailer: $Path"}
  $size=0;if(-not [int]::TryParse($h.Substring(48,10).Trim(),[ref]$size) -or $size -lt 0 -or $pos+60+$size -gt $bytes.Length){throw "Archive member size: $Path"}
  $name=$h.Substring(0,16).Trim();$start=$pos+60;$length=$size
  if($name -eq '//'){$longNames=[Text.Encoding]::ASCII.GetString($bytes,$start,$length)}
  elseif($name -ne '/' -and $name -ne '/SYM64/') {
   if($name.StartsWith('#1/')) {$n=[int]$name.Substring(3);if($n -gt $length){throw "Archive BSD name: $Path"};$name=[Text.Encoding]::ASCII.GetString($bytes,$start,$n).TrimEnd([char]0);$start+=$n;$length-=$n}
   elseif($name -match '^/([0-9]+)$') {if($null -eq $longNames){throw "Archive long name table missing: $Path"};$offset=[int]$Matches[1];if($offset -ge $longNames.Length){throw "Archive name offset: $Path"};$name=$longNames.Substring($offset).Split("`n")[0].TrimEnd('/')}
   else {$name=$name.TrimEnd('/')}
   if($name -notin @('__.SYMDEF','__.SYMDEF SORTED')) {
    $data=[byte[]]::new($length);[Array]::Copy($bytes,$start,$data,0,$length)
    $rel=Read-Mx251GcRel $data "$Path!$name";$out.Add(@{name=$name;rel=$rel})
   }
  }
  $pos+=60+$size+($size%2)
 }
 return @($out.ToArray())
}

function Invoke-Mx251FunctionGc {
 [CmdletBinding()]
 param([Parameter(Mandatory)][string]$Root,[Parameter(Mandatory)][object[]]$Units,
       [string[]]$ExtraAssembly=@(),[string[]]$LibraryPaths=@(),[string[]]$KeepSymbols=@())
 $ErrorActionPreference='Stop'
 $issues=[Collections.Generic.List[object]]::new();$functions=[Collections.Generic.List[object]]::new()
 $documents=[Collections.Generic.List[object]]::new();$libraries=[Collections.Generic.List[object]]::new()
 $nodes=[Collections.Hashtable]::new([StringComparer]::Ordinal);$defs=[Collections.Hashtable]::new([StringComparer]::Ordinal);$globalDefs=[Collections.Hashtable]::new([StringComparer]::Ordinal);$globalFunctions=[Collections.Hashtable]::new([StringComparer]::Ordinal);$unitNames=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 $librarySymbols=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 $libraryRefs=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 $allSymbols=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 $knownWords=[Collections.Generic.HashSet[string]]::new([StringComparer]::OrdinalIgnoreCase)
 foreach($x in ('a acc ab b c cy ov ac f0 f1 psw psw1 sp spx sph spxl dptr dpx dpl dph dpxl pc ea'.Split(' '))){$null=$knownWords.Add($x)}
 foreach($prefix in @('r','wr','dr','ar','b')){0..63|ForEach-Object{$null=$knownWords.Add("$prefix$_")}}
 $instructions=@('mov','movx','movc','movz','movs','lea','push','pop','pushw','popw','add','addc','sub','subb','cmp','mul','div','inc','dec','anl','orl','xrl','clr','cpl','setb','rl','rr','rlc','rrc','swap','da','xch','xchd','nop','sjmp','ajmp','ljmp','ejmp','jmp','acall','lcall','ecall','call','ret','eret','reti','jz','jnz','jc','jnc','jb','jnb','jbc','je','jne','jsg','jsge','jsl','jsle','jg','jge','jl','jle','jgt','jgte','jlt','jlte','jnv','jnov','jv','jo','jno','djnz','cjne','sll','srl','sra')
 $ignoreDirectives=@('.module','.optsdcc','.area','.list','.nlist','.nolist','.page','.title','.sbttl','.radix','.end','.even','.odd','.bndry')
 $dataDirectives=@('.db','.dw','.ds','.byte','.word','.ascii','.asciz','.str','.strz','.org','.equ','.gblequ','.lclequ','.float','.3byte','.4byte','.d24','.d32')
 $rootPath=[IO.Path]::GetFullPath($Root)
 if(-not [IO.Directory]::Exists($rootPath)){throw 'GC Root must already exist'}
 $outPath=Join-Path $rootPath 'function-gc'
 if(Test-Path -LiteralPath $outPath){throw "GC output already exists; refusing to overwrite evidence: $outPath"}
 $addIssue={param($Code,$Detail) $issues.Add(@{code=$Code;detail=[string]$Detail})}
 # Parse every archive, not just the current linker-selected subset. Any application
 # symbol referenced by any member becomes a root in this deliberately conservative v1.
 foreach($library in $LibraryPaths) {
  try {
   $path=[IO.Path]::GetFullPath($library);$members=@(Read-Mx251GcArchive $path)
   foreach($m in $members){foreach($n in $m.rel.Definitions){$null=$librarySymbols.Add($n)};foreach($n in $m.rel.References){$null=$libraryRefs.Add($n)}}
   $libraries.Add(@{path=$path;sha256=(Get-Mx251GcHash $path);members=$members.Count})
  } catch {&$addIssue 'ARCHIVE_UNPROVEN' $_.Exception.Message}
 }
 $inputs=[Collections.Generic.List[object]]::new()
 foreach($unit in $Units){$inputs.Add(@{unit=$unit;extra=$false})}
 $extraIndex=0;foreach($path in $ExtraAssembly){$inputs.Add(@{unit=@{name="extra$extraIndex";assembly=$path;language='asm'};extra=$true});$extraIndex++}
 foreach($gcInput in $inputs) {
  $u=$gcInput.unit;$name=[string](Get-Mx251GcField $u @('name','Name'))
  if($name -notmatch '^[A-Za-z_][A-Za-z0-9_-]*$' -or -not $unitNames.Add($name)){throw "Invalid/duplicate GC unit name: $name"}
  $path=[string](Get-Mx251GcField $u @('assembly','asm','Assembly'))
  if(-not $path -or -not [IO.Path]::IsPathRooted($path)){throw "GC assembly path must be absolute: $name"}
  $path=[IO.Path]::GetFullPath($path);$lines=[IO.File]::ReadAllLines($path)
  $relPath=[string](Get-Mx251GcField $u @('object','rel','Object'))
  $language=[string](Get-Mx251GcField $u @('language','Language') 'c')
  if($language -notin @('c','asm')){throw "Unknown unit language: $name"}
  $owner="root:$name";$nodes[$owner]=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  $doc=@{name=$name;path=$path;sha256=(Get-Mx251GcHash $path);lines=$lines;lineOwners=[string[]]::new($lines.Length);records=[Collections.Generic.List[object]]::new();functions=[Collections.Generic.List[object]]::new();declared=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal);rel=$null;object=$relPath;objectSha256=$null;language=$language;extra=$gcInput.extra;root=$owner}
  if($relPath){try{$relPath=[IO.Path]::GetFullPath($relPath);$doc.object=$relPath;$doc.rel=Read-Mx251GcRel ([IO.File]::ReadAllBytes($relPath)) $relPath;$doc.objectSha256=Get-Mx251GcHash $relPath}catch{&$addIssue 'OBJECT_UNPROVEN' $_.Exception.Message}}
  elseif($language -eq 'c'){&$addIssue 'OBJECT_MISSING' $name}
  $active=$null;$area='';$markers=0;$codeOutside=$false
  for($i=0;$i -lt $lines.Length;$i++) {
   $raw=$lines[$i]
   if($raw -match '^\s*; mx251-function begin schema=1 symbol=(\S+) isr=([01]) addressTaken=([01]) opaque=([01]) indirect=([01])\s*$') {
    if($active){&$addIssue 'NESTED_FUNCTION_MARKER' "${name}:$($i+1)";continue}
    $metadata=$Matches.Clone();$sym=$metadata[1];if($sym -notmatch '^[A-Za-z_.$][A-Za-z0-9_.$]*$'){&$addIssue 'BAD_FUNCTION_SYMBOL' $sym;continue}
    $id="fn:$name`:$sym";if($nodes.ContainsKey($id)){&$addIssue 'DUPLICATE_FUNCTION_MARKER' $id;continue}
    $active=@{id=$id;symbol=$sym;unit=$name;start=$i;end=$null;isr=$metadata[2] -eq '1';addressTaken=$metadata[3] -eq '1';opaque=$metadata[4] -eq '1';indirect=$metadata[5] -eq '1';labels=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal);beginArea=$area}
    $nodes[$id]=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal);$functions.Add($active);$doc.functions.Add($active);$owner=$id;$markers++
    if($active.opaque){&$addIssue 'OPAQUE_FUNCTION' $id};if($active.indirect){&$addIssue 'INDIRECT_CALL' $id}
    if(-not $globalFunctions.ContainsKey($sym)){$globalFunctions[$sym]=[Collections.Generic.List[object]]::new()};$globalFunctions[$sym].Add($active)
    $doc.lineOwners[$i]=$owner;continue
   }
   if($raw -match '^\s*; mx251-function end schema=1 symbol=(\S+)\s*$') {
    if(-not $active -or $active.symbol -cne $Matches[1]){&$addIssue 'MISMATCHED_FUNCTION_END' "${name}:$($i+1)"}
    else {$active.end=$i;if($area -cne $active.beginArea){&$addIssue 'FUNCTION_AREA_STATE_CHANGED' $active.id}}
    $doc.lineOwners[$i]=$owner;$active=$null;$owner=$doc.root;continue
   }
   if($raw -match ';\s*mx251-function\s+(begin|end)'){&$addIssue 'UNKNOWN_FUNCTION_METADATA' "${name}:$($i+1)"}
   $doc.lineOwners[$i]=$owner
   try {$text=Get-Mx251GcText $raw}catch{&$addIssue 'ASSEMBLY_TEXT_UNPROVEN' "${name}:$($i+1)";continue}
   if(-not $text){continue}
   $def=$null;$rhs=$null;$equate=$false
   if($text -match '^([A-Za-z_.$][A-Za-z0-9_.$]*|[0-9]+\$)\s*:{1,2}\s*(.*)$'){$def=$Matches[1];$rhs=$Matches[2]}
   elseif($text -match '^([A-Za-z_.$][A-Za-z0-9_.$]*)\s*(?:=|==|\bequ\b)\s*(.*)$'){$def=$Matches[1];$rhs=$Matches[2];$equate=$true}
   if($def) {
    $key="$name`:$def";if(-not $defs.ContainsKey($key)){$defs[$key]=[Collections.Generic.List[object]]::new()}
    $definition=@{name=$def;owner=$owner;equate=$equate;rhs=$rhs;line=$i+1}
    $defs[$key].Add($definition)
    if($def -notmatch '^[0-9]+\$$') {if(-not $globalDefs.ContainsKey($def)){$globalDefs[$def]=[Collections.Generic.List[object]]::new()};$globalDefs[$def].Add($definition)}
    $null=$allSymbols.Add($def);if($active){$null=$active.labels.Add($def)}
    if($equate){$doc.records.Add(@{owner=$owner;line=$i;kind='alias';expression=$rhs;tokens=@(Get-Mx251GcTokens $rhs)});continue}
    $text=$rhs;if(-not $text){continue}
   }
   if($text -match '^\.area\s+(\S+)'){$area=$Matches[1];if($active){&$addIssue 'AREA_DIRECTIVE_INSIDE_FUNCTION' $active.id};continue}
   if($text -match '^(\.\S+)(?:\s+(.*))?$') {
    $directive=$Matches[1].ToLowerInvariant();$operand=if($Matches.ContainsKey(2)){$Matches[2]}else{''}
    if($directive -eq '.globl'){$tokens=@(Get-Mx251GcTokens $operand);foreach($token in $tokens){$null=$doc.declared.Add($token)};$doc.records.Add(@{owner=$owner;line=$i;kind='declaration';expression='';tokens=$tokens});continue}
    if($directive -in $ignoreDirectives){continue}
    if($directive -in $dataDirectives){$doc.records.Add(@{owner=$owner;line=$i;kind='data';expression=$operand;tokens=@(Get-Mx251GcTokens $operand)});continue}
    &$addIssue 'UNKNOWN_ASSEMBLY_DIRECTIVE' "${name}:$($i+1) $directive";continue
   }
   if($text -match '^(\S+)(?:\s+(.*))?$') {
    $instruction=$Matches[1].ToLowerInvariant();$operand=if($Matches.ContainsKey(2)){$Matches[2]}else{''}
    if($instruction -notin $instructions){&$addIssue 'UNKNOWN_INSTRUCTION' "${name}:$($i+1) $instruction"}
    if($language -eq 'c' -and -not $active -and $area -match '^(CSEG|_CODE)$'){$codeOutside=$true}
    if($instruction -in @('ljmp','ejmp','ajmp','sjmp','lcall','ecall','acall','call') -and @(Get-Mx251GcTokens $operand).Count -eq 0){&$addIssue 'ABSOLUTE_CONTROL_TARGET' "${name}:$($i+1) $operand"}
    if($instruction -eq 'jmp' -and $operand -match '@' -and (-not $active -or $active.opaque -or $language -ne 'c')){&$addIssue 'OPAQUE_INDIRECT_JUMP' "${name}:$($i+1)"}
    # 原生有符号分支来自 as251 的四个 S_BR 指令；目标须是可证明的
    # 元数据函数代码标签。数字、算术、别名或外部未知位置继续保留全体。
    $signedBranch=$instruction -in @('jsg','jsge','jsl','jsle')
    if($signedBranch -and $operand -cnotmatch '^(?:[A-Za-z_][A-Za-z0-9_.$]*|[0-9]+\$)$'){&$addIssue 'SIGNED_BRANCH_TARGET_UNPROVEN' "${name}:$($i+1) $operand"}
    $bitOperands=Get-Mx251GcBitOperands $instruction $operand
    foreach($invalidBit in $bitOperands.invalidConstants){&$addIssue 'BIT_OPERAND_UNPROVEN' "${name}:$($i+1) $invalidBit"}
    $doc.records.Add(@{owner=$owner;line=$i;kind='instruction';expression=$operand;tokens=@(Get-Mx251GcTokens $operand);bitBuiltinTokens=$bitOperands.knownTokens;signedBranch=$signedBranch})
   }
  }
  if($active){&$addIssue 'UNCLOSED_FUNCTION' $active.id}
  if($language -eq 'c' -and $codeOutside){&$addIssue 'UNOWNED_C_CODE' $name}
  if($language -eq 'c' -and $markers -eq 0 -and $codeOutside){&$addIssue 'FUNCTION_METADATA_MISSING' $name}
  foreach($f in $doc.functions){if(-not $f.labels.Contains($f.symbol)){&$addIssue 'FUNCTION_ENTRY_MISSING' $f.id}}
  $documents.Add($doc)
 }
 # Resolve textual names to function owners. Unit-local labels take precedence;
 # global declarations alone do not make all exported application functions roots.
 foreach($doc in $documents) {
  $observed=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
  foreach($record in $doc.records) {
   foreach($token in $record.tokens) {
    $null=$observed.Add($token)
    # Generated C exports alone are not roots. Handwritten assembly is retained
    # wholesale: its application declarations conservatively retain their owners.
    if($record.kind -eq 'declaration' -and $doc.language -eq 'c'){continue}
    $key="$($doc.name):$token";$targets=@();$codeLabelTarget=$false
    if($defs.ContainsKey($key)) {
     $local=@($defs[$key]|Where-Object{$_.owner -ceq $record.owner})
     if(-not $local.Count){$local=@($defs[$key])}
     $owners=@($local.owner|Select-Object -Unique)
     if($owners.Count -gt 1 -and -not (@($local|Where-Object{-not $_.equate}).Count -eq 0 -and @($local.rhs|Select-Object -Unique).Count -eq 1)) {&$addIssue 'AMBIGUOUS_LOCAL_SYMBOL' $key}
     $targets=@($owners|Where-Object{$_ -like 'fn:*'})
     $codeLabelTarget=$owners.Count -eq 1 -and $targets.Count -eq 1 -and @($local|Where-Object{$_.equate}).Count -eq 0
    } elseif($globalFunctions.ContainsKey($token)) {
     $candidates=@($globalFunctions[$token]);if($candidates.Count -ne 1){&$addIssue 'AMBIGUOUS_FUNCTION_SYMBOL' $token}
     $targets=@($candidates.id);$codeLabelTarget=$candidates.Count -eq 1
    } elseif($globalDefs.ContainsKey($token) -and -not $knownWords.Contains($token)) {
     # A globally visible entry/alias inside a function may differ from the
     # metadata entry symbol (e.g. an outlined suffix called from another TU).
     $candidates=@($globalDefs[$token]);$owners=@($candidates.owner|Select-Object -Unique)
     if($owners.Count -gt 1 -and -not (@($candidates|Where-Object{-not $_.equate}).Count -eq 0 -and @($candidates.rhs|Select-Object -Unique).Count -eq 1)){&$addIssue 'AMBIGUOUS_GLOBAL_SYMBOL' $token}
     $targets=@($owners|Where-Object{$_ -like 'fn:*'})
     $codeLabelTarget=$owners.Count -eq 1 -and $targets.Count -eq 1 -and @($candidates|Where-Object{$_.equate}).Count -eq 0
    } elseif(($record.kind -eq 'instruction' -and $record.bitBuiltinTokens.Contains($token)) -or $knownWords.Contains($token) -or $librarySymbols.Contains($token) -or $token -eq '$' -or $token -eq '.') {
     # Known register, data object, library entry, or local assembly position.
    } elseif($doc.rel -and ($doc.rel.References.Contains($token) -or $doc.rel.Definitions.Contains($token))) {
     # External non-application symbols remain for the unchanged linker to resolve.
    } elseif($doc.language -eq 'asm' -and $doc.declared.Contains($token)) {
     # Explicit non-application imports (including linker-defined segment sizes)
     # are left to the normal linker; all application definitions were handled above.
    } else {&$addIssue 'UNKNOWN_SYMBOL_REFERENCE' "$($doc.name):$($record.line+1) $token"}
    if($record.kind -eq 'instruction' -and $record.signedBranch -and -not $codeLabelTarget){&$addIssue 'SIGNED_BRANCH_TARGET_UNPROVEN' "$($doc.name):$($record.line+1) $token"}
    foreach($target in $targets){
     if($target -cne $record.owner -and $record.expression -match '[+-]'){&$addIssue 'FUNCTION_ADDRESS_ARITHMETIC' "$($doc.name):$($record.line+1) $($record.expression)"}
     $null=$nodes[$record.owner].Add($target)
    }
   }
  }
  if($doc.rel){foreach($ref in $doc.rel.References){if(-not $observed.Contains($ref)){&$addIssue 'REL_REFERENCE_NOT_EXPLAINED_BY_ASM' "$($doc.name):$ref"}}}
 }
 $roots=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 foreach($doc in $documents){$null=$roots.Add($doc.root)}
 foreach($f in $functions){if($f.isr -or $f.addressTaken){$null=$roots.Add($f.id)}}
 foreach($symbol in @($KeepSymbols)+@($libraryRefs)) {
  if($globalFunctions.ContainsKey($symbol)){foreach($f in $globalFunctions[$symbol]){$null=$roots.Add($f.id)}}
  elseif($globalDefs.ContainsKey($symbol)){foreach($d in $globalDefs[$symbol]){if($d.owner -like 'fn:*'){$null=$roots.Add($d.owner)}}}
  elseif($symbol -cin $KeepSymbols -and -not $allSymbols.Contains($symbol) -and -not $librarySymbols.Contains($symbol)){&$addIssue 'KEEP_SYMBOL_NOT_RESOLVED' $symbol}
 }
 $live=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal);$queue=[Collections.Generic.Queue[string]]::new()
 foreach($rootNode in $roots){$queue.Enqueue($rootNode)}
 while($queue.Count){$node=$queue.Dequeue();if(-not $live.Add($node)){continue};foreach($edge in $nodes[$node]){$queue.Enqueue($edge)}}
 $fallback=$issues.Count -gt 0
 if($fallback){foreach($f in $functions){$null=$live.Add($f.id)}}
 $removed=@($functions|Where-Object{-not $live.Contains($_.id)})
 $removedSymbols=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 foreach($f in $removed){foreach($label in $f.labels){if($label -notmatch '^[0-9]+\$$' -and -not $knownWords.Contains($label)){
  # Never prune an export also defined by retained code in another unit.
  $retainedDefinition=@($globalDefs[$label]|Where-Object{$live.Contains($_.owner)})
  if(-not $retainedDefinition.Count){$null=$removedSymbols.Add($label)}
 }}}
 $null=[IO.Directory]::CreateDirectory((Join-Path $outPath 'original'))
 $null=[IO.Directory]::CreateDirectory((Join-Path $outPath 'candidate'))
 $changed=[Collections.Generic.List[object]]::new();$artifactRows=[Collections.Generic.List[object]]::new()
 foreach($doc in $documents) {
  if($doc.extra){continue}
  $original=Join-Path $outPath ('original/'+$doc.name+'.asm');$candidate=Join-Path $outPath ('candidate/'+$doc.name+'.asm')
  [IO.File]::Copy($doc.path,$original,$false)
  $drop=[Collections.Generic.HashSet[int]]::new()
  foreach($f in $doc.functions){if(-not $live.Contains($f.id)){for($i=$f.start;$i -le $f.end;$i++){$null=$drop.Add($i)}}}
  $out=[Collections.Generic.List[string]]::new();$declarationsRemoved=[Collections.Generic.List[string]]::new()
  for($i=0;$i -lt $doc.lines.Length;$i++) {
   if($drop.Contains($i)){continue}
   $line=$doc.lines[$i]
   if(-not $fallback -and $line -match '^\s*\.globl\s+([^;]+)(?:;.*)?$') {
    $names=@(Get-Mx251GcTokens $Matches[1]);$kept=@($names|Where-Object{-not $removedSymbols.Contains($_)})
    foreach($n in $names){if($removedSymbols.Contains($n)){$declarationsRemoved.Add($n)}}
    if($kept.Count -ne $names.Count){if(-not $kept.Count){continue};$line="`t.globl "+($kept -join ',')}
   }
   $out.Add($line)
  }
  $wasChanged=$drop.Count -gt 0 -or $declarationsRemoved.Count -gt 0
  if($wasChanged){[IO.File]::WriteAllText($candidate,($out -join "`n")+"`n",[Text.UTF8Encoding]::new($false));$changed.Add(@{name=$doc.name;originalAssembly=$original;snapshotAssembly=$original;candidateAssembly=$candidate;assembly=$doc.path;object=$doc.object})}
  else{[IO.File]::Copy($doc.path,$candidate,$false)}
  $artifactRows.Add(@{name=$doc.name;assembly=$doc.path;inputSha256=$doc.sha256;original=$original;candidate=$candidate;candidateSha256=(Get-Mx251GcHash $candidate);changed=$wasChanged;removedSourceLines=$drop.Count;removedGlobalDeclarations=@($declarationsRemoved);removedUnusedExternalDeclarations=@();object=$doc.object;objectSha256=$doc.objectSha256})
 }
 $manifest=[ordered]@{schema='mx251.function-gc.v1';status=$(if($fallback){'CONSERVATIVE_KEEP_ALL'}else{'CLOSED_GRAPH_CANDIDATE'});fallback=$fallback;issues=@($issues);roots=@($roots|Sort-Object);functions=@($functions|ForEach-Object{@{unit=$_.unit;symbol=$_.symbol;startLine=$_.start+1;endLine=$(if($null -eq $_.end){$null}else{$_.end+1});kept=$live.Contains($_.id);isr=$_.isr;addressTaken=$_.addressTaken;opaque=$_.opaque;indirect=$_.indirect;edges=@($nodes[$_.id]|Sort-Object)}});removedFunctions=@($removed|ForEach-Object{@{unit=$_.unit;symbol=$_.symbol}});units=@($artifactRows);extraAssembly=@($documents|Where-Object{$_.extra}|ForEach-Object{@{path=$_.path;sha256=$_.sha256}});libraries=@($libraries);libraryReferencePolicy='All archive members conservatively root referenced application functions; no library rewriting.';hardwareAccess=$false;assemblerInvoked=$false;linkerInvoked=$false;limits=@('Function text ownership and conservative symbol graph only; final assembler/linker/relocation verification required.','Data/CONST/startup retained; no variable, pool or archive-member trimming.','Unknown metadata, instructions, references, opaque code or indirect calls retain all functions.','No cycle or correctness claim; no SDK source or public API changes.')}
 $manifestPath=Join-Path $outPath 'manifest.json';[IO.File]::WriteAllText($manifestPath,($manifest|ConvertTo-Json -Depth 60)+"`n",[Text.UTF8Encoding]::new($false))
 return @{Manifest=$manifestPath;ChangedUnits=@($changed.ToArray());Status=$manifest.status;Fallback=$fallback;RemovedFunctions=@($removed|ForEach-Object{$_.symbol})}
}
