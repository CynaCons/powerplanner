@echo off
rem Build + run the native layout conformance harness against spec/fixtures.
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || ( echo vcvars failed & exit /b 1 )

set "SRC=%~dp0"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"

pushd "%OUT%"
echo [conf] compiling conformance harness
cl /nologo /EHsc /MT /std:c++17 /bigobj /DUNICODE /D_UNICODE /DWIN32 ^
	"%SRC%conformance\conformance.cpp" "%SRC%PowerPlannerAddin\GanttLayout.cpp" ^
	/Fe"ppconf.exe" || ( popd & exit /b 1 )
popd

echo [conf] running
"%OUT%\ppconf.exe" "%SRC%..\spec\fixtures"
