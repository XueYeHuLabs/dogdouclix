@echo off
setlocal enabledelayedexpansion

echo [DOGDOUCLIX] Locating build toolchain...

:: Locate vswhere
set "VSWHERE=%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if not exist "%VSWHERE%" (
    echo [ERROR] vswhere.exe not found at "%VSWHERE%". 1>&2
    exit /b 1
)

:: Check if cl.exe is already in PATH
where cl.exe >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [DOGDOUCLIX] Initializing MSVC environment via vcvars64.bat...
    for /f "usebackq tokens=*" %%i in (`"%VSWHERE%" -latest -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do (
        set "VS_INSTALL_DIR=%%i"
    )
    if not defined VS_INSTALL_DIR (
        echo [ERROR] Visual Studio with C++ tools not found. 1>&2
        exit /b 1
    )
    if exist "!VS_INSTALL_DIR!\VC\Auxiliary\Build\vcvars64.bat" (
        call "!VS_INSTALL_DIR!\VC\Auxiliary\Build\vcvars64.bat" >nul
    ) else (
        echo [ERROR] vcvars64.bat not found in "!VS_INSTALL_DIR!". 1>&2
        exit /b 1
    )
)

:: Locate cmake
where cmake.exe >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    if defined VS_INSTALL_DIR (
        if exist "!VS_INSTALL_DIR!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe" (
            set "PATH=!VS_INSTALL_DIR!\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin;!PATH!"
        )
    )
)

where cmake.exe >nul 2>&1
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] cmake.exe not found in PATH or Visual Studio directory. 1>&2
    exit /b 1
)

echo [DOGDOUCLIX] Configuring CMake build...
cmake -B "%~dp0build" -S "%~dp0." -DCMAKE_BUILD_TYPE=Release
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] CMake configuration failed. 1>&2
    exit /b %ERRORLEVEL%
)

echo [DOGDOUCLIX] Building Release targets...
cmake --build "%~dp0build" --config Release --parallel
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Build failed. 1>&2
    exit /b %ERRORLEVEL%
)

echo [DOGDOUCLIX] Running test suite...
ctest --test-dir "%~dp0build" -C Release --output-on-failure
if %ERRORLEVEL% NEQ 0 (
    echo [ERROR] Test suite failed. 1>&2
    exit /b %ERRORLEVEL%
)

echo.
echo ========================================
echo [DOGDOUCLIX] Build and test PASSED!
echo Executable: %~dp0build\src\cli\Release\dogdouclix.exe
echo ========================================
exit /b 0