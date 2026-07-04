@echo off
rem Build (if needed), register, and launch PowerPoint with the PowerPlanner add-in.
rem Usage: start.bat          - use the existing DLL (builds only if missing)
rem        start.bat build    - force a fresh build first
setlocal
set "HERE=%~dp0"
set "DLL=%HERE%build\PowerPlannerAddin.dll"

rem A running PowerPoint keeps the old DLL loaded; it must restart to pick up a new one.
tasklist /fi "imagename eq POWERPNT.EXE" | find /i "POWERPNT.EXE" >nul
if not errorlevel 1 (
	echo [start] PowerPoint is already running and must be closed to load the fresh DLL.
	choice /m "Close PowerPoint now (unsaved changes will be lost)"
	if errorlevel 2 exit /b 1
	taskkill /f /im POWERPNT.EXE >nul 2>&1
	timeout /t 2 /nobreak >nul
)

if /i "%~1"=="build" call "%HERE%build.bat" || exit /b 1
if not exist "%DLL%" (
	echo [start] DLL not found - building first...
	call "%HERE%build.bat" || exit /b 1
)

call "%HERE%register.bat" || exit /b 1

echo [start] Launching PowerPoint...
start "" powerpnt.exe
echo [start] Look for the "PowerPlanner" tab on the ribbon, then Insert Gantt.
