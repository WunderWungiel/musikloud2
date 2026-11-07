@echo off
:: Builds MusiKloud2 for Symbian^3/Anna/Belle (Qt 4.7.4 SDK, GCCE) and produces an unsigned .sis.
:: In-source build: Qt 4.7 + sbsv2 does not support shadow builds.
:: Needs the qsoundcloud submodule: git submodule update --init
:: Usage: build_symbian.bat [release|debug]   (default: release)
setlocal
set PROJDIR=%~dp0
set CONF=%1
if "%CONF%"=="" set CONF=release

if not exist "%PROJDIR%external\qsoundcloud\src\request.cpp" (
    echo qsoundcloud submodule missing: run git submodule update --init
    exit /b 1
)

call C:\QtSDK\Symbian\SDKs\Symbian3Qt474\env.bat
pushd "%PROJDIR%"

qmake.exe musikloud2.pro -r -spec symbian-sbsv2 "CONFIG+=%CONF%" || goto :fail
make %CONF%-gcce -w || goto :fail
:: -o: create only the unsigned package (no self-signing, no capability patching).
if exist musikloud2.sis del musikloud2.sis
make -C app sis QT_SIS_TARGET=%CONF%-gcce QT_SIS_OPTIONS=-o || goto :fail
move /y app\musikloud2_unsigned.sis musikloud2.sis >nul || goto :fail

echo.
echo === Build OK ===
dir /b "%PROJDIR%*.sis"
popd
exit /b 0

:fail
echo.
echo === Build FAILED ===
popd
exit /b 1
