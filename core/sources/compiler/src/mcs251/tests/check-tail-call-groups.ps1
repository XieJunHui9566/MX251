param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/FunctionGc.ps1')
. (Join-Path $repo 'core/build/qualified/NearTransfers.ps1')
$null=[IO.Directory]::CreateDirectory($Output)
function F($name,$body,$targets=@(),$signature='v()') {
 $calls=@($targets|ForEach-Object{"; mx251-near-call schema=1 owner=$name target=$_ signature=v()"}) -join "`n"
 return ".globl $name`n; mx251-function begin schema=1 symbol=$name isr=0 addressTaken=0 opaque=0 indirect=0`n; mx251-near-definition schema=1 symbol=$name signature=$signature`n$calls`n${name}:`n$body`n; mx251-bank-contract schema=1 symbol=$name abi=2 bank=0 naked=0`n; mx251-function end schema=1 symbol=$name`n"
}
$caller=F '_caller' "ecall _front`neret" @('_front') 'v(u16,u16)'
$front=F '_front' 'ejmp _middle' @('_middle')
$middle=F '_middle' 'ejmp _last' @('_last')
$last=F '_last' 'eret'
$table=@'
mov a,#(table>>16)
movz wr16,a
mov wr18,#table
add dr16,dr20
ejmp @dr16
; mx251-jump-table schema=1 owner=_front label=table entries=2 stride=4
table:
ejmp _middle
ejmp _last
'@
$tableFront=F '_front' $table @('_middle','_last')
# Here-strings may have CRLF when checked out on Windows.
$legacy=$table -replace '(?s)^.*?; mx251-jump-table',"mov dptr,#table`nmov dpxl,#(table >> 16)`njmp @a+dptr`n; mx251-jump-table"
$legacy=$legacy.Replace('stride=4','stride=4 dispatch=a-dptr')
$legacyFront=F '_front' $legacy @('_middle','_last')
$bytes=''
foreach($base in @('lo','hi','ext')){
 $bytes+="mov a,b`nmov dptr,#$base`nmov dpxl,#($base >> 16)`nmovc a,@a+dptr`n"
 if($base -cne 'ext'){$bytes+="push acc`n"}
}
$bytes+=@'
mov dpxl,a
pop dph
pop dpl
mov dr28,dpx
ejmp @dr28
; mx251-address-table schema=1 owner=_front low=lo high=hi ext=ext entries=2
lo:
.db case0
.db case1
hi:
.db case0>>8
.db case1>>8
ext:
.db case0>>16
.db case1>>16
case0:
ejmp _middle
case1:
ejmp _last
'@
$bytesFront=F '_front' $bytes @('_middle','_last')
$cases=@(
 @{name='cross-unit-chain';front=$front;middle=$middle;last=$last;calls=1;tails=2;fixed=0;members=3},
 @{name='repeated-local-labels';front=$front;middle=(F '_middle' "ejmp 00101`$`nnop`n00101`$:`nejmp _last" @('_last'));last=(F '_last' "ejmp 00101`$`nnop`n00101`$:`neret");sameUnit=$true;calls=1;tails=2;fixed=0;members=3},
 @{name='tail-cycle';front=$front;middle=(F '_middle' "ejmp _front`neret" @('_front'));last='';calls=1;tails=2;fixed=0;members=2},
 @{name='addressed-member';front=$front;middle=$middle;last=$last.Replace('addressTaken=0','addressTaken=1');calls=0;tails=0;fixed=0;members=0},
 @{name='stack-member';front=$front;middle=$middle.Replace('symbol=_middle signature=v()','symbol=_middle signature=v(u16,u16)');last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='external-tail';front=$front;middle=$middle.Replace('ejmp _last','ejmp _external');last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='untyped-tail';front=$front.Replace('mx251-near-call','missing-call');middle=$middle;last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='excluded-member';front=$front;middle=$middle;last=$last;exclude=@('_last');calls=0;tails=0;fixed=0;members=0},
 @{name='opaque-member';front=$front;middle=$middle.Replace('opaque=0','opaque=1');last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='unknown-indirect';front=$front;middle=$middle.Replace('ejmp _last','ejmp @dr16');last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='fixed-table';front=$tableFront;middle=$middle;last=$last;calls=1;tails=1;fixed=2;members=3},
 @{name='table-missing-entry';front=$tableFront.Replace('entries=2','entries=3');middle=$middle;last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='table-wrong-base';front=$tableFront.Replace('wr18,#table','wr18,#elsewhere');middle=$middle;last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='table-short-slot';front=$tableFront.Replace('ejmp _last','ljmp _last');middle=$middle;last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='conditional-external';front=$front.Replace('ejmp _middle','je _middle');middle=$middle;last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='legacy-fixed-table';front=$legacyFront;middle=$middle;last=$last;calls=1;tails=1;fixed=2;members=3},
 @{name='legacy-wrong-extension';front=$legacyFront.Replace('(table >> 16)','(elsewhere >> 16)');middle=$middle;last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='legacy-short-slot';front=$legacyFront.Replace('ejmp _last','sjmp _last');middle=$middle;last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='legacy-address-bytes';front=$bytesFront;middle=$middle;last=$last;calls=1;tails=3;fixed=0;members=3},
 @{name='legacy-mismatched-byte';front=$bytesFront.Replace('.db case0>>8','.db case1>>8');middle=$middle;last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='legacy-external-byte';front=$bytesFront.Replace('.db case0','.db _middle');middle=$middle;last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='legacy-extra-directive';front=$bytesFront.Replace('case0:',".db 0`ncase0:");middle=$middle;last=$last;calls=0;tails=0;fixed=0;members=0},
 @{name='legacy-unknown-jump';front=$bytesFront.Replace('case0:',"ejmp @dr16`ncase0:");middle=$middle;last=$last;calls=0;tails=0;fixed=0;members=0}
)
$results=@()
foreach($c in $cases){
 $middleText=$c.middle;$lastText=$c.last
 if($c.ContainsKey('sameUnit')){$middleText+=$lastText;$lastText=''}
 foreach($unit in @(@{name='u0';text=$caller+$c.front},@{name='u1';text=$middleText},@{name='u2';text=$lastText})){
  [IO.File]::WriteAllText((Join-Path $Output ($unit.name+'.asm')),$unit.text,[Text.UTF8Encoding]::new($false))
 }
 $config=@{units=@(@{name='u0';file='a.c'},@{name='u1';file='b.c'},@{name='u2';file='c.c'})}
 $units=@(Get-Mx251NearUnits $Output $config);$exclude=if($c.ContainsKey('exclude')){$c.exclude}else{@()}
 $p=Get-Mx251NearPlan $units $exclude
 if($p.calls -ne $c.calls -or $p.tailJumps -ne $c.tails -or $p.fixedTailJumps -ne $c.fixed -or $p.functions.Count -ne $c.members){throw ($c.name+': unexpected selection '+(ConvertTo-Json $p -Depth 5 -Compress))}
 foreach($u in $units){foreach($change in $p.changes|Where-Object{$_.unit -ceq $u.name}){if($u.functions[$change.owner].fixedJumps.Contains($change.line)){throw ($c.name+': table slot changed')}}}
 $results+=@{name=$c.name;status='PASS';calls=$p.calls;tailJumps=$p.tailJumps;fixedTailJumps=$p.fixedTailJumps;functions=$p.functions.Count}
}
[IO.File]::WriteAllText((Join-Path $Output 'result.json'),(ConvertTo-Json -InputObject $results -Depth 6))
Write-Output ('PASS '+$results.Count+' tail-group cases')
