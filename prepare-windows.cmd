@echo off
setlocal EnableExtensions DisableDelayedExpansion
title KARN Mods - Prepare Windows
echo KARN Mods - Prepare Windows
echo This prepares the tools and dependencies needed to build Aseprite.
echo Keep both .cmd and both .ps1 files together in the same folder.
echo.
if not exist "%~dp0prepare-windows.ps1" (
  echo Missing prepare-windows.ps1. Place it beside this launcher and try again.
  pause
  exit /b 1
)
pushd "%~dp0"
if errorlevel 1 (
  echo Cannot open the launcher folder. Move the files to a writable local folder.
  pause
  exit /b 1
)
set "KARN_POWERSHELL=%SystemRoot%\System32\WindowsPowerShell\v1.0\powershell.exe"
if defined PROCESSOR_ARCHITEW6432 set "KARN_POWERSHELL=%SystemRoot%\Sysnative\WindowsPowerShell\v1.0\powershell.exe"
"%KARN_POWERSHELL%" -NoProfile -ExecutionPolicy Bypass -File "%~dp0prepare-windows.ps1" %*
set "KARN_RESULT=%errorlevel%"
echo.
if "%KARN_RESULT%"=="0" (
  echo Setup complete. Next, double-click build-windows.cmd.
) else (
  echo Setup did not finish. See the message above and .build\windows-setup.log.
  echo Fix the problem, then double-click this launcher to try again.
)
popd
pause
exit /b %KARN_RESULT%
