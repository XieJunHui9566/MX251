# 普通公开 ABI 不变；事实只来自本次完整 C 对象的闭合入口与 typed IR。
function Get-Mx251ScalarFacts([string]$Root,$Config,[string]$ToolRoot) {
 if($Config.explicitAbi -or $Config.runtime -cne 'standard' -or @($Config.archives).Count){throw 'scalar-nonstandard-or-archive'}
 $functions=[hashtable]::new([StringComparer]::Ordinal);$texts=[hashtable]::new([StringComparer]::Ordinal);$definitions=[hashtable]::new([StringComparer]::Ordinal);$calls=@();$fingerprints=@();$duplicates=[hashtable]::new([StringComparer]::Ordinal)
 foreach($u in $Config.units){
  if([IO.Path]::GetExtension($u.file) -cne '.c' -or -not $u.stackAuto -or $u.backend -ceq 'legacy'){throw 'scalar-unknown-unit'}
  $path=Join-Path $Root ($u.name+'.asm');$t=[IO.File]::ReadAllText($path);$texts[$u.name]=$t
  $fingerprints+=@{path=$u.name+'.asm';sha256=(Hash-File $path)}
  $fingerprints+=@{path=$u.name+'.rel';sha256=(Hash-File (Join-Path $Root ($u.name+'.rel')))}
  $begin=[regex]::Matches($t,'(?m)^; mx251-function begin schema=1 symbol=(\S+) isr=([01]) addressTaken=([01]) opaque=([01]) indirect=([01])\s*$')
  $blocks=[regex]::Matches($t,'(?ms)^; mx251-function begin schema=1 symbol=(\S+) isr=([01]) addressTaken=([01]) opaque=([01]) indirect=([01])\s*\r?\n(.*?)^; mx251-function end schema=1 symbol=\1\s*$')
  if(-not $begin.Count -or $begin.Count -ne $blocks.Count -or [regex]::Matches($t,'(?m)^; mx251-function begin ').Count -ne $begin.Count -or [regex]::Matches($t,'(?m)^; mx251-function end ').Count -ne $blocks.Count){throw 'scalar-function-boundary'}
  if($t -cmatch '(?m)^\s*(?:ecall|ejmp)\s+@'){throw 'scalar-indirect-machine-entry'}
  foreach($m in $blocks){
   $name=$m.Groups[1].Value
   if($m.Groups[4].Value -ne '0' -or $m.Groups[5].Value -ne '0'){throw 'scalar-opaque-or-indirect'}
   if($functions.ContainsKey($name)){$duplicates[$name]=$true}else{$functions[$name]=@{unit=$u.name;isr=$m.Groups[2].Value;escape=$m.Groups[3].Value;body=$m.Groups[6].Value}}
  }
  foreach($m in [regex]::Matches($t,'(?m)^; mx251-scalar-definition schema=1 symbol=(\S+) signature=(\S+)\s*$')){
   $name=$m.Groups[1].Value;if($definitions.ContainsKey($name)){$duplicates[$name]=$true}else{$definitions[$name]=@{unit=$u.name;signature=$m.Groups[2].Value}}
  }
  foreach($m in [regex]::Matches($t,'(?m)^; mx251-scalar-call schema=1 owner=(\S+) target=(\S+) signature=(\S+) values=(\S+)\s*$')){
   $calls+=@{unit=$u.name;owner=$m.Groups[1].Value;target=$m.Groups[2].Value;signature=$m.Groups[3].Value;values=@($m.Groups[4].Value.Split(','))}
  }
 }
 $libraryRefs=[Collections.Generic.HashSet[string]]::new([StringComparer]::Ordinal)
 foreach($p in Get-ChildItem -LiteralPath (Join-Path $ToolRoot 'lib') -Directory|Where-Object{$_.Name -like 'mcs251*'}|Get-ChildItem -Recurse -File -Filter '*.lib'){
  foreach($member in @(Read-Mx251GcArchive $p.FullName)){foreach($ref in $member.rel.References){$null=$libraryRefs.Add($ref)}}
 }
 $selected=@()
 foreach($name in @($definitions.Keys|Sort-Object -CaseSensitive)){
  $d=$definitions[$name]
  if($duplicates.ContainsKey($name) -or -not $functions.ContainsKey($name) -or $libraryRefs.Contains($name)){continue}
  $f=$functions[$name];if($f.unit -cne $d.unit -or $f.isr -cne '0' -or $f.escape -cne '0'){continue}
  $edges=@($calls|Where-Object{$_.target -ceq $name});if(-not $edges.Count){continue}
  if(@($edges|Where-Object{$_.signature -cne $d.signature -or -not $functions.ContainsKey($_.owner) -or $functions[$_.owner].unit -cne $_.unit}).Count){continue}
  $signature=[regex]::Match($d.signature,'^(?:v|[iu](?:8|16|32))\((?<args>(?:[iu](?:8|16|32)|p[123]_[0-9]+)(?:,(?:[iu](?:8|16|32)|p[123]_[0-9]+)){0,7})\)$')
  if(-not $signature.Success){continue};$types=@($signature.Groups['args'].Value.Split(','));$bad=$false;$machineCalls=0
  foreach($unit in $texts.Keys){foreach($raw in $texts[$unit].Split("`n")){
   $code=Get-Mx251GcText $raw;if($name -cnotin @(Get-Mx251GcTokens $code)){continue}
   if($code -ceq ($name+':') -or $code -ceq ($name+'::') -or $code -cmatch ('^\.globl\s+'+[regex]::Escape($name)+'$')){continue}
   if($code -cmatch ('^(ecall|ejmp)\s+'+[regex]::Escape($name)+'$')){$machineCalls++;continue}
   $bad=$true
  }}
  # startup 是锁定配置提供的根，不能把根当作普通常量调用。
  if($name -cin @(Get-Mx251GcTokens ([IO.File]::ReadAllText((Join-Path $Root 'startup.asm'))))){$bad=$true}
  if($bad -or $machineCalls -ne $edges.Count){continue}
  for($i=0;$i -lt $types.Count;$i++){
   if($types[$i] -cnotmatch '^[iu](8|16|32)$'){continue}
   if(@($edges|Where-Object{$_.values.Count -ne $types.Count -or $_.values[$i] -cnotmatch '^[0-9a-f]{8}$'}).Count){continue}
   $bits=@($edges|ForEach-Object{$_.values[$i]}|Sort-Object -Unique -CaseSensitive);if($bits.Count -ne 1){continue}
   $selected+=@{unit=$d.unit;name=$name;signature=$d.signature;ordinal=$i+1;type=$types[$i];bits=$bits[0];callCount=$edges.Count}
  }
 }
 return @{selected=$selected;inputs=$fingerprints;calls=$calls}
}
function Get-Mx251ScalarObjectCost([string]$Path){
 $t=[IO.File]::ReadAllText($Path);$code=0L;$data=@{}
 foreach($m in [regex]::Matches($t,'(?m)^A (\S+) size ([0-9A-F]+) flags ([0-9A-F]+) addr ([0-9A-F]+)\s*$')){
  $size=[Convert]::ToInt64($m.Groups[2].Value,16);$flags=[Convert]::ToInt64($m.Groups[3].Value,16)
  if($flags -band 32){$code+=$size}else{$data[$m.Groups[1].Value]=$size}
 }
 if(-not $code){throw 'scalar-code-cost-unknown'}
 return @{code=$code;data=$data;rel=(Read-Mx251GcRel ([IO.File]::ReadAllBytes($Path)) $Path)}
}
# LST 只用于剪去非收益候选；最终是否保留仍由整个 REL 的实际代码字节决定。
# 不拼接不同后端/编译结果的汇编函数，不改变任何标签或重定位。
function Get-Mx251ScalarFunctionBytes([string]$Path){
 $out=[hashtable]::new([StringComparer]::Ordinal);$name=$null;$bytes=0
 foreach($line in [IO.File]::ReadAllLines($Path)){
  if($line -cmatch 'mx251-function begin schema=1 symbol=(\S+)'){$name=$Matches[1];$bytes=0;continue}
  if($name -and $line -cmatch '^      [0-9A-F]{6} ' -and $line.Length -ge 28){$bytes+=[regex]::Matches($line.Substring(13,15),'[0-9A-F]{2}').Count}
  if($name -and $line -cmatch 'mx251-function end schema=1 symbol=(\S+)'){
   if($Matches[1] -cne $name -or $out.ContainsKey($name)){throw 'scalar-function-cost-boundary'}
   $out[$name]=$bytes;$name=$null
  }
 }
 if($name){throw 'scalar-function-cost-truncated'}
 return ,$out
}
function Invoke-Mx251ScalarParameters([string]$Root,$Config,[string]$ToolRoot){
 $record=@{schema=1;status='FALLBACK';applied=$false;hardwareExecuted=$false;selected=@();units=@()};$saved=@();$accepted=$false
 try{
  . (Join-Path $PSScriptRoot 'FunctionGc.ps1')
  $facts=Get-Mx251ScalarFacts $Root $Config $ToolRoot;$record.facts=$facts
  if(-not $facts.selected.Count){throw 'scalar-no-closed-singleton'}
  $backup=Join-Path $Root 'scalar-baseline';[IO.Directory]::CreateDirectory($backup)|Out-Null
  foreach($group in @($facts.selected|Group-Object unit)){
   $name=$group.Name;$u=@($Config.units|Where-Object{$_.name -ceq $name})[0]
   $files=@(Get-ChildItem -LiteralPath $Root -File|Where-Object{$_.BaseName -ceq $name})
   foreach($p in $files){[IO.File]::Copy($p.FullName,(Join-Path $backup $p.Name),$false);$saved+=@{name=$p.Name;sha256=(Hash-File $p.FullName)}}
   $parent=Get-Mx251ScalarObjectCost (Join-Path $Root ($name+'.rel'))
   $contract=Join-Path $Root ($name+'.scalar.txt');$lines=@('MX251-ORDINARY-SCALAR-V1')+@($group.Group|ForEach-Object{$_.name+"`t"+$_.signature+"`t"+$_.ordinal+"`t"+$_.bits})
   [IO.File]::WriteAllText($contract,(($lines -join "`n")+"`n"),[Text.UTF8Encoding]::new($false))
   $script:scalarParameterContracts[$name]=@{path=$contract;sha256=(Hash-File $contract)}
   $selected=@($group.Group);$attempts=@()
   $args=@(Flags $u.backend $u.stackAuto $u.defines)+@('-c',$u.file,'-o',($name+'.rel'))
   try{
    Run-Tool ('scalar-'+$name) $compilerExe $args;Check-Assembly (Join-Path $Root ($name+'.asm'))
    $candidate=Get-Mx251ScalarObjectCost (Join-Path $Root ($name+'.rel'))
    if($candidate.code -ge $parent.code){
     $before=Get-Mx251ScalarFunctionBytes (Join-Path $backup ($name+'.lst'))
     $after=Get-Mx251ScalarFunctionBytes (Join-Path $Root ($name+'.lst'))
     $kept=@();$costs=@()
     foreach($fn in @($selected|Group-Object name -CaseSensitive)){
      $keep=$before.ContainsKey($fn.Name) -and $after.ContainsKey($fn.Name) -and $after[$fn.Name] -lt $before[$fn.Name]
      $costs+=@{name=$fn.Name;parentBytes=$before[$fn.Name];candidateBytes=$after[$fn.Name];keep=$keep}
      if($keep){$kept+=@($fn.Group)}
     }
     foreach($ext in @('asm','rel','lst')){[IO.File]::Copy((Join-Path $Root ($name+'.'+$ext)),(Join-Path $Root ($name+'.scalar-all.'+$ext)),$false)}
     $attempts+=@{parentCode=$parent.code;candidateCode=$candidate.code;functionCosts=$costs;decision='REJECT_ALL_FACTS_TRIAL'}
     if(-not $kept.Count -or $kept.Count -ge $selected.Count){$record.units+=@{unit=$name;attempts=$attempts;accepted=$false};throw 'scalar-not-profitable'}
     $selected=$kept
     $lines=@('MX251-ORDINARY-SCALAR-V1')+@($selected|ForEach-Object{$_.name+"`t"+$_.signature+"`t"+$_.ordinal+"`t"+$_.bits})
     [IO.File]::WriteAllText($contract,(($lines -join "`n")+"`n"),[Text.UTF8Encoding]::new($false))
     $script:scalarParameterContracts[$name]=@{path=$contract;sha256=(Hash-File $contract)}
     Run-Tool ('scalar-subset-'+$name) $compilerExe $args;Check-Assembly (Join-Path $Root ($name+'.asm'))
     $candidate=Get-Mx251ScalarObjectCost (Join-Path $Root ($name+'.rel'))
     if($candidate.code -ge $parent.code){throw 'scalar-subset-not-profitable'}
    }
    $logLabel=if($attempts.Count){'scalar-subset-'+$name}else{'scalar-'+$name}
    $diagnostics=[IO.File]::ReadAllText((Join-Path $Root ($logLabel+'.stderr.log')))
    foreach($fact in $selected){
     $expected='MX251 ordinary scalar applied symbol='+$fact.name+' signature='+$fact.signature+' ordinal='+$fact.ordinal+' bits='+$fact.bits+' abi=2'
     if([regex]::Matches($diagnostics,[regex]::Escape($expected)).Count -ne 1){throw 'scalar-fact-application-not-confirmed'}
    }
    foreach($key in @($candidate.data.Keys)+@($parent.data.Keys)){if($candidate.data[$key] -ne $parent.data[$key]){throw 'scalar-data-layout-change'}}
    foreach($ref in $candidate.rel.References){if(-not $parent.rel.References.Contains($ref)){throw 'scalar-new-helper-reference'}}
    foreach($def in $parent.rel.Definitions){if(-not $candidate.rel.Definitions.Contains($def)){throw 'scalar-definition-removed'}}
    $record.units+=@{unit=$name;parentCode=$parent.code;candidateCode=$candidate.code;accepted=$true;attempts=$attempts;contractSha256=(Hash-File $contract)}
    $record.selected+=@($selected)
   }catch{
    if($_.Exception.Message -match 'PAUSE_REQUESTED'){throw}
    $record.units+=@{unit=$name;accepted=$false;reason=$_.Exception.Message}
    $script:scalarParameterContracts.Remove($name)
    foreach($p in $files){[IO.File]::Copy((Join-Path $backup $p.Name),$p.FullName,$true)}
   }
  }
  if(-not $record.selected.Count){throw 'scalar-no-profitable-units'}
  # 重新读取当前对象：常量化可删边，但不得产生不一致的新入口。
  $final=Get-Mx251ScalarFacts $Root $Config $ToolRoot
  foreach($r in $record.selected){
   $matches=@($final.selected|Where-Object{$_.unit -ceq $r.unit -and $_.name -ceq $r.name -and $_.ordinal -eq $r.ordinal -and $_.signature -ceq $r.signature -and $_.bits -ceq $r.bits})
   if($matches.Count -ne 1){throw 'scalar-final-entry-fact-not-retained'}
  }
  $record.finalFacts=$final;$record.status='APPLIED_BOUND_ORDINARY_SCALAR_PARAMETERS';$record.applied=$true;$accepted=$true
 }catch{
  if($_.Exception.Message -match 'PAUSE_REQUESTED'){throw};$record.reason=$_.Exception.Message;$record.diagnostic=$_.ScriptStackTrace
 }finally{
  if(-not $accepted){
   $script:scalarParameterContracts.Clear()
   foreach($p in $saved){$b=Join-Path (Join-Path $Root 'scalar-baseline') $p.name;if((Hash-File $b) -cne $p.sha256){throw 'scalar-baseline-drift'};[IO.File]::Copy($b,(Join-Path $Root $p.name),$true)}
  }
 }
 return $record
}
