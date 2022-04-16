#!/usr/bin/env bash
set -e
if [ "$1" = "" ] ; then
	echo "Usage: $0 <path/to/wgpu-native>"
	exit 1
fi
to=$(pwd)
cd $1
if ! grep -qF 'name = "wgpu-native"' Cargo.toml ; then
	echo "does not appear to be a wgpu-native root directory"
	exit 1
fi

git submodule init
git submodule update
cargo build

cp target/debug/libwgpu_native.so $to/
cp ffi/webgpu-headers/webgpu.h $to/

