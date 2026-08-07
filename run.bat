@echo off
setlocal

set "REPO_DIR=%~dp0"
set "COMMAND=%~1"
set "BUILD_MODE=%~2"

if "%COMMAND%"=="" set "COMMAND=help"
if "%BUILD_MODE%"=="" set "BUILD_MODE=debug"
if /i "%BUILD_MODE%"=="--release" set "BUILD_MODE=release"

if /i "%BUILD_MODE%"=="debug" goto :mode_ok
if /i "%BUILD_MODE%"=="release" goto :mode_ok
echo Unknown build mode: %BUILD_MODE% 1>&2
exit /b 2

:mode_ok
cd /d "%REPO_DIR%"

if /i "%COMMAND%"=="build" goto :build
if /i "%COMMAND%"=="test" goto :test
if /i "%COMMAND%"=="emc" goto :emc
if /i "%COMMAND%"=="email-markup-lsp" goto :email_markup_lsp
if /i "%COMMAND%"=="help" goto :help
if /i "%COMMAND%"=="-h" goto :help
if /i "%COMMAND%"=="--help" goto :help
echo Unknown command: %COMMAND% 1>&2
exit /b 2

:configure
cmake --preset %BUILD_MODE% || exit /b 1
exit /b 0

:compile
call :configure || exit /b 1
cmake --build --preset %BUILD_MODE% || exit /b 1
exit /b 0

:build
echo ==^> Build Email Markup (%BUILD_MODE%)
call :compile || exit /b 1
echo Built emc, email-markup-lsp, and email-markup-tests.
exit /b 0

:test
echo ==^> Build Email Markup (%BUILD_MODE%)
call :compile || exit /b 1
echo ==^> Run tests
ctest --preset %BUILD_MODE% || exit /b 1
echo All Email Markup tests passed.
exit /b 0

:emc
call :compile || exit /b 1
"%REPO_DIR%build\%BUILD_MODE%\bin\emc.exe"
exit /b %errorlevel%

:email_markup_lsp
call :compile || exit /b 1
"%REPO_DIR%build\%BUILD_MODE%\bin\email-markup-lsp.exe"
exit /b %errorlevel%

:help
echo Usage: run.bat ^<command^> [debug^|release]
echo.
echo Commands:
echo   build      Configure and build all targets
echo   test       Build and run the test suite
echo   emc       Build and run the compiler
echo   email-markup-lsp    Build and run the language server
echo   help       Show this help
exit /b 0
