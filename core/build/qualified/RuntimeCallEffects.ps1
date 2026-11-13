# Register effects are properties of a particular library implementation.
# Application and SDK identifiers are not part of this selection.
function Get-Mx251RuntimeCallEffects([string]$ProfileRoot,[string[]]$SelectedArchives) {
 $path=Join-Path $PSScriptRoot 'RuntimeCallEffects.json'
 $contract=Get-Content -LiteralPath $path -Raw | ConvertFrom-Json -AsHashtable
 if($contract.schema -ne 1){throw 'runtime-call-effects-schema'}
 $functions=@();$libraries=@()
 foreach($relative in $SelectedArchives) {
  $archive=Join-Path $ProfileRoot $relative;$digest=Hash-File $archive
  $archives=@($contract.archives)
  $sidecarRelative=[IO.Path]::ChangeExtension($relative,'.effects.json')
  $sidecar=Join-Path $ProfileRoot $sidecarRelative
  if(Test-Path -LiteralPath $sidecar -PathType Leaf){
   $lock=Get-Content -LiteralPath (Join-Path $ProfileRoot 'profile.lock.json') -Raw | ConvertFrom-Json -AsHashtable
   $pins=@($lock.profile|Where-Object{$_.path -ceq $sidecarRelative -and $_.sha256 -ceq (Hash-File $sidecar)})
   if($pins.Count -ne 1){throw 'runtime-call-effects-sidecar-not-pinned'}
   $extra=Get-Content -LiteralPath $sidecar -Raw | ConvertFrom-Json -AsHashtable
   if($extra.schema -ne 1){throw 'runtime-call-effects-sidecar-schema'}
   $archives+=@($extra.archives)
   $libraries+=@{path=$sidecar;sha256=(Hash-File $sidecar)}
  }
  $matches=@($archives|Where-Object{$_.path -ceq $relative -and $_.sha256 -ceq $digest})
  if($matches.Count -ne 1){continue}
  $library=$matches[0];$libraries+=@{path=$archive;sha256=$digest}
  foreach($f in $library.functions) {
   $mask=[Convert]::ToInt64($f.localWrites,16)
   $functions+=@{symbol=$f.symbol;unit=('library:'+ $relative+'!'+$f.member);abi=$f.abi;exported=$true;
    localWrites=$mask;writes=$mask;calls=@($f.calls);reason=$null;failureLine=$null;
    instructions=$f.instructions;body='';aliases=@{};objects=@{}}
  }
 }
 return @{functions=$functions;libraries=$libraries;contractSha256=(Hash-File $path)}
}

function Assert-Mx251RuntimeCallEffects($Record) {
 foreach($library in $Record.libraries){
  if((Hash-File $library.path) -cne $library.sha256){throw 'runtime-call-effects-library-changed'}
 }
}
