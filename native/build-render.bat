@echo off
rem Build the render harness (PowerPoint automation -> slide PNG).
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || ( echo vcvars failed & exit /b 1 )

set "SRC=%~dp0"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"

pushd "%OUT%"
echo [render] compiling
cl /nologo /EHsc /MT /std:c++17 /bigobj /DUNICODE /D_UNICODE /DWIN32 /D_WINDLL /I"%SRC%PowerPlannerAddin" ^
	"%SRC%render\render-harness.cpp" "%SRC%PowerPlannerAddin\GanttBuilder.cpp" "%SRC%PowerPlannerAddin\GanttLayout.cpp" "%SRC%PowerPlannerAddin\GanttJson.cpp" ^
	/Fe"pprender.exe" || ( popd & exit /b 1 )
popd
echo [render] built "%OUT%\pprender.exe"
