@echo off
rem Installiert die Raumklang-VST3-Plugins nach "C:\Program Files\Common Files\VST3".
net session >nul 2>&1
if %errorlevel% neq 0 (
  powershell -NoProfile -Command "Start-Process -FilePath '%~f0' -Verb RunAs"
  exit /b
)
set "DEST=%CommonProgramFiles%\VST3"
if not exist "%DEST%" mkdir "%DEST%"
for /d %%P in ("%~dp0Plugins\*.vst3") do (
  if exist "%DEST%\%%~nxP" rmdir /s /q "%DEST%\%%~nxP"
  xcopy "%%P" "%DEST%\%%~nxP\" /e /i /q /y >nul
)
echo.
echo Fertig. Die Plugins liegen in %DEST%.
echo DAW bitte neu starten.
pause
