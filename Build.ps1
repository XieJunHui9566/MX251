#requires -Version 7.0
param(
 [Parameter(Mandatory=$true)][string]$ProjectPath,
 [ValidateSet('ON','OFF')][string]$FunctionGc='ON',
 [string[]]$FunctionGcKeep=@(),
 [ValidateSet('128','256','512')][string]$WideValueLimit='512',
 [ValidateSet('512','1024','2048')][string]$WideNodeLimit='2048',
 [switch]$C251BitComplement,
 [switch]$LeafCallFeedback,
 [switch]$VolatileDataWide,
 [switch]$ConstantRotateFusion,
 [switch]$WordCompareImmediate
)
$ErrorActionPreference='Stop'
& (Join-Path $PSScriptRoot 'core/build/Build-CMake.ps1') @PSBoundParameters `
 -CMake (Join-Path $PSScriptRoot 'host/cmake/bin/cmake.exe') `
 -Ninja (Join-Path $PSScriptRoot 'host/ninja/ninja.exe') `
 -PowerShell (Join-Path $PSScriptRoot 'host/powershell/pwsh.exe')
