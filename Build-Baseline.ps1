#requires -Version 7.0
param(
 [Parameter(Mandatory=$true)][string]$ProjectPath,
 [switch]$C251Compatibility,
 [ValidateSet('AUTO','ON','OFF')][string]$NativeIntegerRuntime='AUTO',
 [ValidateSet('AUTO','OFF')][string]$NearTransfers='AUTO',
 [string]$CMake,
 [string]$Ninja,
 [string]$PowerShell
)
$ErrorActionPreference='Stop'
$baseline=Get-Content -LiteralPath (Join-Path $PSScriptRoot 'baseline.json') -Raw -Encoding UTF8 | ConvertFrom-Json
& $baseline.entry.path @PSBoundParameters
