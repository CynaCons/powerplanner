@echo off
rem Register the PowerPlanner native add-in for the current user (no admin needed).
rem Works next to a prebuilt PowerPlannerAddin.dll, or against the build\ output.
setlocal
set "DLL=%~dp0PowerPlannerAddin.dll"
if not exist "%DLL%" set "DLL=%~dp0build\PowerPlannerAddin.dll"
if not exist "%DLL%" (
	echo [register] PowerPlannerAddin.dll not found next to this script or in build\.
	echo            Build it first ^(build.bat^) or copy the DLL next to this script.
	exit /b 1
)
regsvr32 /s "%DLL%"
if errorlevel 1 ( echo [register] FAILED & exit /b 1 )
echo [register] OK: %DLL%
echo [register] Start PowerPoint - the "PowerPlanner" tab should appear on the ribbon.
