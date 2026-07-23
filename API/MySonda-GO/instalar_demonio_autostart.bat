@echo off
title Instalador 1-Clic Demonio Auto-Inicio MySonda
echo ========================================================
echo Instalandote el Demonio de Replica 24/7 en Windows...
echo ========================================================
echo.

set "EXE_PATH=%~dp0MySonda-GO.exe"
set "STARTUP_FOLDER=%APPDATA%\Microsoft\Windows\Start Menu\Programs\Startup"
set "VBS_PATH=%~dp0run_daemon_hidden.vbs"
set "SHORTCUT_PATH=%STARTUP_FOLDER%\MySondaDaemon.lnk"

:: 1. Crear script VBS para ejecutar el demonio de Go de forma totalmente oculta (sin ventana negra)
echo Set WshShell = CreateObject("WScript.Shell") > "%VBS_PATH%"
echo WshShell.Run """" ^& "%EXE_PATH%" ^& """", 0, False >> "%VBS_PATH%"

:: 2. Crear acceso directo en la carpeta de Inicio de Windows (Startup)
echo Set oWS = WScript.CreateObject("WScript.Shell") > create_shortcut.vbs
echo sLinkFile = "%SHORTCUT_PATH%" >> create_shortcut.vbs
echo Set oLink = oWS.CreateShortcut(sLinkFile) >> create_shortcut.vbs
echo oLink.TargetPath = "%VBS_PATH%" >> create_shortcut.vbs
echo oLink.WorkingDirectory = "%~dp0" >> create_shortcut.vbs
echo oLink.Description = "MySonda Go Replix Daemon 24/7" >> create_shortcut.vbs
echo oLink.Save >> create_shortcut.vbs

cscript //nologo create_shortcut.vbs
del create_shortcut.vbs

:: 3. Iniciar el demonio inmediatamente en segundo plano
start "" "%VBS_PATH%"

echo ========================================================
echo ¡EXITO! El Demonio MySonda se ha instalado correctamente.
echo.
echo 1. Se ejecutara en SEGUNDO PLANO sin ventanas molestamente abiertas.
echo 2. Arrancara AUTOMATICAMENTE cada vez que enciendas esta PC.
echo 3. Ya esta CORRIENDO en este instante.
echo ========================================================
echo.
pause
