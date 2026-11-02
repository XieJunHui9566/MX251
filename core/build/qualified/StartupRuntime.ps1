# 空外部初始化路径按最终链接长度裁剪；保留每个 CRT 出口的完整机器状态。
function Get-Mx251StartupDigest([byte[]]$Data){return [Convert]::ToHexString([Security.Cryptography.SHA256]::HashData($Data)).ToLowerInvariant()}
function Read-Mx251StartupRel([byte[]]$Data,[string]$Name) {
 $null=Read-Mx251GcRel $Data $Name
 $areas=@();$symbols=@();$chunks=@();$area=-1;$pending=$null
 foreach($line in [Text.Encoding]::ASCII.GetString($Data).Split("`n")){
  $line=$line.TrimEnd("`r")
  if($line -cmatch '^A (\S+) size ([0-9A-F]+) flags ([0-9A-F]+) addr ([0-9A-F]+)$'){
   $areas+=@{name=$Matches[1];size=[Convert]::ToInt64($Matches[2],16);flags=[Convert]::ToInt32($Matches[3],16);address=[Convert]::ToInt64($Matches[4],16)};$area++;continue
  }
  if($line -cmatch '^S (\S+) (Def|Ref)([0-9A-F]+)$'){
   $symbols+=@{name=$Matches[1];binding=$Matches[2];value=[Convert]::ToInt64($Matches[3],16);area=$area};continue
  }
  if($line.StartsWith('T ')){$pending=@($line.Substring(2).Split(' ')|ForEach-Object{[Convert]::ToInt32($_,16)});continue}
  if($line.StartsWith('R ')){
   $r=@($line.Substring(2).Split(' ')|ForEach-Object{[Convert]::ToInt32($_,16)});$refs=@()
   for($i=4;$i -lt $r.Count;){
    $mode=$r[$i++];if(($mode -band 240) -eq 240){$mode=(($mode -band 15)*256)+$r[$i++]}
    $offset=$r[$i++];$index=$r[$i++]*256+$r[$i++]
    $target=if($mode -band 2){$symbols[$index].name}else{$areas[$index].name}
    $refs+=@{mode=$mode;index=$offset;targetIndex=$index;target=$target}
   }
   $chunks+=@{area=$r[2]*256+$r[3];offset=$pending[0]*65536+$pending[1]*256+$pending[2];raw=$pending;refs=$refs};$pending=$null
  }
 }
 return @{name=$Name;sha256=(Get-Mx251StartupDigest $Data);areas=$areas;symbols=$symbols;chunks=$chunks}
}

function Read-Mx251StartupMap([string]$Path) {
 $text=[IO.File]::ReadAllText($Path);$areas=@{};$symbols=@{}
 foreach($m in [regex]::Matches($text,'(?m)^(\S+)\s+([0-9A-F]{8})\s+([0-9A-F]{8})\s+=')){
  $name=$m.Groups[1].Value
  $value=@{base=[Convert]::ToInt64($m.Groups[2].Value,16);size=[Convert]::ToInt64($m.Groups[3].Value,16)}
  if($areas.ContainsKey($name) -and ($areas[$name].base -ne $value.base -or $areas[$name].size -ne $value.size)){throw 'startup-conflicting-map-area'}
  $areas[$name]=$value
 }
 foreach($m in [regex]::Matches($text,'(?m)^[CD]:\s+([0-9A-F]+)\s+(\S+)\s*$')){
  $name=$m.Groups[2].Value;$value=[Convert]::ToInt64($m.Groups[1].Value,16)
  if($symbols.ContainsKey($name) -and $symbols[$name] -ne $value){throw 'startup-ambiguous-map-symbol'}
  $symbols[$name]=$value
 }
 # ASlink 不为零长段输出区域表；成对的 s_/l_ 符号仍给出其真实位置。
 foreach($name in @($symbols.Keys)){
  if($name.StartsWith('s_') -and $symbols.ContainsKey('l_'+$name.Substring(2)) -and -not $areas.ContainsKey($name.Substring(2)) -and $symbols['l_'+$name.Substring(2)] -eq 0){
   $areas[$name.Substring(2)]=@{base=$symbols[$name];size=0}
  }
 }
 return @{areas=$areas;symbols=$symbols;text=$text}
}

function Get-Mx251StartupSelection([string]$Map,[string]$ToolRoot,$Contract,[string]$PrivateArchive='',$ReplacementHashes=@{}) {
 $text=[IO.File]::ReadAllText($Map)
 $section=[regex]::Match($text,'(?s)Libraries Linked\s+\[ object file \](.*?)(?:User Base Address Definitions|$)')
 if(-not $section.Success){throw 'startup-library-inventory-missing'}
 $rows=[regex]::Matches($section.Groups[1].Value,'(?m)^([^\r\n]+\.lib)\r?\n\s+\[\s*([^\]\s]+)\s*\]')
 $cache=@{};$selected=@()
 foreach($m in $rows){
  $path=[IO.Path]::GetFullPath($m.Groups[1].Value.Trim());$member=$m.Groups[2].Value
  $relative=[IO.Path]::GetRelativePath($ToolRoot,$path).Replace('\','/')
  $private=$PrivateArchive -and $path -ceq $PrivateArchive
  if(-not $cache.ContainsKey($path)){
   if(-not $private -and (-not $Contract.archives.Contains($relative) -or (Hash-File $path) -cne $Contract.archives[$relative].sha256)){throw 'startup-unqualified-library'}
   $cache[$path]=@(Read-Mx251StartupArchive $path)
  }
  $found=@($cache[$path]|Where-Object{$_.name -ceq $member});if($found.Count -ne 1){throw 'startup-ambiguous-member'}
  $item=$found[0];$expected=if($private){$ReplacementHashes[$member]}else{$Contract.archives[$relative].members[$member]}
  if(-not $expected -or $item.sha256 -cne $expected){throw 'startup-library-member-drift'}
  $selected+=@{path=$path;relative=$relative;member=$member;sha256=$item.sha256;rel=(Read-Mx251StartupRel $item.data ($path+'!'+$member))}
 }
 if(-not $selected.Count){throw 'startup-empty-selection'}
 return @{selected=$selected;archives=$cache}
}

function Get-Mx251StartupDemand([string]$Root,$Config,[string[]]$LinkArguments,$Selection,$Contract) {
 if($Config.runtime -cne 'standard' -or $Config.explicitAbi -or @($Config.archives).Count){throw 'startup-nonstandard-link-contract'}
 if((Hash-File (Join-Path $Root 'startup.asm')) -cnotin $Contract.startupHashes){throw 'startup-unknown-startup'}
 $map=Read-Mx251StartupMap (Join-Path $Root 'linked.partial.map');$sizes=@{}
 foreach($name in @('XINIT','XISEG','PSEG','XSEG')){
  if(-not $map.areas.ContainsKey($name) -or -not $map.symbols.ContainsKey('l_'+$name) -or -not $map.symbols.ContainsKey('s_'+$name) -or $map.areas[$name].size -ne $map.symbols['l_'+$name] -or $map.areas[$name].base -ne $map.symbols['s_'+$name]){throw 'startup-unproved-area-extent'}
  $sizes[$name]=$map.areas[$name].size
 }
 if($sizes.XINIT -or $sizes.XISEG){throw 'startup-copy-area-nonempty'}
 $replace=@('crtxinit.rel');if(-not $sizes.PSEG -and -not $sizes.XSEG){$replace+=@('crtxclear.rel')}
 $objects=@();$inputs=@();$metadata=@{};$rawVectors=@();$mainCount=0
 $slots=[Collections.Generic.HashSet[int]]::new()
 foreach($u in $Config.units|Where-Object{[IO.Path]::GetExtension($_.file) -ceq '.c'}){
  $text=[IO.File]::ReadAllText((Join-Path $Root ($u.name+'.asm')))
  foreach($m in [regex]::Matches($text,'(?m)^; mx251-irq-definition schema=2 symbol=\S+ slot=([0-9]+) private=[01]\s*$')){$null=$slots.Add([int]$m.Groups[1].Value)}
 }
 foreach($u in $Config.units){
  if([IO.Path]::GetExtension($u.file) -cne '.c'){
   $rawVectors+=@(Get-Mx251FormatRawVectors $Root $u $map.areas.HOME $slots)
   $inputs+=@{path=$u.file;sha256=(Hash-File (Join-Path $Root $u.file))};continue
  }
  $metadata[$u.name+'.rel']=$u.name+'.asm'
 }
 if('format-runtime.rel' -cin $LinkArguments){$metadata['format-runtime.rel']='format-runtime.asm'}
 foreach($name in @($LinkArguments|Where-Object{$_ -cmatch '\.rel$'})){
  if([IO.Path]::GetFileName($name) -cne $name){throw 'startup-nonlocal-object'}
  $path=Join-Path $Root $name;$ob=Read-Mx251StartupRel ([IO.File]::ReadAllBytes($path)) $name
  $inputs+=@{path=$name;sha256=$ob.sha256};$objects+=@{member=$null;rel=$ob}
  if($metadata.ContainsKey($name)){
   $asm=$metadata[$name];$path=Join-Path $Root $asm;$text=[IO.File]::ReadAllText($path)
   $inputs+=@{path=$asm;sha256=(Hash-File $path)}
   $begins=[regex]::Matches($text,'(?m)^; mx251-function begin schema=1 symbol=(\S+) isr=([01]) addressTaken=([01]) opaque=([01]) indirect=([01])\s*$')
   $ends=[regex]::Matches($text,'(?m)^; mx251-function end schema=1 symbol=(\S+)\s*$')
   if(-not $begins.Count -or $begins.Count -ne $ends.Count){throw 'startup-unproved-function-coverage'}
   $names=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
   for($i=0;$i -lt $begins.Count;$i++){
    $b=$begins[$i];$symbol=$b.Groups[1].Value
    if($symbol -cne $ends[$i].Groups[1].Value -or $b.Groups[4].Value -cne '0'){throw 'startup-opaque-or-incomplete-function'}
    $null=$names.Add($symbol)
    if($symbol -ceq '_main'){if($b.Groups[2].Value -cne '0'){throw 'startup-main-is-isr'};$mainCount++}
   }
   foreach($s in $ob.symbols|Where-Object{$_.binding -ceq 'Def' -and $_.area -ge 0 -and $_.name.StartsWith('_')}){
    if($ob.areas[$s.area].name -ceq 'CSEG' -and -not $names.Contains($s.name)){
     # 跨翻译单元向量注册的链接别名必须准确指向同一个已标注 ISR。
     $provedAlias=$false
     if($s.name -cmatch '^__mx251_irq_(?:slot|def)_v1_([0-9]+)(?:_\S+)?$'){
      $slot=$Matches[1]
      $irq=[regex]::Matches($text,'(?m)^; mx251-irq-definition schema=2 symbol=(\S+) slot='+$slot+' private=[01]\s*$')
      if($irq.Count -ge 1 -and @($irq|ForEach-Object{$_.Groups[1].Value}|Sort-Object -Unique).Count -eq 1){
       $symbol=$irq[0].Groups[1].Value;$definition=@($ob.symbols|Where-Object{$_.name -ceq $symbol -and $_.binding -ceq 'Def'})
       $isr=@($begins|Where-Object{$_.Groups[1].Value -ceq $symbol -and $_.Groups[2].Value -ceq '1'})
       $provedAlias=($s.name -ceq ('__mx251_irq_slot_v1_'+$slot) -or $s.name -ceq ('__mx251_irq_def_v1_'+$slot+'_'+$symbol)) -and $isr.Count -eq 1 -and $definition.Count -eq 1 -and $definition[0].area -eq $s.area -and $definition[0].value -eq $s.value -and [regex]::IsMatch($text,'(?m)^'+[regex]::Escape($s.name)+'\s*=\s*'+[regex]::Escape($symbol)+'\s*$')
      }
     }
     if(-not $provedAlias){throw 'startup-unannotated-cseg-entry'}
    }
   }
  } elseif($name -cne 'startup.rel' -and $name -cnotin @($rawVectors|ForEach-Object{$_.unit+'.rel'})){throw 'startup-unknown-direct-object'}
 }
 if($mainCount -ne 1){throw 'startup-main-not-unique'}
 $objects+=@($Selection.selected)
 $members=@{};$startCount=0;$markerNames=@('__mcs51_genXINIT','__mcs51_genXRAMCLEAR')
 foreach($item in $objects){
  $ob=$item.rel;$member=$item.member;$crt=$member -and $Contract.crtMembers.ContainsKey($member)
  if($crt){if($ob.sha256 -cnotin @($Contract.crtMembers[$member]) -or $members.ContainsKey($member)){throw 'startup-nonstandard-crt'};$members[$member]=$item}
  $start=$ob.name -ceq 'startup.rel';if($start){$startCount++}
  foreach($a in $ob.areas){if($sizes.ContainsKey($a.name) -and $sizes[$a.name] -eq 0 -and $a.size){throw 'startup-rel-empty-extent-conflict'}}
  foreach($s in $ob.symbols){
   if($s.binding -ceq 'Def' -and $s.name -ceq '___sdcc_external_startup' -and $member -cne '_startup.rel'){throw 'startup-custom-external-hook'}
   if($s.binding -ceq 'Def' -and $s.name -ceq '__sdcc_gsinit_startup' -and -not $start){throw 'startup-unknown-initial-entry'}
  }
  foreach($c in $ob.chunks){foreach($r in $c.refs){
   if($r.target -cin $markerNames){throw 'startup-real-marker-reference'}
   if(-not $crt -and $r.target -cmatch '^(?:[sl]_)?GSINIT[34]$'){throw 'startup-init-address-reference'}
  }}
  if(-not $crt -and -not $start){
   for($i=0;$i -lt $ob.areas.Count;$i++){
    $a=$ob.areas[$i]
    if(-not $a.size -or $a.name -cnotmatch '^(GSINIT[0-5]?|GSFINAL)$'){continue}
    $cs=@($ob.chunks|Where-Object{$_.area -eq $i -and $_.raw.Count -gt 3})
    if($a.name -cne 'GSFINAL' -or $a.size -ne 4 -or $cs.Count -ne 1){throw 'startup-custom-initialization'}
    $c=$cs[0]
    if($c.offset -ne 0 -or $c.raw.Count -ne 7 -or $c.raw[3] -ne 138 -or $c.refs.Count -ne 1){throw 'startup-unknown-gsfinal-bytes'}
    $rr=$c.refs[0];$addend=$c.raw[4]*65536+$c.raw[5]*256+$c.raw[6]
    $entry=@($ob.symbols|Where-Object{$_.name -ceq '__sdcc_program_startup' -and $_.binding -ceq 'Def'})
    $direct=$rr.mode -eq 130 -and $rr.target -ceq '__sdcc_program_startup' -and -not $addend
    $local=$rr.mode -eq 128 -and $entry.Count -eq 1 -and $entry[0].area -eq $rr.targetIndex -and $entry[0].value -eq $addend
    if($rr.index -ne 4 -or -not ($direct -or $local)){throw 'startup-unknown-gsfinal-relocation'}
   }
  }
 }
 if($startCount -ne 1 -or $members.Count -ne $Contract.crtMembers.Count){throw 'startup-incomplete-canonical-closure'}
 $archive=@($replace|ForEach-Object{$members[$_].path}|Sort-Object -Unique)
 if($archive.Count -ne 1){throw 'startup-ambiguous-crt-archive'}
 return @{replacements=$replace;archive=$archive[0];map=$map;inputs=$inputs;rawVectors=$rawVectors;sectionBytes=$sizes;selectedObjectCount=$objects.Count}
}

function Invoke-Mx251StartupRuntime([string]$Root,$Config,[string]$ToolRoot) {
 $record=[ordered]@{schema=1;status='FALLBACK_FULL_STARTUP';applied=$false;hardwareExecuted=$false};$saved=@();$accepted=$false
 try {
  . (Join-Path $PSScriptRoot 'StartupArchive.ps1')
  $contractPath=Join-Path $PSScriptRoot 'StartupRuntimeContract.json';$contract=Get-Content -LiteralPath $contractPath -Raw|ConvertFrom-Json -AsHashtable
  if($contract.schema -ne 1 -or $contract.contract -cne 'mx251.empty-crt-state-preservation.v1'){throw 'startup-contract-mismatch'}
  $record.contractSha256=Hash-File $contractPath
  if((Hash-File (Join-Path $ToolRoot 'bin/sdas251.exe')) -cne $contract.assemblerSha256){throw 'startup-unqualified-assembler'}
  $prior=@($commands|Where-Object{$_.label -ceq 'link'});if($prior.Count -ne 1){throw 'startup-ambiguous-link-command'};$prior=$prior[0]
  $linkArgs=@($prior.arguments);if($linkArgs[-2] -cne '-o' -or $linkArgs[-1] -cne 'linked.partial.hex'){throw 'startup-unexpected-link-output'}
  $selection=Get-Mx251StartupSelection (Join-Path $Root 'linked.partial.map') $ToolRoot $contract
  $demand=Get-Mx251StartupDemand $Root $Config $linkArgs $selection $contract;$record.demand=$demand
  $parentImage=Get-Mx251FormatHex (Join-Path $Root 'linked.partial.hex');$record.parentBytes=$parentImage.Count
  $work=Join-Path $Root 'startup-runtime';$privateDir=Join-Path $Root 'startup-libraries';$null=[IO.Directory]::CreateDirectory($work);$null=[IO.Directory]::CreateDirectory($privateDir)
  $privateArchive=Join-Path $privateDir ([IO.Path]::GetFileName($demand.archive));$members=$selection.archives[$demand.archive];$hashes=@{};$arguments=@();$changes=@()
  foreach($member in $members){
   if([IO.Path]::GetFileName($member.name) -cne $member.name -or $hashes.ContainsKey($member.name)){throw 'startup-invalid-archive-member-name'}
   $path=Join-Path $work $member.name
   if($member.name -cin $demand.replacements){
    $asm=[IO.Path]::ChangeExtension($member.name,'.asm');$template=Join-Path (Join-Path $PSScriptRoot 'startup-templates') $asm
    if((Hash-File $template) -cne $contract.templates[$asm]){throw 'startup-template-drift'}
    $text=[IO.File]::ReadAllText($template)
    foreach($name in @('XINIT','XISEG','PSEG','XSEG')){
     $value=$demand.map.symbols['s_'+$name];if($value -lt 0 -or $value -gt 16777215){throw 'startup-address-width'}
     $text=$text.Replace('#(s_'+$name+' >> 16)',('#0x{0:x2}' -f (($value -shr 16) -band 255))).Replace('#s_'+$name,('#0x{0:x4}' -f ($value -band 65535)))
    }
    [IO.File]::WriteAllText((Join-Path $work $asm),$text,[Text.UTF8Encoding]::new($false))
    Run-Tool ('startup-runtime-'+[IO.Path]::GetFileNameWithoutExtension($member.name)) (Join-Path $ToolRoot 'bin/sdas251.exe') @('-plosgff',('startup-runtime/'+$member.name),('startup-runtime/'+$asm))
    $new=Read-Mx251StartupRel ([IO.File]::ReadAllBytes($path)) $member.name
    $size=($new.areas|Measure-Object -Property size -Sum).Sum;$expected=if($member.name -ceq 'crtxinit.rel'){22}else{14}
    if($size -ne $expected){throw 'startup-replacement-size-mismatch'}
    $changes+=@{member=$member.name;beforeSha256=$member.sha256;afterSha256=$new.sha256;beforeBytes=if($member.name -ceq 'crtxinit.rel'){45}else{66};afterBytes=$size;sourceSha256=(Hash-File (Join-Path $work $asm))}
   } else {[IO.File]::WriteAllBytes($path,$member.data)}
   $hashes[$member.name]=Hash-File $path;$arguments+=@('startup-runtime/'+$member.name)
  }
  Run-Tool 'startup-runtime-archive' (Join-Path $ToolRoot 'bin/sdar.exe') (@('-rc',$privateArchive)+$arguments)
  $newMembers=@(Read-Mx251StartupArchive $privateArchive)
  if($newMembers.Count -ne $members.Count){throw 'startup-archive-count-changed'}
  for($i=0;$i -lt $members.Count;$i++){if($newMembers[$i].name -cne $members[$i].name -or $newMembers[$i].sha256 -cne $hashes[$members[$i].name]){throw 'startup-archive-order-content-changed'}}
  $backup=Join-Path $Root 'startup-baseline';$null=[IO.Directory]::CreateDirectory($backup)
  foreach($p in Get-ChildItem -LiteralPath $Root -File|Where-Object{$_.Name -like 'linked.partial.*' -or $_.Extension -ceq '.rst'}){
   [IO.File]::Copy($p.FullName,(Join-Path $backup $p.Name),$false);$saved+=@{name=$p.Name;sha256=(Hash-File $p.FullName)}
  }
  $record.baselineFiles=$saved
  Run-Tool 'startup-link' $compilerExe (@('-L'+$privateDir)+$linkArgs)
  $after=Get-Mx251StartupSelection (Join-Path $Root 'linked.partial.map') $ToolRoot $contract $privateArchive $hashes
  if($after.selected.Count -ne $selection.selected.Count){throw 'startup-selected-closure-count-changed'}
  for($i=0;$i -lt $selection.selected.Count;$i++){
   $a=$selection.selected[$i];$b=$after.selected[$i]
   if($a.member -cne $b.member -or [IO.Path]::GetFileName($a.path) -cne [IO.Path]::GetFileName($b.path)){throw 'startup-selected-order-changed'}
   $expected=if($a.member -cin $demand.replacements){$hashes[$a.member]}else{$a.sha256}
   if($b.sha256 -cne $expected){throw 'startup-selected-content-changed'}
  }
  $map=Read-Mx251StartupMap (Join-Path $Root 'linked.partial.map')
  foreach($name in @('XISEG','PSEG','XSEG','HOME','SSEG')){
   if($map.areas[$name].base -ne $demand.map.areas[$name].base -or $map.areas[$name].size -ne $demand.map.areas[$name].size){throw 'startup-data-or-vector-layout-changed'}
  }
  if($map.areas.XINIT.size -or $map.symbols.l_XINIT -or $map.symbols.l_XISEG){throw 'startup-copy-extent-changed'}
  $image=Get-Mx251FormatHex (Join-Path $Root 'linked.partial.hex');$record.candidateBytes=$image.Count
  $delta=0;foreach($change in $changes){$delta+=$change.beforeBytes-$change.afterBytes}
  if($delta -le 0 -or $record.parentBytes-$record.candidateBytes -ne $delta){throw 'startup-whole-image-cost-mismatch'}
  foreach($raw in $demand.rawVectors){foreach($jump in $raw.jumps){
   for($i=0;$i -lt 3;$i++){if(-not $image.ContainsKey($jump.source+$i) -or $image[$jump.source+$i] -ne $parentImage[$jump.source+$i]){throw 'startup-raw-vector-bytes-changed'}}
   if($image[$jump.target] -ne 138 -or $parentImage[$jump.target] -ne 138){throw 'startup-raw-target-changed'}
  }}
  foreach($p in $demand.inputs){if((Hash-File (Join-Path $Root $p.path)) -cne $p.sha256){throw 'startup-input-drift'}}
  $trial=@($commands|Where-Object{$_.label -ceq 'startup-link'})[-1]
  $prior.label='startup-baseline-link';$trial.label='link';$trial.actualLogLabel='startup-link';Write-Json (Join-Path $Root 'commands.json') @($commands.ToArray())
  $record.status='APPLIED_STATE_PRESERVING_EMPTY_CRT';$record.applied=$true;$record.savedBytes=$delta;$record.changes=$changes;$record.librarySha256=Hash-File $privateArchive;$accepted=$true
 } catch {
  if($_.Exception.Message -match 'PAUSE_REQUESTED'){throw}
  $record.reason=$_.Exception.Message;$record.diagnostic=$_.ScriptStackTrace
 } finally {
  if(-not $accepted -and $saved.Count){foreach($p in $saved){
   $backupFile=Join-Path (Join-Path $Root 'startup-baseline') $p.name
   if((Hash-File $backupFile) -cne $p.sha256){throw 'startup-baseline-drift'}
   [IO.File]::Copy($backupFile,(Join-Path $Root $p.name),$true)
  }}
 }
 return $record
}
