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
call "%SRC%sources.bat"
cl /nologo /EHsc /MT /std:c++17 /bigobj /DUNICODE /D_UNICODE /DWIN32 /D_WINDLL /I"%SRC%PowerPlannerAddin" ^
	"%SRC%render\appbar-shot.cpp" %SHARED_GANTT_SRC% ^
	/Fe"ppappbarshot.exe" || ( popd & exit /b 1 )
popd
echo [appbar-shot] built "%OUT%\ppappbarshot.exe"
