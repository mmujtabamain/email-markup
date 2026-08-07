@echo off
setlocal

set "SCRIPT_DIR=%~dp0"
for %%I in ("%SCRIPT_DIR%..") do set "REPO_DIR=%%~fI"
set "BUILD_MODE=%~1"
if "%BUILD_MODE%"=="" set "BUILD_MODE=debug"

if /i "%BUILD_MODE%"=="debug" goto :mode_ok
if /i "%BUILD_MODE%"=="release" goto :mode_ok
echo Build mode must be debug or release. 1>&2
exit /b 2

:mode_ok
if defined EMC goto :compiler_ready
echo ==^> Step 1/2 - Build the Email Markup compiler (%BUILD_MODE%)
call "%REPO_DIR%\run.bat" build %BUILD_MODE% || exit /b 1
set "EMC=%REPO_DIR%\build\%BUILD_MODE%\bin\emc.exe"
goto :compile

:compiler_ready
echo ==^> Step 1/2 - Use the configured Email Markup compiler
"%EMC%" --version || exit /b 1

:compile
echo ==^> Step 2/2 - Compile all ten examples
for %%D in (01-interpolation 02-conditionals 03-loops 04-expressions 05-typed-props 06-named-slots 07-tokens 08-includes 09-css-inlining 10-responsive-media) do (
  echo ^> "%EMC%" compile "%SCRIPT_DIR%%%D\message.em" -o "%SCRIPT_DIR%%%D\message.html"
  "%EMC%" compile "%SCRIPT_DIR%%%D\message.em" -o "%SCRIPT_DIR%%%D\message.html" || exit /b 1
)

echo Compiled all ten examples.
exit /b 0
