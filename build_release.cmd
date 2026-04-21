@echo off
setlocal
cd /d "%~dp0"

rem Mirrors .github/workflows/build.yml -> job "windows" (shell: cmd).
rem Uses Release only (CI sets CMAKE_BUILD_TYPE=Release for non-PR pushes via env).
rem Skips only the Upload artifact step.

set "CMAKE_BUILD_TYPE=Release"

for /f "usebackq delims=" %%H in (`git rev-parse --short^=6 HEAD 2^>nul`) do set "GITHUB_SHA_SHORT=%%H"
if not defined GITHUB_SHA_SHORT set "GITHUB_SHA_SHORT=000000"

git submodule update --init --recursive || (
  echo [build_release] git submodule update failed.
  exit /b 1
)

cmake -B build -D LOVR_VERSION_HASH=%GITHUB_SHA_SHORT% || (
  echo [build_release] Configure failed.
  exit /b 1
)

cmake --build build --config %CMAKE_BUILD_TYPE% || (
  echo [build_release] Build failed.
  exit /b 1
)

build\%CMAKE_BUILD_TYPE%\lovr test --headless || (
  echo [build_release] Test failed.
  exit /b 1
)

exit /b 0
