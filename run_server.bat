@echo off
setlocal
set "TOOL_BIN=C:\Users\HP\AppData\Local\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin"
if exist "%TOOL_BIN%" (
    set "PATH=%TOOL_BIN%;%PATH%"
)

if not exist "build\tracex_server.exe" (
    echo [INFO] Binaries not found, running build first...
    call build.bat
)

echo Starting TraceX Server on port 8080...
echo Open http://localhost:8080 in your browser.
start http://localhost:8080
.\build\tracex_server.exe --port 8080 --web frontend --db data/tracex.db
endlocal
