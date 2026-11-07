@echo off
setlocal
echo ========================================================
echo  Building TraceX: Software Failure Investigation Engine
echo ========================================================

set "TOOL_BIN=C:\Users\HP\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
if exist "%TOOL_BIN%" (
    set "PATH=%TOOL_BIN%;%PATH%"
)

cmake -B build -G "Ninja"
if errorlevel 1 (
    echo [ERROR] CMake configuration failed.
    exit /b %errorlevel%
)

cmake --build build
if errorlevel 1 (
    echo [ERROR] Build failed.
    exit /b %errorlevel%
)

echo.
echo ========================================================
echo  Build successful! Executables located in ./build/
echo    - ./build/tracex.exe        (CLI Engine)
echo    - ./build/tracex_server.exe (HTTP REST Server & Web UI)
echo    - ./build/tracex_tests.exe  (Unit Test Suite)
echo ========================================================
endlocal
