@echo off
rem === HNodeVST build environment (NO setlocal: vcvars env must survive return) ===
set "VS17=C:\Program Files (x86)\Microsoft Visual Studio\2017\BuildTools"
call "%VS17%\VC\Auxiliary\Build\vcvars64.bat" >nul 2>&1
if errorlevel 1 (echo VCVARS_FAILED & exit /b 1)
echo [env] cl: %VCINSTALLDIR%
echo [env] WindowsSDK: %WindowsSdkDir%
