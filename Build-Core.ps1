#requires -Version 7.0
param(
 [Parameter(Mandatory=$true)][string]$ProjectPath,
 [switch]$C251Compatibility,
 [ValidateSet('AUTO','ON','OFF')][string]$NativeIntegerRuntime='AUTO',
 [ValidateSet('AUTO','OFF')][string]$NearTransfers='AUTO',
 [string]$CMake=(Join-Path $PSScriptRoot 'host/cmake/bin/cmake.exe'),
 [string]$Ninja=(Join-Path $PSScriptRoot 'host/ninja/ninja.exe'),
 [string]$PowerShell=(Join-Path $PSScriptRoot 'host/powershell/pwsh.exe')
)
$ErrorActionPreference='Stop'
$config=Get-Content -LiteralPath (Join-Path $ProjectPath 'mx251.json') -Raw -Encoding UTF8 | ConvertFrom-Json
# Native math/MDU projects already choose their integer library through runtime.
$nativeInt=$NativeIntegerRuntime -eq 'ON' -or ($NativeIntegerRuntime -eq 'AUTO' -and
 $config.runtime -eq 'standard' -and $config.model -eq 'small' -and
 $config.backend -eq 'ordinary-wide' -and -not $config.explicitAbi)
& (Join-Path $PSScriptRoot 'core/build/Build-CMake.ps1') `
 -ProjectPath $ProjectPath -CMake $CMake -Ninja $Ninja -PowerShell $PowerShell `
 -FunctionSharing -MachineIr -CalleePreservation -NativeIntegerRuntime:$nativeInt `
 -SelectiveLeafAbi AUTO -NearTransfers $NearTransfers -C251BitComplement:$C251Compatibility
