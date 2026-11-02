# 读取编译器语义符号清单和链接器选中的对象；不扫描用户 C 文本或按工程名特判。
function Get-Mx251InterruptSelection([string]$Root,$Config,[int]$VectorCount,[string]$DiscoveryMap='') {
 $all=@{};$archiveUnits=@{};$direct=@{}
 foreach($a in $Config.archives){foreach($name in $a.units){$archiveUnits[$name]=$a.name}}
 foreach($name in $Config.link){if($name -ne '@startup' -and -not $archiveUnits.ContainsKey($name)){$direct[$name]=$true}}
 foreach($u in $Config.units){
  if([IO.Path]::GetExtension($u.file) -ine '.c'){continue}
  $path=Join-Path $Root ($u.name+'.asm');$text=[IO.File]::ReadAllText($path,[Text.Encoding]::Latin1)
  $m=[regex]::Matches($text,'(?m)^; mx251-irq-module schema=2 hasMain=([01]) vectorCount=([0-9]+)\r?$')
  if($m.Count -ne 1 -or [int]$m[0].Groups[2].Value -ne $VectorCount){throw "MX251-IRQ010: missing or incompatible compiler ISR metadata: $($u.name)"}
  $defs=@();$regs=@()
  foreach($kind in @('definition','registration')){
   $seen=@{}
   $pattern=if($kind -eq 'definition'){'(?m)^; mx251-irq-definition schema=2 symbol=([A-Za-z_.$][A-Za-z0-9_.$]*) slot=([0-9]+) private=([01])\r?$'}else{'(?m)^; mx251-irq-registration schema=1 symbol=([A-Za-z_.$][A-Za-z0-9_.$]*) slot=([0-9]+)\r?$'}
   foreach($entry in [regex]::Matches($text,$pattern)){
    $name=$entry.Groups[1].Value;$slot=[int]$entry.Groups[2].Value
    if($slot -lt 0 -or $slot -gt 255 -or ($VectorCount -gt 0 -and $slot -ge $VectorCount) -or $seen.ContainsKey($name)){throw 'MX251-IRQ011: invalid or duplicate ISR metadata'}
    $seen[$name]=$true;$record=[ordered]@{symbol=$name;slot=$slot;unit=$u.name}
    if($kind -eq 'definition'){$record['private']=($entry.Groups[3].Value -eq '1');$defs+=,$record}else{$regs+=,$record}
   }
  }
  $all[$u.name]=[ordered]@{name=$u.name;assembly=$path;hasMain=($m[0].Groups[1].Value -eq '1');definitions=$defs;registrations=$regs}
 }
 $needsDiscovery=$false
 foreach($name in $archiveUnits.Keys){if($all.ContainsKey($name) -and ($all[$name].hasMain -or $all[$name].definitions.Count)){$needsDiscovery=$true}}
 if($needsDiscovery -and -not $DiscoveryMap){return [ordered]@{needsDiscovery=$true;schema=1}}
 $active=@{};foreach($name in $all.Keys){if($direct.ContainsKey($name)){$active[$name]=$true}}
 if($needsDiscovery){
  if(-not [IO.File]::Exists($DiscoveryMap)){throw 'MX251-IRQ012: archive selection map is missing'}
  $map=[IO.File]::ReadAllText($DiscoveryMap,[Text.Encoding]::Latin1)
  foreach($a in $Config.archives){
   $pattern='(?m)(?:^|[\\/])'+[regex]::Escape($a.name)+'\.lib\s*\[\s*([A-Za-z_][A-Za-z0-9_]*)\.rel\s*\]'
   foreach($match in [regex]::Matches($map,$pattern)){
    $name=$match.Groups[1].Value
    if($name -cnotin $a.units){throw 'MX251-IRQ013: unexpected archive member in linker map'}
    if($all.ContainsKey($name)){$active[$name]=$true}
   }
  }
 }
 $mains=@($active.Keys|Where-Object{$all[$_].hasMain})
 if($mains.Count -ne 1){throw 'MX251-IRQ014: exactly one linked compiler-owned vector table is required'}
 $main=$all[$mains[0]];$defs=@();$bySlot=@{};$byName=@{}
 foreach($name in @($active.Keys|Sort-Object)){
  foreach($entry in $all[$name].definitions){
   if($bySlot.ContainsKey($entry.slot) -or (-not $entry.private -and $byName.ContainsKey($entry.symbol))){throw "MX251-IRQ003: duplicate linked ISR slot or symbol: $($entry.slot) $($entry.symbol)"}
   $bySlot[$entry.slot]=$entry;if(-not $entry.private){$byName[$entry.symbol]=$entry};$defs+=,$entry
  }
 }
 $registrations=@{}
 foreach($entry in $main.registrations){
  if($registrations.ContainsKey($entry.slot) -or -not $bySlot.ContainsKey($entry.slot) -or $bySlot[$entry.slot].symbol -cne $entry.symbol -or ($bySlot[$entry.slot].private -and $bySlot[$entry.slot].unit -cne $main.name)){throw "MX251-IRQ002: ISR declaration/definition mismatch: $($entry.slot) $($entry.symbol)"}
  $registrations[$entry.slot]=$entry
 }
 $changed=@($defs|Where-Object{-not $registrations.ContainsKey($_.slot)})
 return [ordered]@{schema=1;needsDiscovery=$false;mainUnit=$main.name;mainAssembly=$main.assembly;vectorCount=$VectorCount;
  definitions=$defs;registrations=@($main.registrations);added=$changed;activeUnits=@($active.Keys|Sort-Object);
  archiveSelectionUsed=$needsDiscovery;discoveryMapSha256=if($DiscoveryMap){Hash-File $DiscoveryMap}else{$null};
  sourceInventory=@($all.Keys|Sort-Object|ForEach-Object{[ordered]@{unit=$_;sha256=(Hash-File $all[$_].assembly);active=$active.ContainsKey($_)}})}
}

function New-Mx251InterruptTable($Entries,[int]$VectorCount,[bool]$UseSlotAliases=$false) {
 $slots=@{};foreach($e in $Entries){$slots[[int]$e.slot]=$e.symbol}
 $count=$VectorCount
 if($count -eq 0 -and $slots.Count){$count=1+[int](($slots.Keys|Measure-Object -Maximum).Maximum)}
 $lines=[Collections.Generic.List[string]]::new();$lines.Add("`tljmp`t__sdcc_mcs251_reset_trampoline")
 for($i=0;$i -lt $count;$i++){
  if($slots.ContainsKey($i)){
   $destination=if($UseSlotAliases){"__mx251_irq_slot_v1_$i"}else{$slots[$i]};$lines.Add("`tejmp`t$destination")
   if($VectorCount -gt 0 -or $i -ne $count-1){$lines.Add("`t.ds`t4")}
  }else{
   if($VectorCount -gt 0){$lines.Add("`tejmp`t__sdcc_mcs251_unhandled_interrupt")}else{$lines.Add("`treti")}
   if($VectorCount -gt 0 -or $i -ne $count-1){$lines.Add($(if($VectorCount -gt 0){"`t.ds`t4"}else{"`t.ds`t7"}))}
  }
 }
 return ($lines -join "`n")+"`n"
}

function Set-Mx251InterruptTable([string]$Root,$Plan) {
 $text=[IO.File]::ReadAllText($Plan.mainAssembly,[Text.Encoding]::Latin1)
 $pattern='(?ms)^__interrupt_vect:\r?\n(.*?)^__sdcc_mcs251_reset_trampoline::\r?$'
 $matches=[regex]::Matches($text,$pattern)
 if($matches.Count -ne 1){throw 'MX251-IRQ015: compiler vector table boundary is unknown'}
 $old=New-Mx251InterruptTable $Plan.registrations $Plan.vectorCount
 $normalize={param([string]$s) (@($s -split '\r?\n'|Where-Object{$_.Trim()}|ForEach-Object{[regex]::Replace($_.Trim(),'\s+',' ')}) -join "`n")}
 if((&$normalize $matches[0].Groups[1].Value) -cne (&$normalize $old)){throw 'MX251-IRQ016: vector table differs from compiler semantic metadata'}
 $Plan['changed']=($Plan.added.Count -gt 0)
 if(-not $Plan.changed){return}
 [IO.File]::Copy($Plan.mainAssembly,(Join-Path $Root 'interrupt-vectors.original.asm'),$false)
 $new=New-Mx251InterruptTable $Plan.definitions $Plan.vectorCount $true
 $group=$matches[0].Groups[1]
 $next=$text.Substring(0,$group.Index)+$new+$text.Substring($group.Index+$group.Length)
 $globals=[Collections.Generic.List[string]]::new()
 foreach($entry in $Plan.definitions){$globals.Add("`t.globl __mx251_irq_slot_v1_$($entry.slot)")}
 foreach($entry in $Plan.added){
  if($Plan.vectorCount -gt 0){
   $reg="__mx251_irq_reg_v1_$($entry.slot)_$($entry.symbol)"
   $globals.Add("`t.globl $reg");$globals.Add("$reg = 1")
  }
 }
 # 仅更换编译器向量段并补外部符号登记；所有函数体及原始汇编字节文本保留。
 $next=($globals -join "`n")+"`n"+$next
 [IO.File]::WriteAllText($Plan.mainAssembly,$next,[Text.Encoding]::Latin1)
 $Plan['originalAssemblySha256']=Hash-File (Join-Path $Root 'interrupt-vectors.original.asm')
 $Plan['patchedAssemblySha256']=Hash-File $Plan.mainAssembly
}

function Assert-Mx251InterruptTable([string]$Root,$Config,$Plan,$Memory,[string]$Map,[long]$CodeBase) {
 $mapPath=Join-Path $Root 'linked.partial.map'
 $selected=Get-Mx251InterruptSelection $Root $Config $Plan.vectorCount $mapPath
 if(($selected.activeUnits -join ',') -cne ($Plan.activeUnits -join ',')){throw 'MX251-IRQ017: link member selection changed during vector resolution'}
 $proof=@()
 foreach($entry in $Plan.definitions){
  $m=[regex]::Matches($Map,'(?m)^C:\s+([0-9A-F]+)\s+'+[regex]::Escape("__mx251_irq_slot_v1_$($entry.slot)")+'\s+\S+\s*$')
  if($m.Count -ne 1){throw "MX251-IRQ018: missing final ISR symbol: $($entry.symbol)"}
  $target=[Convert]::ToInt64($m[0].Groups[1].Value,16);$at=[long]($CodeBase+3+8*$entry.slot)
  $expected=@(0x8a,[int](($target -shr 16)-band 255),[int](($target -shr 8)-band 255),[int]($target-band 255))
  for($i=0;$i -lt 4;$i++){if(-not $Memory.ContainsKey([long]($at+$i)) -or [int]$Memory[[long]($at+$i)] -ne $expected[$i]){throw "MX251-IRQ019: final vector bytes do not reach $($entry.symbol) at slot $($entry.slot)"}}
  $proof+=,[ordered]@{slot=$entry.slot;symbol=$entry.symbol;vector=$at;target=$target;bytes=$expected}
 }
 $Plan['finalVerified']=$true;$Plan['finalEntries']=$proof
 $Plan['scriptSha256']=Hash-File (Join-Path $PSScriptRoot 'InterruptVectors.ps1')
 Write-Json (Join-Path $Root 'interrupt-vectors.json') $Plan
}
