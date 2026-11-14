param(
 [Parameter(Mandatory=$true)][string]$Output,
 [string]$Corpus,
 [string]$Core
)
$ErrorActionPreference='Stop'
$repo=Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
. (Join-Path $repo 'core/build/qualified/RuntimeCallEffects.ps1')
function Hash-File([string]$Path){(Get-FileHash -LiteralPath $Path -Algorithm SHA256).Hash.ToLowerInvariant()}
function Read-Json([string]$Path){Get-Content -LiteralPath $Path -Raw | ConvertFrom-Json -AsHashtable}
function Check([bool]$Condition,[string]$Message){if(-not $Condition){throw $Message}}

# Exercise the production parser and instruction decoder together. A setup
# write that contradicts the declaration must still appear in boundaryWrites.
$body=@'
; mx251 call-clobber save callee=_sink removed=70 writes=00000000 arguments=00008080
 mov r7,#0x12
 mov r15,#0x34
 lcall _sink
; mx251 call-clobber restore callee=_sink removed=70 writes=00000000 arguments=00008080
'@
$fn=@{symbol='_caller';unit='u0';body=$body;aliases=@{};objects=@{};sfrLayout='sph85-v1'}
$new=@(Get-Mx251OrdinaryCallUses $fn)
Check ($new.Count -eq 1 -and $new[0].argumentWrites -eq 0x8080 -and $new[0].boundaryWrites -eq 0x8080) 'new format was not decoded'
Check (-not ($new[0].boundaryWrites -band $new[0].removed)) 'valid call conflicts'
$fn.body=$body.Replace(' arguments=00008080','')
$old=@(Get-Mx251OrdinaryCallUses $fn)
Check ($old.Count -eq 1 -and $null -eq $old[0].argumentWrites -and $old[0].boundaryWrites -eq 0x8080) 'old format was not decoded'
$fn.body=$body.Replace(' mov r7,#0x12'," mov r4,#0x99`n mov r7,#0x12")
$conflict=@(Get-Mx251OrdinaryCallUses $fn)
Check (($conflict[0].boundaryWrites -band $conflict[0].removed) -eq 0x10) 'actual argument overwrite was missed'
$fn.body=$body.Replace('restore callee=_sink removed=70 writes=00000000 arguments=00008080','restore callee=_sink removed=70 writes=00000000 arguments=000080c0')
$caught=$false
try{$null=Get-Mx251OrdinaryCallUses $fn}catch{if($_.Exception.Message -cne 'clobber-unpaired-ordinary-save'){throw};$caught=$true}
Check $caught 'mismatched save/restore was accepted'

$rows=@();$violations=@();$uses=0;$abi4=0;$projectsWithUses=0
if($Corpus){
 if(-not $Core){$Core=Join-Path $Corpus 'candidate/core'}
 $matrix=Read-Json (Join-Path $Corpus 'comparison.json')
 foreach($row in $matrix.rows){
  $root=Join-Path $Corpus ('projects/'+$row.id+'/mx251/.build/mx251/latest')
  $config=Read-Json (Join-Path $root 'project.snapshot.json')
  $receipt=Read-Json (Join-Path $root 'receipt.json')
  $record=Read-Json (Join-Path $root 'call-clobber-feedback.json')
  $profileRoot=Join-Path $Core ('profiles/'+$config.target)
  $archives=@($record.runtimeLibraries|Where-Object{$_.path.EndsWith('.lib')}|ForEach-Object{[IO.Path]::GetRelativePath($profileRoot,$_.path).Replace('\','/')})
  $libraries=Get-Mx251RuntimeCallEffects $profileRoot $archives
  $scan=Get-Mx251BankFunctions $root $config $libraries.functions $receipt.targetContract.qualifiedMemory
  $count=0;$native=0;$markers=0;$projectUses=@()
  foreach($f in $scan.functions){
   $markers+=[regex]::Matches($f.body,'(?m)^;\s*mx251 call-clobber save ').Count
   foreach($use in @(Get-Mx251OrdinaryCallUses $f)){
    ++$count
    $defs=@($scan.symbols[$use.callee])
    $local=@($defs|Where-Object{$_.unit -ceq $use.unit -and -not $_.exported})
    $targets=@(if($local.Count){$local}else{$defs|Where-Object{$_.exported}})
    $writes=if($targets.Count -eq 1){$targets[0].writes}else{4294967295L}
    if($targets.Count -eq 1 -and $targets[0].abi -eq 4){++$native}
    if(($writes -band $use.removed) -or ($use.boundaryWrites -band $use.removed)){
     $violations+=@{project=$row.id;caller=$use.caller;callee=$use.callee;removed=$use.removed;calleeWrites=$writes;boundaryWrites=$use.boundaryWrites;boundaryReason=$use.boundaryReason}
    }
    $use.finalCalleeWrites=$writes;$projectUses+=@($use)
   }
  }
  Check ($markers -eq $count) ('unparsed call records: '+$row.id)
  $uses+=$count;$abi4+=$native;if($count){++$projectsWithUses}
  $rows+=@{id=$row.id;calls=$count;abi4Calls=$native;uses=$projectUses;firmwareSha256=(Hash-File (Join-Path $root 'firmware.hex'))}
 }
}
$result=@{status=$(if($violations.Count){'FAIL'}else{'PASS'});parserCases=4;projects=$rows.Count;projectsWithUses=$projectsWithUses;calls=$uses;abi4Calls=$abi4;violations=$violations;rows=$rows;scriptSha256=(Hash-File (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1'))}
$directory=Split-Path $Output -Parent
if($directory){[IO.Directory]::CreateDirectory($directory)|Out-Null}
[IO.File]::WriteAllText($Output,($result|ConvertTo-Json -Depth 40)+"`n",[Text.UTF8Encoding]::new($false))
$result|Select-Object status,parserCases,projects,projectsWithUses,calls,abi4Calls|ConvertTo-Json -Compress
Check (-not $violations.Count) ('call conflicts: '+$violations.Count)
