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
call "%SRC%sources.bat"
cl /nologo /EHsc /MT /std:c++17 /bigobj /DUNICODE /D_UNICODE /DWIN32 /D_WINDLL /I"%SRC%PowerPlannerAddin" ^
	"%SRC%render\reflow-test.cpp" %SHARED_GANTT_SRC% ^
	/Fe"ppreflow.exe" gdi32.lib user32.lib || ( popd & exit /b 1 )
popd
echo [reflow] built "%OUT%\ppreflow.exe"
