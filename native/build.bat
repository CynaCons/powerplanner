@echo off
rem Build the PowerPlanner native COM add-in (x64) using the MSVC toolchain.
setlocal enabledelayedexpansion

set "VCVARS=C:\Program Files\Microsoft Visual Studio\2022\Community\VC\Auxiliary\Build\vcvars64.bat"
if not exist "%VCVARS%" (
	echo [build] vcvars64.bat not found at "%VCVARS%"
	exit /b 1
)
call "%VCVARS%" >nul
if errorlevel 1 ( echo [build] vcvars64 failed & exit /b 1 )

set "SRC=%~dp0PowerPlannerAddin"
set "OUT=%~dp0build"
if not exist "%OUT%" mkdir "%OUT%"

rem No precompiled header: #import + named_guids interacts badly with PCH
rem (LNK1179 duplicate COMDAT). The selectany GUID defs fold across TUs at link.
rem /MT statically links the VC++ runtime so the add-in DLL is self-contained.
rem (/MD failed to load inside POWERPNT.EXE, which cannot resolve vcruntime140.)
set "CLFLAGS=/nologo /c /EHsc /MT /std:c++17 /W3 /DUNICODE /D_UNICODE /DWIN32 /D_WINDLL /I"%SRC%""

pushd "%OUT%"

echo [build] compiling Connect.cpp
cl %CLFLAGS% /Fo"Connect.obj" "%SRC%\Connect.cpp" || ( popd & exit /b 1 )

echo [build] compiling dllmain.cpp
cl %CLFLAGS% /Fo"dllmain.obj" "%SRC%\dllmain.cpp" || ( popd & exit /b 1 )

echo [build] compiling resources
rc /nologo /I"%SRC%" /fo "PowerPlannerAddin.res" "%SRC%\PowerPlannerAddin.rc" || ( popd & exit /b 1 )

echo [build] linking PowerPlannerAddin.dll
link /nologo /DLL /MACHINE:X64 /DEF:"%SRC%\PowerPlannerAddin.def" /OUT:"PowerPlannerAddin.dll" ^
	Connect.obj dllmain.obj PowerPlannerAddin.res || ( popd & exit /b 1 )

popd
echo [build] OK -^> "%OUT%\PowerPlannerAddin.dll"
exit /b 0
