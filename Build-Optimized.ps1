#requires -Version 7.0
param(
 [Parameter(Mandatory=$true)][string]$ProjectPath,
 [switch]$C251Compatibility,
 [string]$CMake=(Join-Path $PSScriptRoot 'host/cmake/bin/cmake.exe'),
 [string]$Ninja=(Join-Path $PSScriptRoot 'host/ninja/ninja.exe'),
 [string]$PowerShell=(Join-Path $PSScriptRoot 'host/powershell/pwsh.exe')
)
$ErrorActionPreference='Stop'
# One policy for ordinary-wide small/stack-auto engineering projects.
# Decisions inside the build use program facts, not project or SDK names.
& (Join-Path $PSScriptRoot 'core/build/Build-CMake.ps1') `
 -ProjectPath $ProjectPath -CMake $CMake -Ninja $Ninja -PowerShell $PowerShell `
 -FunctionSharing -MachineIr -CalleePreservation -NativeIntegerRuntime `
 -SelectiveLeafAbi AUTO -C251BitComplement:$C251Compatibility
