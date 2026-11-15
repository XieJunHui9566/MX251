# Bind each internal ABI candidate to its definition, typed calls and surviving
# machine references. Static functions use a translation-unit scope.
function Get-Mx251TransportArguments([string]$Signature) {
 $depth=0;$start=-1
 for($i=0;$i -lt $Signature.Length;$i++) {
  if($Signature[$i] -ceq '('){if(-not $depth){$start=$i};$depth++}
  elseif($Signature[$i] -ceq ')'){$depth--}
 }
 if($depth -or $start -lt 0 -or -not $Signature.EndsWith(')')){return}
 $body=$Signature.Substring($start+1,$Signature.Length-$start-2)
 if(-not $body -or $body -ceq 'void'){return}
 $depth=0;$begin=0
 for($i=0;$i -lt $body.Length;$i++) {
  if($body[$i] -ceq '('){$depth++}elseif($body[$i] -ceq ')'){$depth--}
  elseif($body[$i] -ceq ',' -and -not $depth){$body.Substring($begin,$i-$begin);$begin=$i+1}
 }
 $body.Substring($begin)
}

function Get-Mx251LeafAbiCandidates([string]$Root,$Config,[string]$ToolRoot,[string]$ProfileRoot,[string[]]$Keep=@()) {
 if($Config.explicitAbi -or $Config.runtime -cne 'standard' -or @($Config.archives).Count){return}
 $cUnits=@($Config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.c'})
 $rawUnits=@($Config.units|Where-Object{[IO.Path]::GetExtension($_.file) -cne '.c'})
 if(@($cUnits|Where-Object{$_.backend -cne 'ordinary-wide' -or -not $_.stackAuto}).Count){return}
 $definitions=[hashtable]::new([StringComparer]::Ordinal);$texts=[hashtable]::new([StringComparer]::Ordinal);$calls=@();$refs=[hashtable]::new([StringComparer]::Ordinal);$allDefinitions=@()
 $slots=[Collections.Generic.HashSet[int]]::new()
 foreach($u in $cUnits) {
  $text=[IO.File]::ReadAllText((Join-Path $Root ($u.name+'.asm')));$texts[$u.name]=$text
  foreach($m in [regex]::Matches($text,'(?m)^; mx251-irq-definition schema=2 symbol=\S+ slot=([0-9]+) private=[01]\s*$')){$null=$slots.Add([int]$m.Groups[1].Value)}
  $rel=Read-Mx251GcRel ([IO.File]::ReadAllBytes((Join-Path $Root ($u.name+'.rel')))) $u.name
  foreach($name in $rel.Definitions){if(-not $refs.ContainsKey($name)){$refs[$name]=0};$refs[$name]++}
  foreach($m in [regex]::Matches($text,'(?ms)^; mx251-function begin schema=1 symbol=(\S+) isr=([01]) addressTaken=([01]) opaque=([01]) indirect=([01])\s*\r?\n(.*?)^; mx251-function end schema=1 symbol=\1\s*$')) {
   $name=$m.Groups[1].Value;$body=$m.Groups[6].Value;$escaped=[regex]::Escape($name)
   $exported=$text -cmatch ('(?m)^\s*\.globl\s+'+$escaped+'\s*$')
   $entry=@{symbol=$name;unit=$u.name;exported=$exported}
   $allDefinitions+=@($entry)
   $sig=[regex]::Matches($body,'(?m)^; mx251-transport-definition schema=1 symbol='+$escaped+' signature=(\S+)\s*$')
   $abi=[regex]::Matches($body,'(?m)^;\s*mx251-bank-contract schema=1 symbol='+$escaped+' abi=2 bank=0 naked=0\s*$')
   if($sig.Count -ne 1 -or $abi.Count -ne 1 -or $m.Groups[2].Value -ne '0' -or $m.Groups[3].Value -ne '0' -or $m.Groups[4].Value -ne '0'){continue}
   $entry.signature=$sig[0].Groups[1].Value;$entry.transport=$entry.signature
   $entry.scope=if($exported){'*'}else{$u.file.Replace('\','/')}
   if(-not $definitions.ContainsKey($name)){$definitions[$name]=@()};$definitions[$name]+=@($entry)
  }
  foreach($m in [regex]::Matches($text,'(?m)^; mx251-transport-call schema=1 owner=(\S+) target=(\S+) signature=(\S+)\s*$')) {
   $calls+=@{unit=$u.name;owner=$m.Groups[1].Value;target=$m.Groups[2].Value;signature=$m.Groups[3].Value}
  }
 }
 if($rawUnits.Count) {
  try{$homeArea=Get-Mx251FormatHome (Join-Path $Root 'linked.partial.map');foreach($u in $rawUnits){$null=Get-Mx251FormatRawVectors $Root $u $homeArea $slots}}catch{return}
 }
 $excluded=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 foreach($name in $Keep){$null=$excluded.Add($name)}
 foreach($file in @((Get-ChildItem -LiteralPath (Join-Path $ToolRoot 'lib') -Directory|Where-Object{$_.Name -like 'mcs251*'}|Get-ChildItem -Recurse -File -Filter '*.lib'))+@(Get-ChildItem -LiteralPath $ProfileRoot -Recurse -File -Filter '*.lib')) {
  foreach($member in @(Read-Mx251GcArchive $file.FullName)){foreach($name in $member.rel.References){$null=$excluded.Add($name)}}
 }
 foreach($line in [IO.File]::ReadAllLines((Join-Path $Root 'startup.asm'))){foreach($name in @(Get-Mx251GcTokens (Get-Mx251GcText $line))){$null=$excluded.Add($name)}}
 # Tokenize machine text once per unit, before considering candidates.
 $references=[hashtable]::new([StringComparer]::Ordinal)
 foreach($unit in $texts.Keys) {
  $entries=[hashtable]::new([StringComparer]::Ordinal);$owner=''
  foreach($raw in $texts[$unit].Split("`n")) {
   if($raw -cmatch '^; mx251-function begin schema=1 symbol=(\S+) '){$owner=$Matches[1]}
   if($raw -cmatch '^; mx251-function end '){$owner=''}
   $code=Get-Mx251GcText $raw
   if(-not $code){continue}
   foreach($token in @(Get-Mx251GcTokens $code)) {
    if(-not $definitions.ContainsKey($token)){continue}
    $escaped=[regex]::Escape($token)
    if($code -cmatch ('^(?:'+$escaped+':{1,2}|\.globl\s+'+$escaped+')$')){continue}
    if(-not $entries.ContainsKey($token)){$entries[$token]=[Collections.Generic.List[object]]::new()}
    $entries[$token].Add(@{owner=$owner;direct=($owner -and $code -cmatch ('^ecall\s+'+$escaped+'$'))})
   }
  }
  $references[$unit]=$entries
 }
 $selected=@()
 foreach($name in $definitions.Keys) {foreach($d in $definitions[$name]) {
  if($d.exported -and ($refs[$name] -ne 1 -or $excluded.Contains($name))){continue}
  $arguments=@(Get-Mx251TransportArguments $d.signature)
  if($arguments.Count -lt 1 -or $arguments.Count -gt 2){continue}
  $units=@($texts.Keys|Where-Object{
    $key=$_
    if(-not $d.exported){$key -ceq $d.unit}
    else{-not @($allDefinitions|Where-Object{$_.unit -ceq $key -and $_.symbol -ceq $name -and -not $_.exported}).Count}
  })
  $bad=$false;$machine=[hashtable]::new([StringComparer]::Ordinal)
  foreach($unit in $units) {
   if(-not $references[$unit].ContainsKey($name)){continue}
   foreach($reference in $references[$unit][$name]) {
    if($reference.direct) {
     $key=$unit+':'+$reference.owner;if(-not $machine.ContainsKey($key)){$machine[$key]=0};$machine[$key]++
    }else{$bad=$true}
   }
  }
  $edges=@($calls|Where-Object{$_.target -ceq $name -and $_.unit -cin $units})
  if($bad -or @($edges|Where-Object{$_.signature -cne $d.signature}).Count){continue}
  $typed=[hashtable]::new([StringComparer]::Ordinal);foreach($edge in $edges){$typed[$edge.unit+':'+$edge.owner]=$true}
  foreach($key in $machine.Keys){if(-not $typed.ContainsKey($key)){$bad=$true}}
  $count=0;foreach($n in $machine.Values){$count+=$n}
  if($bad -or $count -lt 4){continue}
  $d.units=$units;$d.calls=$count
  # Rank one joint trial; caller frequency does not claim a byte saving.
  $d.rank=$count*(1+$arguments.Count+(@($arguments|Where-Object{$_ -match 'p[GXC]\('}).Count*2))
  $selected+=@($d)
 }}
 @($selected|Sort-Object @{Expression={$_.rank};Descending=$true},symbol,unit)
}
