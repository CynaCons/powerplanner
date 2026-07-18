@echo off
rem Phase 13 v2.8.4: COM-free pure unit seed (ppunit.exe).
setlocal
call "C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat" >nul || ( echo vcvars failed & exit /b 1 )

set "SRC=%~dp0"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"

pushd "%OUT%"
echo [unit] compiling ppunit
cl /nologo /EHsc /MT /std:c++17 /bigobj /DUNICODE /D_UNICODE /DWIN32 ^
	"%SRC%unit\ppunit.cpp" "%SRC%PowerPlannerAddin\GanttOps.cpp" "%SRC%PowerPlannerAddin\GanttLayout.cpp" "%SRC%PowerPlannerAddin\GanttJson.cpp" "%SRC%PowerPlannerAddin\GanttHitTest.cpp" ^
	/Fe"%OUT%\ppunit.exe" || ( popd & exit /b 1 )
popd

echo [unit] running
pushd "%SRC%.."
"%OUT%\ppunit.exe"
set RC=%ERRORLEVEL%
popd
if not %RC%==0 exit /b %RC%
echo [unit] OK
exit /b 0
