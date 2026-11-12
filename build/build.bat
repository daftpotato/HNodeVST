@echo off
rem ===========================================================================
rem HNodeVST build  (no setlocal: vcvars' environment must survive)
rem ===========================================================================
call "%~dp0env.bat" >nul 2>&1

set "ROOT=%~dp0.."
set "SDK=%ROOT%\sdk\vst3sdk"
set "SPOUT=%ROOT%\sdk\spout2\SPOUTSDK"
set "OUT=%ROOT%\build\out"
set "OBJ=%ROOT%\build\obj"

if not exist "%OBJ%" mkdir "%OBJ%"
if not exist "%OUT%" mkdir "%OUT%"

set "COMMON=/nologo /c /EHsc /W2 /std:c++17 /DNDEBUG /DNOMINMAX /DUNICODE /D_UNICODE /D_WIN32_WINNT=0x0601 /DWIN32_LEAN_AND_MEAN /I "%SDK%" /I "%SDK%\base" /I "%SDK%\public.sdk" /I "%SPOUT%\SpoutLibrary\Binaries""

rem ---- VST3 base ----
set "S1=%SDK%\base\source\baseiids.cpp %SDK%\base\source\fbuffer.cpp %SDK%\base\source\fdebug.cpp %SDK%\base\source\fdynlib.cpp %SDK%\base\source\fobject.cpp %SDK%\base\source\fstreamer.cpp %SDK%\base\source\fstring.cpp %SDK%\base\source\timer.cpp %SDK%\base\source\updatehandler.cpp %SDK%\pluginterfaces\base\funknown.cpp %SDK%\pluginterfaces\base\coreiids.cpp %SDK%\pluginterfaces\base\ustring.cpp %SDK%\pluginterfaces\base\conststringtable.cpp %SDK%\base\thread\source\flock.cpp %SDK%\base\thread\source\fcondition.cpp"

rem ---- VST3 core ----
set "S2=%SDK%\public.sdk\source\vst\vstaudioeffect.cpp %SDK%\public.sdk\source\vst\vstbus.cpp %SDK%\public.sdk\source\vst\vstcomponent.cpp %SDK%\public.sdk\source\vst\vstcomponentbase.cpp %SDK%\public.sdk\source\vst\vsteditcontroller.cpp %SDK%\public.sdk\source\vst\vstinitiids.cpp %SDK%\public.sdk\source\vst\vstnoteexpressiontypes.cpp %SDK%\public.sdk\source\vst\vstparameters.cpp %SDK%\public.sdk\source\vst\vstpresetfile.cpp %SDK%\public.sdk\source\vst\vstrepresentation.cpp"

rem ---- VST3 hosting ----
set "S3=%SDK%\public.sdk\source\vst\hosting\connectionproxy.cpp %SDK%\public.sdk\source\vst\hosting\eventlist.cpp %SDK%\public.sdk\source\vst\hosting\hostclasses.cpp %SDK%\public.sdk\source\vst\hosting\hostdataexchangehandler.cpp %SDK%\public.sdk\source\vst\hosting\module.cpp %SDK%\public.sdk\source\vst\hosting\module_win32.cpp %SDK%\public.sdk\source\vst\hosting\parameterchanges.cpp %SDK%\public.sdk\source\vst\hosting\pluginterfacesupport.cpp %SDK%\public.sdk\source\vst\hosting\plugprovider.cpp %SDK%\public.sdk\source\vst\hosting\processdata.cpp"

rem ---- VST3 utility ----
set "S4=%SDK%\public.sdk\source\vst\utility\dataexchange.cpp %SDK%\public.sdk\source\vst\utility\mpeprocessor.cpp %SDK%\public.sdk\source\vst\utility\stringconvert.cpp %SDK%\public.sdk\source\vst\utility\systemtime.cpp %SDK%\public.sdk\source\vst\utility\vst2persistence.cpp"

rem ---- VST3 common ----
set "S5=%SDK%\public.sdk\source\common\commoniids.cpp %SDK%\public.sdk\source\common\commonstringconvert.cpp %SDK%\public.sdk\source\common\memorystream.cpp %SDK%\public.sdk\source\common\openurl.cpp %SDK%\public.sdk\source\common\pluginview.cpp %SDK%\public.sdk\source\common\readfile.cpp %SDK%\public.sdk\source\common\systemclipboard_win32.cpp %SDK%\public.sdk\source\common\threadchecker_win32.cpp"

rem ---- VST3 main ----
set "S6=%SDK%\public.sdk\source\main\pluginfactory.cpp %SDK%\public.sdk\source\main\dllmain.cpp"

rem ---- plugin ----
set "S7=%ROOT%\src\HNodeVST.cpp %ROOT%\src\Processor.cpp %ROOT%\src\Controller.cpp %ROOT%\src\Editor.cpp %ROOT%\src\Engine.cpp %ROOT%\src\ArtNet.cpp %ROOT%\src\Osc.cpp %ROOT%\src\SpoutOut.cpp %ROOT%\src\Generators.cpp %ROOT%\src\SpoutLoader.cpp"

set "ALL=%S1% %S2% %S3% %S4% %S5% %S6% %S7%"

set "N=0"
for %%S in (%ALL%) do set /a N+=1
echo [build] %N% translation units

pushd "%OBJ%"
rem SDK sources (S1..S6) are cached; plugin sources (S7) always rebuild.
for %%S in (%S1% %S2% %S3% %S4% %S5% %S6%) do (
  if not exist "%%~nS.obj" (
    cl %COMMON% /Fo"%%~nS.obj" "%%S" > "%OBJ%\%%~nS.log" 2>&1
    if errorlevel 1 (
      echo [FAIL] %%~nS
      type "%OBJ%\%%~nS.log"
      popd
      exit /b 1
    )
  )
)
for %%S in (%S7%) do (
  cl %COMMON% /Fo"%%~nS.obj" "%%S" > "%OBJ%\%%~nS.log" 2>&1
  if errorlevel 1 (
    echo [FAIL] %%~nS
    type "%OBJ%\%%~nS.log"
    popd
    exit /b 1
  )
)
popd
echo [ok] all objects compiled

rem ===========================================================================
rem Link
rem ===========================================================================
set "BUNDLE=%OUT%\HNodeVST.vst3\Contents\x86_64-win"
if not exist "%BUNDLE%" mkdir "%BUNDLE%"

del /q "%OBJ%\_link.rsp" 2>nul
for %%S in (%ALL%) do echo "%OBJ%\%%~nS.obj" >> "%OBJ%\_link.rsp"

link /nologo /DLL /DELAYLOAD:SpoutLibrary.dll /OUT:"%BUNDLE%\HNodeVST.vst3" /LIBPATH:"%SPOUT%\SpoutLibrary\Binaries" SpoutLibrary.lib kernel32.lib user32.lib gdi32.lib advapi32.lib shell32.lib ole32.lib oleaut32.lib ws2_32.lib d3d11.lib dxgi.lib comctl32.lib delayimp.lib @%OBJ%\_link.rsp
if errorlevel 1 (
  echo [FAIL] link
  exit /b 1
)
echo [ok] linked %BUNDLE%\HNodeVST.vst3

copy /y "%SPOUT%\SpoutLibrary\Binaries\SpoutLibrary.dll" "%BUNDLE%\" >nul
echo [ok] copied SpoutLibrary.dll
echo [done] %BUNDLE%\HNodeVST.vst3
