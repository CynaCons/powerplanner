@echo off
rem Build + run the native PowerPoint-free ops harness.
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || ( echo vcvars failed & exit /b 1 )

set "SRC=%~dp0"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"

pushd "%OUT%"
echo [ops] compiling ops harness
cl /nologo /EHsc /MT /std:c++17 /bigobj /DUNICODE /D_UNICODE /DWIN32 ^
	"%SRC%ops\ops-test.cpp" "%SRC%PowerPlannerAddin\GanttOps.cpp" "%SRC%PowerPlannerAddin\GanttLayout.cpp" "%SRC%PowerPlannerAddin\GanttJson.cpp" ^
	/Fe"%OUT%\ppops.exe" || ( popd & exit /b 1 )
popd

echo [ops] running
"%OUT%\ppops.exe"
