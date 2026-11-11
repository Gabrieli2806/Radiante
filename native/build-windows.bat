@echo off
setlocal
rem Builds the native renderer and installs prismrt.dll + shaders into src\main\resources\radiante-native.
rem Requires Visual Studio 2026 or its Build Tools (C++ workload, with CMake) and the Vulkan SDK.
rem vswhere finds whichever edition is installed; without it, Community's usual folder is tried.
set VSDIR=C:\Program Files\Microsoft Visual Studio\18\Community
set VSWHERE="%ProgramFiles(x86)%\Microsoft Visual Studio\Installer\vswhere.exe"
if exist %VSWHERE% for /f "usebackq delims=" %%i in (`%VSWHERE% -latest -products * -version [18.0^,19.0^) -requires Microsoft.VisualStudio.Component.VC.Tools.x86.x64 -property installationPath`) do set "VSDIR=%%i"
set CM="%VSDIR%\Common7\IDE\CommonExtensions\Microsoft\CMake\CMake\bin\cmake.exe"
if not exist %CM% (
    echo Visual Studio 2026 with the C++ workload and CMake was not found, looked in "%VSDIR%".
    exit /b 1
)
if "%RADIANTE_BUILD_DIR%"=="" set RADIANTE_BUILD_DIR=C:\rtb\radiante
set SRC=%~dp0
%CM% -S "%SRC%." -B "%RADIANTE_BUILD_DIR%" -G "Visual Studio 18 2026" -A x64 -DPRISM_ENABLE_NRD=ON -DUSE_AMD=ON -DCMAKE_POLICY_VERSION_MINIMUM=3.5 || exit /b 1
%CM% --build "%RADIANTE_BUILD_DIR%" --config Release -j 16 || exit /b 1
%CM% --install "%RADIANTE_BUILD_DIR%" --config Release || exit /b 1
