# A naked ISR consisting only of EJMP to a compiler-owned vector has no hidden calls.
function Add-Mx251SparseVectorSlots($Slots) {
 if($Slots.Count){$last=[int](($Slots|Measure-Object -Maximum).Maximum);for($i=0;$i -le $last;$i++){$null=$Slots.Add($i)}}
}
function Get-Mx251VectorRedirects([string]$Text,[long]$CodeBase,$Slots) {
 $result=@{}
 if($CodeBase -lt 0 -or -not $Slots){return $result}
 $pattern='(?ms)^; mx251-function begin schema=1 symbol=(\S+) isr=1 addressTaken=[01] opaque=1 indirect=0\r?\n(.*?)^; mx251-function end schema=1 symbol=\1\r?$'
 foreach($m in [regex]::Matches($Text,$pattern)){
  $symbol=$m.Groups[1].Value;$body=$m.Groups[2].Value
  $contract='(?m)^;\s*mx251-bank-contract schema=1 symbol='+[regex]::Escape($symbol)+' abi=2 bank=0 naked=1\s*$'
  if(-not [regex]::IsMatch($body,$contract)){continue}
  $code=@($body -split '\r?\n'|ForEach-Object{($_ -split ';',2)[0].Trim()}|Where-Object{$_})
  if($code.Count -ne 2 -or $code[0] -cne ($symbol+':') -or $code[1] -cnotmatch '^ejmp\s+(0x[0-9a-fA-F]+|[0-9]+)$'){continue}
  $literal=$Matches[1]
  $address=if($literal.StartsWith('0x')){[Convert]::ToInt64($literal.Substring(2),16)}else{[long]$literal}
  $offset=$address-$CodeBase-3
  if($offset -lt 0 -or $offset -gt 8*255 -or $offset%8 -ne 0 -or -not $Slots.Contains([int]($offset/8))){continue}
  $result[$symbol]=$address
 }
 return $result
}
