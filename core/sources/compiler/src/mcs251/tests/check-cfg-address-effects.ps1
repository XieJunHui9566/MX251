param([Parameter(Mandatory)][string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/CallClobberFeedback.ps1')
$null=New-Item -ItemType Directory -Path $OutputDirectory
$rows=@()
function Check([string]$Name,[string]$Body,[bool]$Known,$Aliases=@{}) {
 $effect=Get-Mx251BankBody $Body $Aliases
 if(($null -eq $effect.reason) -ne $Known){throw "$Name : $($effect | ConvertTo-Json -Compress)"}
 $script:rows+=@{name=$Name;known=$Known;body=$Body;effect=$effect}
}
$join=@'
 mov wr12,#0x007e
 mov wr14,#0xfd5b
 jz other
 mov wr14,#0xfd53
 sjmp join
other:
 mov wr14,#0xfd5f
join:
 mov dr16,dr12
 mov @dr16,r9
 eret
'@
Check 'different low bytes merge into a far interval' $join $true
Check 'one path can point at core registers' ($join.Replace('mov wr14,#0xfd5f',"mov wr12,#0x0000`n mov wr14,#0x0000")) $false
Check 'partial GPR overlap kills the high address byte' ($join.Replace('mov dr16,dr12',"mov r12,#0xff`n mov dr16,dr12")) $false
Check 'unknown incoming high half' ($join.Replace('mov wr12,#0x007e','mov wr12,wr0')) $false
Check 'known high bytes with a dynamic low half' "mov wr12,#0x007e`nmov wr14,wr0`nL:`nmov @dr12,r9" $true
Check 'copy and zero extension retain bytes' "mov r8,#0x7e`nmovz wr12,r8`nmov wr14,wr0`nL:`nmov @dr12,r9" $true
Check 'mask establishes the 24 bit upper zero' "mov dr12,dr0`nanl wr12,#0x00ff`norl wr12,#0x007e`nL:`nmov @dr12,r9" $false
Check 'unknown lower byte near the address-space end rejects a word' "mov wr12,#0x00ff`nmov wr14,wr0`nL:`nmov @dr12,wr8" $false
Check 'last legal word' "mov wr12,#0x00ff`nmov wr14,#0xfffe`nL:`nmov @dr12,wr8" $true
Check 'word crosses the address-space end' "mov wr12,#0x00ff`nmov wr14,#0xffff`nL:`nmov @dr12,wr8" $false
Check 'displaced word' "mov wr12,#0x007e`nmov wr14,#0xfe00`nL:`nmov @dr12+2,wr8" $true
Check 'displacement crosses the address-space end' "mov wr12,#0x00ff`nmov wr14,#0xfffe`nL:`nmov @dr12+2,wr8" $false
$poll="L:`nmov dptr,#0xfe53`nmov dpxl,#0x7e`nmov a,@dpx`njnb acc.0,L`nclr a`nmov @dpx,a"
Check 'polling loop retains DPX on exit' $poll $true
Check 'call invalidates address facts' ($poll.Replace('clr a',"ecall _callee`nclr a")) $false
Check 're-established address after a call' ($poll.Replace('clr a',"ecall _callee`nmov dptr,#0xfe53`nmov dpxl,#0x7e`nclr a")) $true
Check 'direct DPXL alias invalidates address facts' ($poll.Replace('clr a',"mov _DPXL,r9`nclr a")) $false @{_DPXL=132}
Check 'direct word write invalidates both DPTR bytes' ($poll.Replace('clr a',"mov 0x82,wr8`nmov dpxl,#0`nclr a")) $false
Check 'loop back edge can change address' "mov wr12,#0x007e`nmov wr14,#0xfe00`nL:`nmov @dr12,r9`nmov dr12,dr0`njnz L" $false
Check 'loop invariant address survives back edge' "mov wr12,#0x007e`nmov wr14,#0xfe00`nL:`nmov @dr12,r9`ndjnz r9,L" $true
Check 'indirect jump has no CFG proof' ($join.Replace('sjmp join','jmp @a+dptr')) $false
Check 'ordinary unknown pointer stays unknown' "mov @dr12,r9" $false
$env:MX251_CFG_ADDRESS_EFFECTS='0'
try {Check 'disabled option retains previous behavior' $join $false} finally {Remove-Item Env:MX251_CFG_ADDRESS_EFFECTS}
@{status='PASS';checks=$rows.Count;rows=$rows} | ConvertTo-Json -Depth 8 | Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8
Write-Output "PASS: $($rows.Count) CFG address and physical-alias cases"
