#!/bin/zsh
# Usage: build.sh [unreal-engine-version]  (e.g. build.sh 5.6; defaults to 5.5)
set -e
ue_version="${1:-5.5}"
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
rm -rf Source
popd
popd

echo **** STAMP VERSION ****
python3 $cwd/../stamp-version.py $cwd/../Version $cwd/P4Search/P4Search.uplugin

echo **** PACKAGE ****
pushd $cwd
rm -f P4Search_Mac.zip
tar -c -a -f P4Search_Mac.zip P4Search
# cp README.md P4Search_README.md
popd

echo **** DEPLOY ****
pushd ~/P4Search_Plugin
rm -rf P4Search
cp $cwd/P4Search_Mac.zip .
unzip P4Search_Mac.zip
rm P4Search_Mac.zip
popd

rm -rf "$engine_root/Engine/Plugins/P4Search"
cp -r ~/P4Search_Plugin/P4Search "$engine_root/Engine/Plugins/P4Search"
ls -la "$engine_root/Engine/Plugins" | grep P4Search
