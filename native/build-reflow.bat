@echo off
rem Build the N5 reflow test harness.
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || ( echo vcvars failed & exit /b 1 )

set "SRC=%~dp0"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"

pushd "%OUT%"
echo [reflow] compiling
cl /nologo /EHsc /MT /std:c++17 /bigobj /DUNICODE /D_UNICODE /DWIN32 /D_WINDLL /I"%SRC%PowerPlannerAddin" ^
	"%SRC%render\reflow-test.cpp" "%SRC%PowerPlannerAddin\GanttBuilder.cpp" "%SRC%PowerPlannerAddin\GanttLayout.cpp" ^
	"%SRC%PowerPlannerAddin\GanttJson.cpp" "%SRC%PowerPlannerAddin\GanttOps.cpp" "%SRC%PowerPlannerAddin\GanttHitTest.cpp" "%SRC%PowerPlannerAddin\PptRenderer.cpp" "%SRC%PowerPlannerAddin\Overlay.cpp" ^
	/Fe"ppreflow.exe" gdi32.lib user32.lib || ( popd & exit /b 1 )
popd
echo [reflow] built "%OUT%\ppreflow.exe"
