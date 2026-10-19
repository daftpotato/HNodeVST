@echo off
rem ===========================================================================
rem fetch-sdk.bat - download and assemble the two third-party SDKs.
rem Run this ONCE after cloning, before build.bat.
rem
rem   sdk/vst3sdk  - Steinberg VST3 SDK (MIT-ish SDK licence)
rem   sdk/spout2   - Spout2 by Lynn Jarvis (BSD 2-clause)
rem
rem Neither is committed to the repo (see .gitignore) because they are large
rem and carry their own licences.
rem ===========================================================================
setlocal enabledelayedexpansion
set "ROOT=%~dp0.."
set "SDK=%ROOT%\sdk"
set "TMP=%ROOT%\build\_sdkdl"

if not exist "%SDK%" mkdir "%SDK%"
if not exist "%TMP%" mkdir "%TMP%"

echo [1/4] Steinberg VST3 SDK...
call :fetch "https://codeload.github.com/steinbergmedia/vst3sdk/zip/refs/heads/master" "vst3sdk.zip"
call :fetch "https://codeload.github.com/steinbergmedia/vst3_base/zip/refs/heads/master" "vst3_base.zip"
call :fetch "https://codeload.github.com/steinbergmedia/vst3_pluginterfaces/zip/refs/heads/master" "vst3_pluginterfaces.zip"
call :fetch "https://codeload.github.com/steinbergmedia/vst3_public_sdk/zip/refs/heads/master" "vst3_public_sdk.zip"

echo [2/4] extracting...
if exist "%TMP%\x" rmdir /s /q "%TMP%\x"
mkdir "%TMP%\x"
powershell -NoProfile -Command "Expand-Archive -Force -Path '%TMP%\vst3sdk.zip' -DestinationPath '%TMP%\x'" >nul
powershell -NoProfile -Command "Expand-Archive -Force -Path '%TMP%\vst3_base.zip' -DestinationPath '%TMP%\x'" >nul
powershell -NoProfile -Command "Expand-Archive -Force -Path '%TMP%\vst3_pluginterfaces.zip' -DestinationPath '%TMP%\x'" >nul
powershell -NoProfile -Command "Expand-Archive -Force -Path '%TMP%\vst3_public_sdk.zip' -DestinationPath '%TMP%\x'" >nul

echo [3/4] assembling sdk\vst3sdk...
if exist "%SDK%\vst3sdk" rmdir /s /q "%SDK%\vst3sdk"
mkdir "%SDK%\vst3sdk"
for %%F in (CMakeLists.txt LICENSE.txt README.md) do (
  if exist "%TMP%\x\vst3sdk-master\%%F" copy /y "%TMP%\x\vst3sdk-master\%%F" "%SDK%\vst3sdk\" >nul
)
xcopy /e /i /q /y "%TMP%\x\vst3_base-master" "%SDK%\vst3sdk\base" >nul
xcopy /e /i /q /y "%TMP%\x\vst3_pluginterfaces-master" "%SDK%\vst3sdk\pluginterfaces" >nul
xcopy /e /i /q /y "%TMP%\x\vst3_public_sdk-master" "%SDK%\vst3sdk\public.sdk" >nul

echo [4/4] Spout2...
call :fetch "https://codeload.github.com/leadedge/Spout2/zip/refs/heads/master" "spout2.zip"
if exist "%SDK%\spout2" rmdir /s /q "%SDK%\spout2"
powershell -NoProfile -Command "Expand-Archive -Force -Path '%TMP%\spout2.zip' -DestinationPath '%SDK%'" >nul
if exist "%SDK%\Spout2-master" move /y "%SDK%\Spout2-master" "%SDK%\spout2" >nul

rmdir /s /q "%TMP%" 2>nul

echo.
echo --- verifying ---
if exist "%SDK%\vst3sdk\base\source\fobject.cpp" (echo [ok] vst3sdk base) else (echo [FAIL] vst3sdk base)
if exist "%SDK%\vst3sdk\pluginterfaces\vst\ivstaudioprocessor.h" (echo [ok] vst3sdk pluginterfaces) else (echo [FAIL] vst3sdk pluginterfaces)
if exist "%SDK%\vst3sdk\public.sdk\source\vst\vstaudioeffect.cpp" (echo [ok] vst3sdk public.sdk) else (echo [FAIL] vst3sdk public.sdk)
if exist "%SDK%\spout2\SPOUTSDK\SpoutLibrary\Binaries\SpoutLibrary.lib" (echo [ok] spout2) else (echo [FAIL] spout2)

echo.
echo Done. Now run build.bat
endlocal
exit /b 0

:fetch
if exist "%TMP%\%~2" exit /b 0
echo     %~2
curl -sL --max-time 600 -o "%TMP%\%~2" "%~1"
if errorlevel 1 (echo [FAIL] download %~2 & exit /b 1)
exit /b 0
