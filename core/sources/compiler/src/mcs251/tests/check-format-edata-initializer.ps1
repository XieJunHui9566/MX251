param([string]$OutputDirectory)
$ErrorActionPreference='Stop'
$repo=[IO.Path]::GetFullPath((Join-Path $PSScriptRoot '../../../../../..'))
. (Join-Path $repo 'core/build/qualified/FormatRuntime.ps1')
$prefix=@('.module unit','.optsdcc -mmcs251','.area ESEG (XDATA)','_value::','.ds 4','.area GSINIT (CODE)')
$suffix=@('.area CSEG (CODE)','.area CONST (CODE)','_text:','.ascii "test"','.db 0')
$checks=0
function Check($code,$expected,$description){
 $actual=Test-Mx251FormatInitializerUnit ($prefix+@($code -split "`n")+$suffix)
 if($actual -ne $expected){throw $description}
 $script:checks++
}
Check "mov a,#0x12`nmov e:(_value+3),a`nclr a`nmov e:(_value+2),a" $true 'literal and zero EDATA stores'
Check "mov a,#0x12`nmov dptr,#(_value + 3)`nmov dpxl,#((_value + 3) >> 16)`nmov @dpx,a" $true 'existing far stores'
Check "mov e:(_value+3),a" $false 'value must be established'
Check "mov a,e:(_value+3)" $false 'arbitrary reads are not constant initialization'
Check "mov a,#0x12`necall _printf`nmov e:(_value+3),a" $false 'calls do not become initializers'
Check "mov a,#0x12`nmov e:(@dpx),a" $false 'only fixed symbolic stores'
Check ".area CSEG (CODE)`nmov a,#0x12`nmov e:(_value+3),a" $false 'ordinary code without function metadata'
$record=@{status='PASS';checks=$checks;scriptSha256=(Get-FileHash (Join-Path $repo 'core/build/qualified/FormatRuntime.ps1') -Algorithm SHA256).Hash.ToLowerInvariant()}
if($OutputDirectory){$null=New-Item -ItemType Directory -Path $OutputDirectory -Force;$record|ConvertTo-Json|Set-Content (Join-Path $OutputDirectory 'result.json') -Encoding utf8}
Write-Output "PASS $checks initializer classification checks"
