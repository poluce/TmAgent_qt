@echo off
setlocal

if "%~2"=="" (
    echo copy_runtime_assets.cmd requires at least 2 arguments.
    exit /b 2
)

set "RESOURCES_SRC=%~1"
set "RESOURCES_DEST=%~2"
set "BACKEND_SRC=%~3"
set "BACKEND_DEST=%~4"
set "TOOL_SRC=%~5"
set "TOOL_DEST=%~6"

if /I not "%RESOURCES_SRC%"=="-" if /I not "%RESOURCES_DEST%"=="-" (
    xcopy /Y /E /I "%RESOURCES_SRC%" "%RESOURCES_DEST%"
    if errorlevel 4 exit /b %errorlevel%
)

call :copy_plugin_dir "%BACKEND_SRC%" "%BACKEND_DEST%"
if errorlevel 1 exit /b %errorlevel%

if not "%TOOL_DEST%"=="" (
    call :copy_plugin_dir "%TOOL_SRC%" "%TOOL_DEST%"
    if errorlevel 1 exit /b %errorlevel%
)

exit /b 0

:copy_plugin_dir
if "%~2"=="" exit /b 0

if exist "%~2" rmdir /S /Q "%~2"
mkdir "%~2"
if errorlevel 1 exit /b %errorlevel%

if exist "%~1\\*.dll" (
    xcopy /Y /I "%~1\\*.dll" "%~2\\"
    if errorlevel 1 exit /b %errorlevel%
)

exit /b 0
