#!/usr/bin/env bash
set -ex
rm -rf ../package
mkdir -p ../package
cp emu293.exe *.dll ../README.md ../package
for rom_help in ../roms/**/README.md; do
	dir=$(basename $(dirname $rom_help))
	mkdir -p ../package/roms/$dir
	cp $rom_help ../package/roms/$dir/
done
