@echo off
rem Unregister the PowerPlanner native add-in for the current user.
setlocal
set "DLL=%~dp0PowerPlannerAddin.dll"
if not exist "%DLL%" set "DLL=%~dp0build\PowerPlannerAddin.dll"
if not exist "%DLL%" ( echo [unregister] PowerPlannerAddin.dll not found. & exit /b 1 )
regsvr32 /s /u "%DLL%"
echo [unregister] done: %DLL%
