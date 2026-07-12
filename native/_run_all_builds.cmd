@echo off
setlocal
cd /d C:\dev\public-repo\powerplanner\native

echo ===== COMMAND 1: build.bat =====
call build.bat > build1_out.txt 2>&1
set EC1=%ERRORLEVEL%
echo EXIT_CODE=%EC1%>> build1_out.txt
type build1_out.txt

echo.
echo ===== COMMAND 2: build-appbar-shot.bat =====
call build-appbar-shot.bat > build2_out.txt 2>&1
set EC2=%ERRORLEVEL%
echo EXIT_CODE=%EC2%>> build2_out.txt
type build2_out.txt

echo.
echo ===== COMMAND 3: build-overlay.bat =====
call build-overlay.bat > build3_out.txt 2>&1
set EC3=%ERRORLEVEL%
echo EXIT_CODE=%EC3%>> build3_out.txt
type build3_out.txt

echo.
echo ===== SUMMARY =====
echo build.bat exit code: %EC1%
echo build-appbar-shot.bat exit code: %EC2%
echo build-overlay.bat exit code: %EC3%

exit /b 0
