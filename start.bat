@echo off
rem Starts the ChessAi analysis board and opens it in your browser.
cd /d "%~dp0"
if not exist engine\chessai.exe (
    echo Building engine...
    pushd engine
    mingw32-make || (echo Engine build failed & pause & exit /b 1)
    popd
)
python app\server.py
pause
