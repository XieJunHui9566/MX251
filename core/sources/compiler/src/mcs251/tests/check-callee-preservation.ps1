param([Parameter(Mandatory)][string]$OutputDirectory,[string]$CompiledDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
$null=New-Item -ItemType Directory -Path $OutputDirectory
$body=@'
_f:
; mx251-callee-preserved schema=1 saved=ff00f300
 push wr8
 push dr12
 push dr24
 push dr28
 add spx,#6
 mov @spx-0x0005,wr24
 ecall _unknown
 cmp wr0,#3
 jc L
 mov wr12,#42
L:
 sub spx,#6
 pop dr28
 pop dr24
 pop dr12
 pop wr8
 eret
'@
$cases=@(@{name='balanced-join';body=$body;expected=4278252288L})
$mutations=@{
 'missing-pop'=@(' pop dr24', '')
 'wrong-register'=@(' pop dr24', ' pop dr20')
 'overwritten-save'=@('@spx-0x0005','@spx-0x0006')
 'early-return'=@(' jc L'," jc Early`nEarly:`n eret")
 'skip-entry'=@(' jc L',' jc _f')
 'unbalanced-join'=@(' mov wr12,#42',' push wr12')
 'tail-exit'=@(' eret',' ejmp _unknown')
 'partial-mask'=@('ff00f300','ff00f100')
 'unknown-sp'=@(' add spx,#6',' mov spx,dr20')
 'pop-sp'=@(' add spx,#6'," add spx,#6`n pop spx")
 'write-after-restore'=@(' pop wr8'," pop wr8`n mov wr8,#3")
}
foreach($name in $mutations.Keys){$m=$mutations[$name];$cases+=@{name=$name;body=$body.Replace($m[0],$m[1]);expected=0L}}
$results=@()
foreach($c in $cases){
 $value=Get-Mx251CalleePreservation $c.body '_f'
 if($value -ne $c.expected){throw ($c.name+': '+$value)}
 $results+=@{name=$c.name;preserved=$value}
}
$compiled=@()
if($CompiledDirectory){
 foreach($path in Get-ChildItem $CompiledDirectory -Recurse -Filter '*.asm') {
  $text=[IO.File]::ReadAllText($path.FullName)
  foreach($m in [regex]::Matches($text,'(?ms)^; mx251-function begin schema=1 symbol=(\S+)[^\n]*\n(.*?)^; mx251-function end')) {
   if($m.Groups[2].Value -notmatch 'mx251-callee-preserved'){continue}
   $mask=Get-Mx251CalleePreservation $m.Groups[2].Value $m.Groups[1].Value
   if($m.Groups[1].Value -ceq '_dispatch') {
    if($mask){throw 'Computed jump tables do not export preservation yet'}
    continue
   }
   if($mask -ne 4278252288L){throw ('Compiled preservation not established: '+$path.FullName+' '+$m.Groups[1].Value)}
   $compiled+=@{file=$path.FullName;symbol=$m.Groups[1].Value;preserved=$mask}
  }
 }
 if(-not $compiled.Count){throw 'No compiled preserved functions'}
}
@{status='PASS';cases=$results;compiled=$compiled}|ConvertTo-Json -Depth 8|Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
Write-Output ('PASS: '+$cases.Count+' stack/control-flow cases, '+$compiled.Count+' compiled functions')
