@echo off
rem Installiert die Raumklang-VST3-Plugins nach "C:\Program Files\Common Files\VST3".
net session >nul 2>&1
if %errorlevel% neq 0 (
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
set "DEST=%CommonProgramFiles%\VST3"
if not exist "%DEST%" mkdir "%DEST%"
rem Plugins frueherer Versionen, die es nicht mehr gibt oder die umbenannt wurden.
if exist "%DEST%\Raumklang Raum.vst3" rmdir /s /q "%DEST%\Raumklang Raum.vst3"
for /d %%P in ("%DEST%\Raumklang R*ckweg.vst3") do rmdir /s /q "%%P"
for /d %%P in ("%~dp0Plugins\*.vst3") do (
  if exist "%DEST%\%%~nxP" rmdir /s /q "%DEST%\%%~nxP"
  xcopy "%%P" "%DEST%\%%~nxP\" /e /i /q /y >nul
)
echo.
echo Fertig. Die Plugins liegen in %DEST%.
echo DAW bitte neu starten.
pause
