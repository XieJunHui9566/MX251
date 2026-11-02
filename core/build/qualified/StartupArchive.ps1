function Read-Mx251StartupArchive([string]$Path) {
 $bytes=[IO.File]::ReadAllBytes($Path)
 if($bytes.Length -lt 8 -or [Text.Encoding]::ASCII.GetString($bytes,0,8) -cne "!<arch>`n"){throw "Unsupported archive format: $Path"}
 $out=[Collections.Generic.List[object]]::new();$pos=8;$longNames=$null
 while($pos -lt $bytes.Length) {
  if($pos+60 -gt $bytes.Length){throw "Truncated archive header: $Path"}
  $h=[Text.Encoding]::ASCII.GetString($bytes,$pos,60)
  if($h.Substring(58,2) -cne ('`'+"`n")){throw "Archive header trailer: $Path"}
  $size=0;if(-not [int]::TryParse($h.Substring(48,10).Trim(),[ref]$size) -or $size -lt 0 -or $pos+60+$size -gt $bytes.Length){throw "Archive member size: $Path"}
  $name=$h.Substring(0,16).Trim();$start=$pos+60;$length=$size
  if($name -eq '//'){$longNames=[Text.Encoding]::ASCII.GetString($bytes,$start,$length)}
  elseif($name -ne '/' -and $name -ne '/SYM64/') {
   if($name.StartsWith('#1/')) {$n=[int]$name.Substring(3);if($n -gt $length){throw "Archive BSD name: $Path"};$name=[Text.Encoding]::ASCII.GetString($bytes,$start,$n).TrimEnd([char]0);$start+=$n;$length-=$n}
   elseif($name -match '^/([0-9]+)$') {if($null -eq $longNames){throw "Archive long name table missing: $Path"};$offset=[int]$Matches[1];if($offset -ge $longNames.Length){throw "Archive name offset: $Path"};$name=$longNames.Substring($offset).Split("`n")[0].TrimEnd('/')}
   else {$name=$name.TrimEnd('/')}
   if($name -notin @('__.SYMDEF','__.SYMDEF SORTED')) {
    $data=[byte[]]::new($length);[Array]::Copy($bytes,$start,$data,0,$length)
    $out.Add(@{name=$name;data=$data;sha256=(Get-Mx251StartupDigest $data)})
   }
  }
  $pos+=60+$size+($size%2)
 }
 return @($out.ToArray())
}

