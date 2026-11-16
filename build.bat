@echo off
setlocal EnableDelayedExpansion

echo ============================================================
echo   MediaX Desktop Player - Windows Build
echo   Team: Ankit, Adhiraj, Yatharth, Akash
echo ============================================================
echo.

cd /d "%~dp0"

if not exist "build" mkdir "build"

REM ---------------------------------------------------------------------------
REM Auto-detect modern w64devkit if extracted in common directories
REM ---------------------------------------------------------------------------
if exist "C:\w64devkit\bin\g++.exe" (
    set "PATH=C:\w64devkit\bin;%PATH%"
    echo [INFO] Prioritizing modern compiler found at: C:\w64devkit\bin
) else if exist "%USERPROFILE%\Downloads\w64devkit\bin\g++.exe" (
    set "PATH=%USERPROFILE%\Downloads\w64devkit\bin;%PATH%"
    echo [INFO] Prioritizing modern compiler found in: Downloads\w64devkit\bin
) else if exist "%USERPROFILE%\Desktop\w64devkit\bin\g++.exe" (
    set "PATH=%USERPROFILE%\Desktop\w64devkit\bin;%PATH%"
    echo [INFO] Prioritizing modern compiler found on: Desktop\w64devkit\bin
)

REM ---------------------------------------------------------------------------
REM Check for g++
REM ---------------------------------------------------------------------------
where g++ >nul 2>&1
if %ERRORLEVEL% equ 0 goto COMPILE_GPP

REM ---------------------------------------------------------------------------
REM Check for cmake
REM ---------------------------------------------------------------------------
where cmake >nul 2>&1
if %ERRORLEVEL% equ 0 goto COMPILE_CMAKE

goto COMPILER_MISSING


:COMPILE_GPP
echo [INFO] Active Compiler:
g++ --version | findstr /i "g++ gcc Free Software Foundation"
echo.

echo [1/3] Compiling MediaX.exe...
REM Try C++17 first; fallback to C++14 if compiler is an older version
g++ -std=c++17 -O2 -Wall -Wextra -I. -Ibridge -Iengine ^
    main.cpp bridge/Bridge.cpp engine/AudioHandle.cpp engine/Engine.cpp engine/MetadataReader.cpp engine/PlaylistManager.cpp engine/SearchEngine.cpp engine/SmartQueue.cpp ^
    -o build/MediaX.exe -lole32 -lshell32 -luser32 2>build/compile_err.log

if %ERRORLEVEL% neq 0 (
    echo [WARN] C++17 build returned error, retrying with C++14 flags...
    g++ -std=c++14 -O2 -Wall -I. -Ibridge -Iengine ^
        main.cpp bridge/Bridge.cpp engine/AudioHandle.cpp engine/Engine.cpp engine/MetadataReader.cpp engine/PlaylistManager.cpp engine/SearchEngine.cpp engine/SmartQueue.cpp ^
        -o build/MediaX.exe -lole32 -lshell32 -luser32
)

if %ERRORLEVEL% neq 0 (
    echo.
    echo ============================================================
    echo [ERROR] Compilation failed. Error details:
    echo ============================================================
    type build\compile_err.log
    echo.
    echo TIP: If using an old MinGW (like GCC 6.3 from 2016), extract
    echo the modern 'w64devkit' into C:\w64devkit and rerun this script!
    echo ============================================================
    echo.
    pause
    exit /b 1
)

echo [2/3] Copying web resources into build/resources/...
if exist "resources" (
    xcopy /s /e /y /i "resources" "build\resources" >nul
)

echo [3/3] Compiling C++ Verification Tool (verify_backend.exe)...
g++ -std=c++14 -O2 -I. -Ibridge -Iengine ^
    test_runner.cpp bridge/Bridge.cpp engine/AudioHandle.cpp engine/Engine.cpp engine/MetadataReader.cpp engine/PlaylistManager.cpp engine/SearchEngine.cpp engine/SmartQueue.cpp ^
    -o build/verify_backend.exe 2>nul

echo.
echo ============================================================
echo   SUCCESS! MediaX has been compiled!
echo.
echo   Generated Files:
echo     1. Desktop App:        build\MediaX.exe
echo     2. C++ DSA Tester:     build\verify_backend.exe
echo     3. Web Assets Folder:  build\resources\
echo ============================================================
echo.
pause
exit /b 0


:COMPILE_CMAKE
echo [INFO] g++ not found directly, but CMake is available.
echo [1/2] Running CMake configure...
cd build
cmake ..
if %ERRORLEVEL% neq 0 (
    echo.
    echo [INFO] Retrying with MinGW Makefiles...
    cmake .. -G "MinGW Makefiles"
)
if %ERRORLEVEL% neq 0 goto COMPILER_MISSING

echo [2/2] Building executable with CMake...
cmake --build . --config Release --parallel
if %ERRORLEVEL% neq 0 (
    echo [ERROR] CMake build failed.
    pause
    exit /b 1
)
echo.
echo ============================================================
echo   BUILD SUCCESSFUL via CMake!
echo   Executable location: build\MediaX.exe
echo ============================================================
echo.
pause
exit /b 0


:COMPILER_MISSING
echo.
echo ============================================================
echo   [!] NO C++ COMPILER DETECTED IN WINDOWS PATH
echo ============================================================
echo.
echo You downloaded w64devkit, but its 'bin' folder was not found.
echo.
echo QUICK FIX (Takes 30 seconds):
echo   Extract w64devkit directly to C:\ so that this folder exists:
echo        C:\w64devkit\bin\g++.exe
echo   Then simply run build.bat again!
echo ============================================================
echo.
pause
exit /b 1
