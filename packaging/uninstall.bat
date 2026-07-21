@echo off
setlocal EnableExtensions
REM uninstall.bat - remove the litewin_exporter service and everything
REM install.bat / the fleet installer added. Idempotent: safe to re-run, and
REM safe even if some pieces were never installed. Run elevated.

set "SVC=litewin_exporter"
set "INSTALLDIR=%ProgramFiles%\litewin_exporter"
set "EXE=%INSTALLDIR%\litewin_exporter.exe"

net session >nul 2>&1
if errorlevel 1 (
    echo ERROR: must run as Administrator ^(elevated cmd^).
    exit /b 1
)

echo === litewin_exporter uninstall ===

REM -- service --
sc stop "%SVC%" >nul 2>&1
ping -n 3 127.0.0.1 >nul
sc delete "%SVC%" >nul 2>&1

REM -- firewall (both the TCP rule and the optional ICMP echo the fleet
REM    installer may have added) --
netsh advfirewall firewall delete rule name="litewin_exporter" >nul 2>&1
netsh advfirewall firewall delete rule name="litewin_exporter ICMP Echo" >nul 2>&1

REM -- Defender exclusion (no-op if none was added) --
powershell -NoProfile -Command "Remove-MpPreference -ExclusionPath '%EXE%'" >nul 2>&1

REM -- files --
if exist "%INSTALLDIR%" rmdir /S /Q "%INSTALLDIR%" >nul 2>&1

echo Done. Service, firewall rules, Defender exclusion, and %INSTALLDIR% removed.
exit /b 0
