@echo off
rem Build the N4 overlay test harness (PowerPoint automation + screen capture).
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || ( echo vcvars failed & exit /b 1 )

set "SRC=%~dp0"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"

pushd "%OUT%"
echo [overlay] compiling
cl /nologo /EHsc /MT /std:c++17 /bigobj /DUNICODE /D_UNICODE /DWIN32 /D_WINDLL /I"%SRC%PowerPlannerAddin" ^
	"%SRC%render\overlay-test.cpp" "%SRC%PowerPlannerAddin\Overlay.cpp" ^
	"%SRC%PowerPlannerAddin\GanttBuilder.cpp" "%SRC%PowerPlannerAddin\GanttLayout.cpp" "%SRC%PowerPlannerAddin\GanttJson.cpp" "%SRC%PowerPlannerAddin\GanttOps.cpp" "%SRC%PowerPlannerAddin\PptRenderer.cpp" ^
	/Fe"ppoverlay.exe" || ( popd & exit /b 1 )
popd
echo [overlay] built "%OUT%\ppoverlay.exe"
