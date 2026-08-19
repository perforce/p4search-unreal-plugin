@echo off
REM CI/build-agent script — Windows counterpart to build-ci.sh (same
REM PREP/BUILD/PACKAGE/CLEANUP steps, produces P4Search_Win.zip). No
REM machine-specific paths: engine location comes from %UE_ENGINE_ROOT%
REM (required, no fallback — a silent default would risk building against
REM the wrong engine install on an agent). No DEPLOY step and no local-dev
REM variant of this file exists yet, unlike the Mac side's build.sh/
REM build-ci.sh split — there's no established Windows local-dev workflow
REM for this plugin to preserve, so this one script covers the CI case only
REM for now.
REM
REM Does NOT sync the engine itself — that job belongs to
REM sync-engine.cmd/sync-engine.ps1 (see docker/Windows/), run separately
REM before this. Confirmed on a real test run that sync-engine.cmd must run
REM via `docker exec` against an already-running container, not as part of
REM a single sequential script the container's own entrypoint executes --
REM the entrypoint/init process gets a more restricted security token that
REM fails the sync's p4 print write. Whether that same restriction affects
REM build.bat itself when EC eventually drives this is unverified; only the
REM plugin repo's own build steps were checked here, not any EC-Docker
REM execution wiring.
setlocal

if not defined UE_ENGINE_ROOT (
    echo build.bat: UE_ENGINE_ROOT must be set to the Unreal Engine install root 1>&2
    exit /b 1
)

set "UAT=%UE_ENGINE_ROOT%\Engine\Build\BatchFiles\RunUAT.bat"
if not exist "%UAT%" (
    echo build.bat: RunUAT.bat not found at "%UAT%" -- check UE_ENGINE_ROOT 1>&2
    exit /b 1
)

set "cwd=%cd%"
echo Working dir set to %cwd%
echo Using UE_ENGINE_ROOT=%UE_ENGINE_ROOT%

echo **** PREP ****
if exist "%cwd%\NoCompile" rd /s /q "%cwd%\NoCompile"
if exist "%cwd%\P4Search" rd /s /q "%cwd%\P4Search"

echo **** BUILD ****
call "%UAT%" BuildPlugin -Plugin="%cwd%\P4Search.uplugin" -Package="%cwd%\NoCompile" -nocompile -nocompileuat -verbose -NoHostPlatform -Unversioned
if errorlevel 1 goto :error

echo *** PACKAGE ****
call "%UAT%" BuildPlugin -Plugin="%cwd%\NoCompile\P4Search.uplugin" -Package="%cwd%\P4Search" -CreateSubFolder -nocompile -nocompileuat
if errorlevel 1 goto :error

echo **** CLEANUP ****
rd /s /q "%cwd%\NoCompile"
rd /s /q "%cwd%\P4Search\Intermediate"
rd /s /q "%cwd%\P4Search\Source"

echo **** STAMP VERSION ****
REM Same RELEASE/PATCHLEVEL Version-file system p4zbrush uses (see
REM p4zbrush/Version + its Version.java reader) -- stamps the *packaged*
REM P4Search.uplugin, not the checked-in source one, so this never leaves
REM the source tree dirty. PowerShell, not Python, since this container has
REM no Python installed (see stamp-version.ps1's own header for why).
powershell -NoLogo -ExecutionPolicy Bypass -File "%cwd%\..\stamp-version.ps1" "%cwd%\..\Version" "%cwd%\P4Search\P4Search.uplugin"
if errorlevel 1 goto :error

echo **** PACKAGE ****
REM UNVERIFIED: assumes tar.exe (bsdtar, a built-in Windows 10 1803+/Server
REM 2019+ utility) is present in this specific servercore image -- not
REM confirmed on a real test run yet. If missing, swap for
REM [System.IO.Compression.ZipFile]::CreateFromDirectory via a nested
REM powershell -Command call (same pattern already used in sync-engine.ps1).
pushd "%cwd%"
if exist P4Search_Win.zip del /q P4Search_Win.zip
tar -c -a -f P4Search_Win.zip P4Search
popd

endlocal
exit /b 0

:error
echo build.bat: BuildPlugin failed 1>&2
endlocal
exit /b 1
