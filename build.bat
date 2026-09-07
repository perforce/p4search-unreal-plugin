@echo off
REM Local dev build script (Windows) -- counterpart to build.sh (Mac). Same
REM PREP/BUILD/CLEANUP/STAMP VERSION/DEPLOY/PACKAGE steps, adapted to batch
REM (tar/xcopy/findstr instead of tar/cp/grep) and the Windows default Epic
REM Games install path. DEPLOY (Source-intact, for local project builds) runs
REM before PACKAGE (Source-stripped zip) -- see the DEPLOY step's own comment.
REM Usage: build.bat [unreal-engine-version]  (e.g. build.bat 5.6; defaults to 5.5)
setlocal enabledelayedexpansion

set "ue_version=%~1"
if "%ue_version%"=="" set "ue_version=5.5"
set "engine_root=C:\Program Files\Epic Games\UE_%ue_version%"

if not exist "%engine_root%" (
    echo build.bat: Unreal Engine not found at "%engine_root%" 1>&2
    exit /b 1
)

set "UAT=%engine_root%\Engine\Build\BatchFiles\RunUAT.bat"
if not exist "%UAT%" (
    echo build.bat: RunUAT.bat not found at "%UAT%" -- check engine install 1>&2
    exit /b 1
)

set "cwd=%cd%"
echo Working dir set to %cwd%
echo Using engine at "%engine_root%"

echo **** PREP ****
if exist "%cwd%\NoCompile" rd /s /q "%cwd%\NoCompile"
if exist "%cwd%\P4Search" rd /s /q "%cwd%\P4Search"
if exist "%cwd%\P4Search_ZipStage" rd /s /q "%cwd%\P4Search_ZipStage"

echo **** BUILD ****
call "%UAT%" BuildPlugin -Plugin="%cwd%\P4Search.uplugin" -Package="%cwd%\NoCompile" -nocompile -nocompileuat -verbose -NoHostPlatform -Unversioned
if errorlevel 1 goto :error

echo *** PACKAGE ****
call "%UAT%" BuildPlugin -Plugin="%cwd%\NoCompile\P4Search.uplugin" -Package="%cwd%\P4Search" -CreateSubFolder -nocompile -nocompileuat
if errorlevel 1 goto :error

echo **** CLEANUP ****
rd /s /q "%cwd%\NoCompile"
rd /s /q "%cwd%\P4Search\Intermediate"

echo **** STAMP VERSION ****
powershell -NoLogo -ExecutionPolicy Bypass -File "%cwd%\..\stamp-version.ps1" "%cwd%\..\Version" "%cwd%\P4Search\P4Search.uplugin"
if errorlevel 1 goto :error

echo **** DEPLOY ****
REM Deploys the Source-intact copy (not the zip below) -- a local C++ game project's own
REM Editor target build needs UBT to resolve the P4Search module's rules from source to build
REM the target's makefile at all, even though a precompiled binary already exists.
set "deploy_dir=%USERPROFILE%\P4Search_Plugin"
if not exist "%deploy_dir%" mkdir "%deploy_dir%"
if exist "%deploy_dir%\P4Search" rd /s /q "%deploy_dir%\P4Search"
xcopy /e /i /y "%cwd%\P4Search" "%deploy_dir%\P4Search" >nul

if exist "%engine_root%\Engine\Plugins\P4Search" rd /s /q "%engine_root%\Engine\Plugins\P4Search"
xcopy /e /i /y "%deploy_dir%\P4Search" "%engine_root%\Engine\Plugins\P4Search" >nul
dir "%engine_root%\Engine\Plugins" | findstr P4Search

echo **** PACKAGE ****
REM Builds the distributable zip from a separate Source-stripped copy, so the local deploy
REM above keeps Source while this stays a binary-only package.
pushd "%cwd%"
if exist P4Search_Win.zip del /q P4Search_Win.zip
if exist P4Search_ZipStage rd /s /q P4Search_ZipStage
mkdir P4Search_ZipStage
xcopy /e /i /y P4Search P4Search_ZipStage\P4Search >nul
rd /s /q P4Search_ZipStage\P4Search\Source
pushd P4Search_ZipStage
REM Assumes tar.exe (bsdtar, built into Windows 10 1803+/Server 2019+) is present -- same
REM assumption build-ci.bat already makes for the CI path.
tar -c -a -f "%cwd%\P4Search_Win.zip" P4Search
popd
rd /s /q P4Search_ZipStage
popd

endlocal
exit /b 0

:error
echo build.bat: build failed 1>&2
endlocal
exit /b 1
