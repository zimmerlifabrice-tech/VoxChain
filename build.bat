@echo off
REM Optional: build VoxChain locally on Windows.
REM Requires: Visual Studio 2022 (or Build Tools) with "Desktop development with C++", CMake, Git.
REM Normally you don't need this: GitHub builds the plugin automatically.

cmake -B build -A x64 || goto :error
cmake --build build --config Release --parallel --target VoxChain_VST3 VoxChainTests VoxChainPluginTests || goto :error
build\VoxChainTests_artefacts\Release\VoxChainTests.exe || goto :error
build\VoxChainPluginTests_artefacts\Release\VoxChainPluginTests.exe || goto :error

echo.
echo Fertig! Plugin: build\VoxChain_artefacts\Release\VST3\VoxChain.vst3
echo Kopiere den Ordner VoxChain.vst3 nach C:\Program Files\Common Files\VST3
pause
exit /b 0

:error
echo.
echo Build fehlgeschlagen.
pause
exit /b 1
