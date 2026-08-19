#!/bin/zsh
# CI/build-agent variant of build.sh — same PREP/BUILD/PACKAGE/CLEANUP steps
# (produces the same P4Search_Mac.zip), but with no machine-specific paths:
#   - Engine location comes from $UE_ENGINE_ROOT (required, no fallback — a
#     silent default would risk building against the wrong engine install on
#     an agent, unlike build.sh's convenience default for known local setups).
#   - No DEPLOY step. build.sh's DEPLOY step installs the freshly-built
#     plugin into the developer's own local Unreal Editor (~/P4Search_Plugin
#     staging dir, then Engine/Plugins/P4Search) so they can immediately test
#     it — meaningless on a headless build agent, which has no local Editor
#     to test in. The EC module script is responsible for moving
#     P4Search_Mac.zip into the build's own artifact directory instead.
set -e

if [[ -z "$UE_ENGINE_ROOT" ]]; then
	echo "build-ci.sh: UE_ENGINE_ROOT must be set to the Unreal Engine install root" >&2
	exit 1
fi

cwd="$(pwd)"
echo Working dir set to $cwd
echo Using UE_ENGINE_ROOT=$UE_ENGINE_ROOT

echo **** PREP ****
pushd $cwd
rm -rf NoCompile
rm -rf P4Search
popd

echo **** BUILD ****
pushd "$UE_ENGINE_ROOT"
./Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -Plugin=$cwd/P4Search.uplugin -Package=$cwd/NoCompile -nocompile -nocompileuat -verbose -NoHostPlatform -Unversioned
echo *** PACKAGE ****
./Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -Plugin=$cwd/NoCompile/P4Search.uplugin -Package=$cwd/P4Search -CreateSubFolder -nocompile -nocompileuat
popd

echo **** CLEANUP ****
pushd $cwd
rm -rf NoCompile
pushd P4Search
rm -rf Intermediate
rm -rf Source
popd
popd

echo **** STAMP VERSION ****
# Same RELEASE/PATCHLEVEL Version-file system p4zbrush uses (see
# p4zbrush/Version + its Version.java reader) -- stamps the *packaged*
# P4Search.uplugin, not the checked-in source one, so this never leaves the
# source tree dirty.
python3 $cwd/../stamp-version.py $cwd/../Version $cwd/P4Search/P4Search.uplugin

echo **** PACKAGE ****
pushd $cwd
rm -f P4Search_Mac.zip
tar -c -a -f P4Search_Mac.zip P4Search
popd
