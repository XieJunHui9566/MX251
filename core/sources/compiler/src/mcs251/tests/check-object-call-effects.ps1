# Real compiler objects, cross-unit closure and physical register aliases.
param([Parameter(Mandatory)][string]$Sdcc,[Parameter(Mandatory)][string]$OutputDirectory,[string]$Baseline)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
$Sdcc=[IO.Path]::GetFullPath($Sdcc)
$OutputDirectory=[IO.Path]::GetFullPath($OutputDirectory)
$null=New-Item -ItemType Directory -Path $OutputDirectory
$units=@()
Push-Location $OutputDirectory
try {
 foreach($name in @('store','user')){
  $file='object-call-effects-'+$name+'.c';Copy-Item (Join-Path $PSScriptRoot $file) $file
  & $Sdcc -mmcs251 --model-small --stack-auto --mcs251-wide-ordinary --mcs251-function-metadata -c $file -o ($name+'.rel') 2> ($name+'.log')
  if($LASTEXITCODE){throw "Compile failed: $name"}
  $units+=@{name=$name;file=$file}
 }
 $config=@{units=$units};$layout=@{stackBase=1024L};$objects=Get-Mx251BankObjects $OutputDirectory $config $layout
 function Check($condition,[string]$description){if(-not $condition){throw $description};$script:checks++}
 $checks=0
 if($Baseline){
  foreach($u in $units){
   & $Baseline -mmcs251 --model-small --stack-auto --mcs251-wide-ordinary --mcs251-function-metadata -c $u.file -o ($u.name+'-parent.rel') 2> ($u.name+'-parent.log')
   if($LASTEXITCODE){throw "Parent compile failed: $($u.name)"}
   Check ((Get-FileHash ($u.name+'.rel')).Hash -ceq (Get-FileHash ($u.name+'-parent.rel')).Hash) "metadata does not change code before feedback: $($u.name)"
  }
 }
 Check ($objects.user._state.size -eq 8) 'extern struct resolves to allocated definition and full extent'
 Check ($objects.user._ready.space -ceq 'bit') 'extern bit retains BIT space'
 Check ($objects.user._local_byte.unit -ceq 'user' -and $objects.store._local_byte.unit -ceq 'store') 'same-named static objects remain local'
 Check (-not $objects.store.ContainsKey('_absolute_byte')) '__at object is numeric, not relocatable DATA'
 $scan=Get-Mx251BankFunctions $OutputDirectory $config @() $layout
 foreach($name in @('_store_state','_chain_store','_live_objects','_live_registers','_store_absolute','_store_edata','_chain_edata')){
  Check ($scan.symbols[$name][0].writes -ne 4294967295L) "complete call effects for $name"
 }
 Check ($objects.user._estate.space -ceq 'edata' -and $objects.user._estate.size -eq 8) 'EDATA exported allocation extent'
 Check ($objects.user._estate.addressLow -eq 256 -and $objects.user._estate.addressHigh -eq 1016) 'EDATA linker allocation window'
 Check ($objects.user._elocal.unit -ceq 'user' -and $objects.store._elocal.unit -ceq 'store') 'EDATA same-named statics stay local'
 Check (-not $objects.store.ContainsKey('_eabsolute')) '__at EDATA is not a relocatable allocation'
 foreach($window in @($null,@{},@{stackBase=263L},@{stackBase=65537L})){
  Check (-not (Get-Mx251BankObjects $OutputDirectory $config $window).user.ContainsKey('_estate')) 'EDATA needs a fitting linker window'
 }
 Check ((Get-Mx251BankObjects $OutputDirectory $config @{stackBase=264L}).user._estate.addressHigh -eq 256) 'EDATA exact-fit allocation'
 $aliases=New-Mx251OrdinalMap
 $aliases['_F0']=213L;$aliases['_PSW']=208L
 $cases=@(
  @('mov ((_state+0x0004)+0),dr12',0),
  @('mov (_state+7),r9',0),
  @('mov (_state+7),wr12',4294967295L),
  @('mov (_state-1),r9',4294967295L),
  @('mov (_state+8),r9',4294967295L),
  @('mov e:((_estate+4)+0),dr12',0),
  @('mov e:(_estate+7),r9',0),
  @('mov e:(_estate+7),wr12',4294967295L),
  @('mov e:(_estate-1),r9',4294967295L),
  @('mov e:(_estate+8),r9',4294967295L),
  @('mov e:_state,r9',4294967295L),
  @('mov _estate,r9',4294967295L),
  @('setb _F0',0), @('mov _F0,c',0),
  @('mov _PSW,#0',4294967295L),
  @('clr _ready',0),
  @('mov _ready,r9',4294967295L),
  @('setb _state',4294967295L),
  @('mov 0x00,#1',1), @('mov 0x07,#1',128),
  @('mov 0x08,#1',0), @('mov 0x1f,#1',0),
  @('mov 0xe0,#1',2048), @('mov 0xf0,#1',1024),
  @('setb 0xe3',2048), @('clr 0xf2',1024),
  @('mov 0xd0,#8',4294967295L), @('setb 0xd3',4294967295L),
  @('mov @dr12,r9',4294967295L)
 )
 foreach($case in $cases){
  $effect=Get-Mx251BankBody $case[0] $aliases $objects.user
  Check ($effect.writes -eq $case[1]) "register effects: $($case[0]) = $($effect.writes)"
 }
 # All PSW flag spellings preserve the bank; RS0/RS1 do not.
 foreach($bit in 0..7){
  $aliases['_flag']=208L+$bit
  $expected=if($bit -in @(3,4)){4294967295L}else{0L}
  foreach($operand in @('_flag',('0x{0:x}' -f (208+$bit)),('_PSW.'+$bit))){
   foreach($op in @('setb','clr','cpl')){
    $effect=Get-Mx251BankBody "$op $operand" $aliases $objects.user
    Check ($effect.writes -eq $expected) "PSW bit effect: $op $operand"
   }
  }
 }
 $effect=Get-Mx251BankBody "mov dr12,#_estate`nL:`nmov @dr12,r9" $aliases $objects.user
 Check ($null -eq $effect.reason -and $effect.writes -eq 61440) 'EDATA symbolic address survives a basic-block boundary'
 # A colliding external object is not an alternative definition to choose.
 Copy-Item store.rel collision.rel
 $duplicate=Get-Mx251BankObjects $OutputDirectory @{units=$units+@(@{name='collision';file='collision.asm'})} $layout
 Check (-not $duplicate.user.ContainsKey('_state') -and -not $duplicate.user.ContainsKey('_estate')) 'duplicate exported definition cannot supply object facts'
 Check ($duplicate.user.ContainsKey('_local_byte')) 'duplicate exported definition does not erase local objects'
 # DATA requires a register-bank reservation; BIT objects do not alias GPRs.
 foreach($u in $units){
  $path=Join-Path $OutputDirectory ($u.name+'.rel');$text=[IO.File]::ReadAllText($path)
  [IO.File]::WriteAllText($path,$text.Replace('A REG_BANK_0 size 8 flags 4 addr 0','A REG_BANK_0 size 0 flags 4 addr 0'))
 }
 $unreserved=Get-Mx251BankObjects $OutputDirectory $config
 Check (-not $unreserved.user.ContainsKey('_state') -and $unreserved.user.ContainsKey('_ready')) 'register bank reservation governs DATA facts'
 @{status='PASS';checks=$checks;compilerSha256=(Get-FileHash $Sdcc -Algorithm SHA256).Hash.ToLowerInvariant()} | ConvertTo-Json | Set-Content results.json -Encoding utf8
 Write-Output "PASS: $checks object, call-closure and register-alias checks"
} finally {Pop-Location}
