@echo off
setlocal EnableExtensions
REM install.bat - install/upgrade litewin_exporter as a Windows service.
REM
REM Usage: install.bat [PORT] [/defender]
REM   PORT        TCP port for /metrics (default 9182)
REM   /defender   also add a Windows Defender exclusion for the installed exe
REM
REM Run elevated. Must sit next to litewin_exporter_amd64.exe and
REM litewin_exporter_386.exe (the release bundle). Idempotent: safe to re-run.

set "SVC=litewin_exporter"
set "PORT=9182"
set "DEFENDER=0"
set "INSTALLDIR=%ProgramFiles%\litewin_exporter"
set "EXE=%INSTALLDIR%\litewin_exporter.exe"

REM -- parse args (PORT is the first non-switch arg) --
:parseargs
if "%~1"=="" goto argsdone
if /I "%~1"=="/defender" (set "DEFENDER=1") else (set "PORT=%~1")
shift
goto parseargs
:argsdone

REM -- require elevation --
net session >nul 2>&1
if errorlevel 1 (
    echo ERROR: must run as Administrator ^(elevated cmd^).
    exit /b 1
)

REM -- pick the arch-matching exe from this bundle --
set "ARCH=386"
if /I "%PROCESSOR_ARCHITECTURE%"=="AMD64" set "ARCH=amd64"
if /I "%PROCESSOR_ARCHITEW6432%"=="AMD64" set "ARCH=amd64"
set "SRCEXE=%~dp0litewin_exporter_%ARCH%.exe"
if not exist "%SRCEXE%" (
    echo ERROR: %SRCEXE% not found next to install.bat.
    exit /b 1
)

echo === litewin_exporter install ^(arch=%ARCH%, port=%PORT%^) ===

REM -- stop and remove any existing service first (idempotent upgrade) --
sc stop "%SVC%" >nul 2>&1
REM wait briefly for the service to release the exe before overwriting
ping -n 3 127.0.0.1 >nul
sc delete "%SVC%" >nul 2>&1

REM -- copy the exe into place --
if not exist "%INSTALLDIR%" mkdir "%INSTALLDIR%"
copy /Y "%SRCEXE%" "%EXE%" >nul
if errorlevel 1 (
    echo ERROR: failed to copy exe to %EXE% ^(is the service still running?^).
    exit /b 1
)

REM -- create the service (auto-start). binPath keeps the exe path quoted so
REM    a Program Files space is handled; sc's own parser reads the \" escapes. --
sc create "%SVC%" binPath= "\"%EXE%\" --web.listen-address :%PORT%" start= auto DisplayName= "litewin_exporter (Prometheus)" >nul
if errorlevel 1 (
    echo ERROR: sc create failed.
    exit /b 1
)
sc description "%SVC%" "Lightweight Prometheus metrics exporter for Windows." >nul 2>&1

REM -- firewall: open the metrics port. Delete-then-add makes it idempotent.
REM    (The PowerShell fleet installer re-scopes this rule to the scraper IPs.) --
netsh advfirewall firewall delete rule name="litewin_exporter" >nul 2>&1
netsh advfirewall firewall add rule name="litewin_exporter" dir=in action=allow protocol=TCP localport=%PORT% >nul

REM -- optional Defender exclusion --
if "%DEFENDER%"=="1" (
    echo Adding Windows Defender exclusion for %EXE%
    powershell -NoProfile -Command "Add-MpPreference -ExclusionPath '%EXE%'" >nul 2>&1
)

REM -- start it --
sc start "%SVC%" >nul
if errorlevel 1 (
    echo WARNING: sc start reported an error; check 'sc query %SVC%'.
)

echo Done. Service '%SVC%' installed, auto-start, listening on :%PORT%.
echo Test locally: curl http://127.0.0.1:%PORT%/metrics
exit /b 0
