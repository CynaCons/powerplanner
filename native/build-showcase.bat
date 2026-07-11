@echo off
rem Build the S1 showcase harness (demo, not a gate). Inserts a mockup-mirror
rem chart into a visible PowerPoint slide and leaves it open.
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || ( echo vcvars failed & exit /b 1 )

set "SRC=%~dp0"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"

pushd "%OUT%"
echo [showcase] compiling
call "%SRC%sources.bat"
cl /nologo /EHsc /MT /std:c++17 /bigobj /DUNICODE /D_UNICODE /DWIN32 /D_WINDLL /I"%SRC%PowerPlannerAddin" ^
	"%SRC%render\showcase.cpp" %SHARED_GANTT_SRC% ^
	/Fe"ppshowcase.exe" gdi32.lib user32.lib || ( popd & exit /b 1 )
popd
echo [showcase] built "%OUT%\ppshowcase.exe"
