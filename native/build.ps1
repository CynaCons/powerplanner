<#
.SYNOPSIS
  Build (and optionally register) the PowerPlanner native COM add-in.
.EXAMPLE
  ./build.ps1                # compile only
  ./build.ps1 -Register      # compile, then per-user regsvr32 (no admin)
  ./build.ps1 -Unregister    # unregister
#>
param(
	[switch]$Register,
	[switch]$Unregister
)

$ErrorActionPreference = 'Stop'
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
$dll  = Join-Path $here 'build\PowerPlannerAddin.dll'

if ($Unregister) {
	if (Test-Path $dll) { & regsvr32.exe /s /u $dll; Write-Host "[build] unregistered $dll" }
	else { Write-Host "[build] nothing to unregister (no DLL)" }
	return
}

Write-Host "[build] compiling..."
& cmd.exe /c "`"$here\build.bat`""
if ($LASTEXITCODE -ne 0) { throw "build failed (exit $LASTEXITCODE)" }

if (-not (Test-Path $dll)) { throw "expected output missing: $dll" }
Write-Host "[build] output: $dll"

if ($Register) {
	# regsvr32 calls DllRegisterServer, which sets per-user registration -> HKCU.
	& regsvr32.exe /s $dll
	if ($LASTEXITCODE -ne 0) { throw "regsvr32 failed (exit $LASTEXITCODE)" }
	Write-Host "[build] registered (per-user) $dll"
}
