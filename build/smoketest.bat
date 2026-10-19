@echo off
call "%~dp0env.bat" >nul 2>&1
set "SDK=%~dp0..\sdk\vst3sdk"
cl /nologo /c /EHsc /W2 /std:c++17 /DNDEBUG /DNOMINMAX /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /DWIN32_LEAN_AND_MEAN /I "%SDK%" /I "%SDK%\base" /I "%SDK%\public.sdk" /I "%~dp0..\src" /Fo:"%~dp0smoketest.obj" "%~dp0smoketest.cpp"
if errorlevel 1 (echo COMPILE_FAILED & exit /b 1)

rem Link against the SDK objects only (skip dllmain/pluginfactory and the
rem plugin's own objects, which live inside the .vst3 being tested).
del /q "%~dp0_smoke.rsp" 2>nul
for %%O in ("%~dp0obj\*.obj") do (
  echo %%~nxO | findstr /i /v /c:"dllmain.obj" /c:"pluginfactory.obj" /c:"HNodeVST.obj" /c:"Processor.obj" /c:"Controller.obj" /c:"Editor.obj" /c:"Engine.obj" /c:"ArtNet.obj" /c:"Osc.obj" /c:"SpoutOut.obj" /c:"Generators.obj" /c:"module_win32.obj" >nul
  if not errorlevel 1 echo "%%O" >> "%~dp0_smoke.rsp"
)

link /nologo /OUT:"%~dp0smoketest.exe" "%~dp0smoketest.obj" kernel32.lib user32.lib advapi32.lib gdi32.lib ole32.lib oleaut32.lib ws2_32.lib shell32.lib @%~dp0_smoke.rsp
if errorlevel 1 (echo LINK_FAILED & exit /b 1)
echo SMOKETEST_BUILD_OK
