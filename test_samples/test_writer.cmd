@echo off
REM ============================================================
REM test_writer.cmd - pstwriter end-to-end smoke test (no PS).
REM Convert + validate sample_mail / sample_contacts / sample_calendar.
REM Exit code 0 on full success, 1 on any failure.
REM ============================================================
setlocal EnableDelayedExpansion

set "HERE=%~dp0"
set "BUILD_DIR=d:\Work Qfion\PST Dev\build\gcc"
set "BIN=%BUILD_DIR%\bin"
set "CONVERT=%BIN%\pst_convert.exe"
set "INFO=%BIN%\pst_info.exe"
set "TESTS=%BIN%\pstwriter_tests.exe"

set FAILED=0

if not exist "%CONVERT%" goto :nobinary

echo.
echo ==^> Unit tests
"%TESTS%" --reporter compact >nul 2>&1
if errorlevel 1 echo     NOTE: some unit tests failed [usually missing golden samples in tests/golden/].

call :run mail     sample_mail.json     out_mail.pst
call :run contacts sample_contacts.json out_contacts.pst
call :run calendar sample_calendar.json out_calendar.pst

echo.
echo ============================================================
if not "%FAILED%"=="0" goto :failed

echo ALL CONVERSIONS PASSED
echo Generated PSTs in "%HERE%":
dir /b "%HERE%out_*.pst"
echo.
echo Manual gate: open each .pst in Outlook to confirm it loads.
exit /b 0

:failed
echo %FAILED% step[s] failed.
exit /b 1

:nobinary
echo Missing binary: "%CONVERT%"
echo Build first:    cmake --build "%BUILD_DIR%"
exit /b 1

:run
set "KIND=%~1"
set "JSON=%HERE%%~2"
set "PST=%HERE%%~3"
echo.
echo ==^> pst_convert %KIND%   -^> %~3
"%CONVERT%" %KIND% "%JSON%" "%PST%"
if errorlevel 1 goto :run_fail_convert

echo ==^> pst_info validate    %~3
"%INFO%" "%PST%" | findstr /C:"ALL CHECKS PASSED" >nul
if errorlevel 1 goto :run_fail_info
echo     OK
goto :eof

:run_fail_convert
set /a FAILED+=1
echo     CONVERT FAILED
goto :eof

:run_fail_info
set /a FAILED+=1
echo     pst_info did NOT report ALL CHECKS PASSED. Run manually:
echo         "%INFO%" "%PST%"
goto :eof
