@echo off
setlocal
set "TOOL_BIN=C:\Users\HP\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
if exist "%TOOL_BIN%" (
    set "PATH=%TOOL_BIN%;%PATH%"
)

if not exist "build\tracex_tests.exe" (
    echo [INFO] Binaries not found, running build first...
    call build.bat
)

.\build\tracex_tests.exe
endlocal
