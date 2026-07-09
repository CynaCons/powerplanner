@echo off
rem Build + run the S2 app-bar screenshot harness (demo/review tooling, NOT a gate).
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || ( echo vcvars failed & exit /b 1 )

set "SRC=%~dp0"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"

pushd "%OUT%"
echo [appbar-shot] compiling
cl /nologo /EHsc /MT /std:c++17 /bigobj /DUNICODE /D_UNICODE /DWIN32 /D_WINDLL /I"%SRC%PowerPlannerAddin" ^
	"%SRC%render\appbar-shot.cpp" "%SRC%PowerPlannerAddin\Overlay.cpp" ^
	"%SRC%PowerPlannerAddin\GanttBuilder.cpp" "%SRC%PowerPlannerAddin\GanttLayout.cpp" "%SRC%PowerPlannerAddin\GanttJson.cpp" "%SRC%PowerPlannerAddin\GanttOps.cpp" "%SRC%PowerPlannerAddin\GanttHitTest.cpp" "%SRC%PowerPlannerAddin\PptRenderer.cpp" ^
	/Fe"ppappbarshot.exe" || ( popd & exit /b 1 )
popd
echo [appbar-shot] built "%OUT%\ppappbarshot.exe"
