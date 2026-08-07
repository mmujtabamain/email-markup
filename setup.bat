@echo off
setlocal

set "REPO_DIR=%~dp0"
set "VCPKG_DIR=%REPO_DIR%external\vcpkg"

cd /d "%REPO_DIR%"

echo.
echo ==^> Step 1/4 - Check build prerequisites
where git >nul 2>nul || goto :missing_git
where cmake >nul 2>nul || goto :missing_cmake
where ninja >nul 2>nul || goto :missing_ninja

echo.
echo ==^> Step 2/4 - Initialise the vcpkg submodule
git submodule update --init --recursive --depth 1 || goto :error

echo.
echo ==^> Step 3/4 - Bootstrap vcpkg
if exist "%VCPKG_DIR%\vcpkg.exe" (
  echo vcpkg is already bootstrapped.
) else (
  call "%VCPKG_DIR%\bootstrap-vcpkg.bat" -disableMetrics || goto :error
)

echo.
echo ==^> Step 4/4 - Configure the Debug build
cmake --preset debug || goto :error

echo.
echo Setup complete. Run run.bat build to compile Email Markup.
exit /b 0

:missing_git
echo Missing required command: git 1>&2
exit /b 1
:missing_cmake
echo Missing required command: cmake 1>&2
exit /b 1
:missing_ninja
echo Missing required command: ninja 1>&2
exit /b 1
:error
echo Setup failed. 1>&2
exit /b 1
