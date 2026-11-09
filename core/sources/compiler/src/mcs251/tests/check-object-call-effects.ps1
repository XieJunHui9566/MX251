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
 $config=@{units=$units};$objects=Get-Mx251BankObjects $OutputDirectory $config
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
 $scan=Get-Mx251BankFunctions $OutputDirectory $config
 foreach($name in @('_store_state','_chain_store','_live_objects','_live_registers','_store_absolute')){
  Check ($scan.symbols[$name][0].writes -ne 4294967295L) "complete call effects for $name"
 }
 $aliases=New-Mx251OrdinalMap
 $cases=@(
  @('mov ((_state+0x0004)+0),dr12',0),
  @('mov (_state+7),r9',0),
  @('mov (_state+7),wr12',4294967295L),
  @('mov (_state-1),r9',4294967295L),
  @('mov (_state+8),r9',4294967295L),
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
 # A colliding external object is not an alternative definition to choose.
 Copy-Item store.rel collision.rel
 $duplicate=Get-Mx251BankObjects $OutputDirectory @{units=$units+@(@{name='collision';file='collision.asm'})}
 Check (-not $duplicate.user.ContainsKey('_state')) 'duplicate exported definition cannot supply object facts'
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
