@echo off
setlocal
set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
call "%VCVARS%" >nul || exit /b 1

set "SRC=%~dp0"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"
if exist "%OUT%\window-classes.txt" del /q "%OUT%\window-classes.txt"

pushd "%OUT%"
cl /nologo /EHsc /MT /std:c++17 /bigobj /DUNICODE /D_UNICODE /DWIN32 /D_WINDLL /I"%SRC%PowerPlannerAddin" ^
	"%SRC%render\window-probe.cpp" ^
	/Fe"ppwindowprobe.exe" >nul || ( popd & exit /b 1 )
"%OUT%\ppwindowprobe.exe"
set "RC=%ERRORLEVEL%"
popd
exit /b %RC%
