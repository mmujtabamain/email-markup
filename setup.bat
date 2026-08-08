@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "REPO_DIR=%~dp0"
set "VCPKG_DIR=%REPO_DIR%external\vcpkg"
set "VCPKG_VERSION_FILE=%REPO_DIR%external\vcpkg.version"
set "VCPKG_STATE_FILE=%VCPKG_DIR%\.email-markup-version"
set "VCPKG_VERSION="

set "USE_COLOR="
if not defined NO_COLOR (
  if defined WT_SESSION set "USE_COLOR=1"
  if defined ANSICON set "USE_COLOR=1"
  if /i "%ConEmuANSI%"=="ON" set "USE_COLOR=1"
  if defined TERM if /i not "%TERM%"=="dumb" set "USE_COLOR=1"
)

set "RED="
set "GREEN="
set "YELLOW="
set "BLUE="
set "BOLD="
set "RESET="
if defined USE_COLOR (
  for /f "delims=#" %%E in ('"prompt #$E# & for %%E in (1) do rem"') do set "ESC=%%E"
  call :set_colors
)

cd /d "%REPO_DIR%"

call :step "Step 1/4 - Check build prerequisites"
where git >nul 2>nul || goto :missing_git
where cmake >nul 2>nul || goto :missing_cmake
where ninja >nul 2>nul || goto :missing_ninja

call :step "Step 2/4 - Clone vcpkg"
if exist "%VCPKG_VERSION_FILE%" set /p "VCPKG_VERSION="<"%VCPKG_VERSION_FILE%"
set "VCPKG_REQUESTED_VERSION=latest"
if defined VCPKG_VERSION set "VCPKG_REQUESTED_VERSION=%VCPKG_VERSION%"

if exist "%VCPKG_DIR%\bootstrap-vcpkg.bat" (
  set "VCPKG_INSTALLED_VERSION="
  if exist "%VCPKG_STATE_FILE%" set /p "VCPKG_INSTALLED_VERSION="<"%VCPKG_STATE_FILE%"
  call :check_vcpkg_version || goto :error
) else if exist "%VCPKG_DIR%" goto :invalid_vcpkg

if exist "%VCPKG_DIR%\bootstrap-vcpkg.bat" goto :vcpkg_cloned
if exist "%VCPKG_DIR%" goto :invalid_vcpkg

if defined VCPKG_VERSION (
  git clone --depth 1 --branch "%VCPKG_VERSION%" https://github.com/microsoft/vcpkg.git "%VCPKG_DIR%" || goto :error
) else (
  git clone --depth 1 https://github.com/microsoft/vcpkg.git "%VCPKG_DIR%" || goto :error
)
>"%VCPKG_STATE_FILE%" echo %VCPKG_REQUESTED_VERSION%
rmdir /s /q "%VCPKG_DIR%\.git" || goto :error
goto :vcpkg_cloned

:invalid_vcpkg
  call :fail "%VCPKG_DIR% exists but is not a valid vcpkg checkout."
  exit /b 1

:vcpkg_cloned
call :step "Step 3/4 - Bootstrap vcpkg"
if exist "%VCPKG_DIR%\vcpkg.exe" (
  call :success "vcpkg is already bootstrapped."
) else (
  call "%VCPKG_DIR%\bootstrap-vcpkg.bat" -disableMetrics || goto :error
)

call :step "Step 4/4 - Configure the Debug build"
cmake --preset debug || goto :error

echo.
call :success "Setup complete. Run run.bat build to compile Email Markup."
exit /b 0

:missing_git
call :fail "Missing required command: git"
exit /b 1
:missing_cmake
call :fail "Missing required command: cmake"
exit /b 1
:missing_ninja
call :fail "Missing required command: ninja"
exit /b 1
:error
call :fail "Setup failed."
exit /b 1

:set_colors
set "RED=%ESC%[0;31m"
set "GREEN=%ESC%[0;32m"
set "YELLOW=%ESC%[0;33m"
set "BLUE=%ESC%[0;34m"
set "BOLD=%ESC%[1m"
set "RESET=%ESC%[0m"
exit /b 0

:step
echo.
echo %BOLD%%BLUE%==^> %~1%RESET%
exit /b 0

:success
echo %GREEN%%~1%RESET%
exit /b 0

:warn
echo %YELLOW%%~1%RESET%
exit /b 0

:check_vcpkg_version
if "%VCPKG_INSTALLED_VERSION%"=="%VCPKG_REQUESTED_VERSION%" (
  call :success "vcpkg %VCPKG_REQUESTED_VERSION% is already cloned."
  exit /b 0
)
call :warn "The requested vcpkg version changed; replacing the generated checkout."
rmdir /s /q "%VCPKG_DIR%" || exit /b 1
exit /b 0

:fail
echo %RED%%~1%RESET% 1>&2
exit /b 0
