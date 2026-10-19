@echo off
rem Install HNodeVST.vst3 into the system VST3 folder (requires administrator).
rem Right-click this file and choose "Run as administrator".
setlocal
set "SRC=%~dp0out\HNodeVST.vst3"
set "DST=%ProgramFiles%\Common Files\VST3\HNodeVST.vst3"

if not exist "%SRC%" (
  echo Build the plugin first:  build.bat
  exit /b 1
)

echo Removing old install...
if exist "%DST%" rmdir /s /q "%DST%"

echo Copying to "%DST%"...
xcopy /e /i /q /y "%SRC%" "%DST%" >nul
if errorlevel 1 (
  echo.
  echo FAILED - this script needs to run as administrator.
  echo Right-click install-system.bat and choose "Run as administrator".
  exit /b 1
)

echo.
echo Installed:
dir /s /b "%DST%"
echo.
echo Rescan plug-ins in Live: Preferences -^> Plug-Ins -^> Rescan
endlocal
