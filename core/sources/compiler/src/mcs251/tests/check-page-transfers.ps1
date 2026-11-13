param([Parameter(Mandatory=$true)][string]$Output)
$ErrorActionPreference='Stop';Set-StrictMode -Version Latest
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/NearTransfers.ps1')
function Binding([long]$Address,[long]$Target,[string]$Op='lcall') {
 return @{unit='u0';line=$Address;op=$Op;address=(0xff0000+$Address);encodedTarget=(0xff0000+$Target);owner='_caller';target='_callee'}
}
$cases=@(
 @{name='empty';input=@();calls=0;jumps=0;rounds=1},
 @{name='next-PC-page';input=@((Binding 0x7fe 0x804));calls=1;jumps=0;rounds=1;source=0xff07fe;target=0xff0803},
 @{name='cross-page';input=@((Binding 0x7fd 0x803));calls=0;jumps=0;rounds=2},
 @{name='target-enters-page';input=@((Binding 0x7fc 0x800));calls=1;jumps=0;rounds=1;source=0xff07fc;target=0xff07ff},
 @{name='removal-moves-another-page';input=@((Binding 0x100 0x9000),(Binding 0x7fe 0x7fd));calls=0;jumps=0;rounds=3},
 @{name='mixed-forward-backward';input=@((Binding 0x100 0x200),(Binding 0x150 0x20 'ljmp'));calls=1;jumps=1;rounds=1;source=0xff0100;target=0xff01fe}
)
$results=@()
foreach($c in $cases){
 $p=Get-Mx251PagePlan $c.input
 if($p.calls -ne $c.calls -or $p.jumps -ne $c.jumps -or $p.rounds -ne $c.rounds){throw ($c.name+': page selection differs')}
 if($c.ContainsKey('source') -and ($p.changes[0].finalAddress -ne $c.source -or $p.changes[0].finalTarget -ne $c.target)){throw ($c.name+': relocated address differs')}
 $results+=@{name=$c.name;status='PASS';plan=$p}
}
$null=[IO.Directory]::CreateDirectory($Output)
[IO.File]::WriteAllText((Join-Path $Output 'result.json'),(ConvertTo-Json -InputObject $results -Depth 8))
Write-Output ('PASS '+$results.Count+' page-layout cases')
