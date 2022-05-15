#!/usr/bin/env bash

FULL=$1

sloc() {
	if [ "$FULL" = "1" ] ; then
		xargs wc -l | grep -v " total$"
	else
		echo -ne "$1:\t"
		xargs cat | wc -l
	fi
}

git ls-files '*.c' | sloc ".c"
git ls-files '*.h' | grep -vF "stb_" | grep -vF "webgpu.h" | grep -vF "gpudl.h" | grep -vF "sokol_" | sloc ".h"
git ls-files '*.wgsl' | sloc ".wgsl"
