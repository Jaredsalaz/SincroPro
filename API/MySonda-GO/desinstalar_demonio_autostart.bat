@echo off
title Desinstalador 1-Clic Demonio MySonda
echo ========================================================
echo Deteniendo y desinstalando el Demonio MySonda...
echo ========================================================
echo.

taskkill /IM MySonda-GO.exe /F 2>nul
set "STARTUP_FOLDER=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup"
if exist "%STARTUP_FOLDER%\MySondaDaemon.lnk" del "%STARTUP_FOLDER%\MySondaDaemon.lnk"

echo ========================================================
echo ¡EXITO! El Demonio MySonda ha sido detenido y desinstalado.
echo ========================================================
echo.
pause
