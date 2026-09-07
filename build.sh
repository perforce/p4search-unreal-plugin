#!/bin/zsh
# Usage: build.sh [unreal-engine-version]  (e.g. build.sh 5.6; defaults to 5.8)
set -e
ue_version="${1:-5.8}"
engine_root="/Users/Shared/Epic Games/UE_$ue_version"

if [[ ! -d "$engine_root" ]]; then
	echo "build.sh: Unreal Engine not found at \"$engine_root\"" >&2
	exit 1
fi

cwd="$(pwd)"
echo Working dir set to $cwd
echo Using engine at "$engine_root"

echo **** PREP ****
pushd $cwd
rm -rf NoCompile
rm -rf P4Search
rm -rf P4Search_ZipStage
popd

echo **** BUILD ****
pushd "$engine_root"
./Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -Plugin=$cwd/P4Search.uplugin -Package=$cwd/NoCompile -nocompile -nocompileuat -verbose -NoHostPlatform -Unversioned
echo *** PACKAGE ****
./Engine/Build/BatchFiles/RunUAT.sh BuildPlugin -Plugin=$cwd/NoCompile/P4Search.uplugin -Package=$cwd/P4Search -CreateSubFolder -nocompile -nocompileuat
popd

echo **** CLEANUP ****
pushd $cwd
rm -rf NoCompile
pushd P4Search
rm -rf Intermediate
popd
popd

echo **** STAMP VERSION ****
python3 $cwd/../stamp-version.py $cwd/../Version $cwd/P4Search/P4Search.uplugin

echo **** DEPLOY ****
# Deploys the Source-intact copy (not the zip below) -- a local C++ game project's own Editor
# target build needs UBT to resolve the P4Search module's rules from source to build the
# target's makefile at all, even though a precompiled binary already exists.
pushd ~/P4Search_Plugin
rm -rf P4Search
popd
cp -r $cwd/P4Search ~/P4Search_Plugin/P4Search

rm -rf "$engine_root/Engine/Plugins/P4Search"
cp -r ~/P4Search_Plugin/P4Search "$engine_root/Engine/Plugins/P4Search"
ls -la "$engine_root/Engine/Plugins" | grep P4Search

echo **** PACKAGE ****
# Builds the distributable zip from a separate Source-stripped copy, so the local deploy above
# keeps Source while this stays a binary-only package.
pushd $cwd
rm -f P4Search_Mac.zip
rm -rf P4Search_ZipStage
mkdir P4Search_ZipStage
cp -r P4Search P4Search_ZipStage/P4Search
rm -rf P4Search_ZipStage/P4Search/Source
pushd P4Search_ZipStage
tar -c -a -f $cwd/P4Search_Mac.zip P4Search
popd
rm -rf P4Search_ZipStage
# cp README.md P4Search_README.md
popd
