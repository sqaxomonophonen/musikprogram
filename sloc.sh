#!/usr/bin/env bash

sloc() {
	#cat
	echo -ne "$1:\t"
	xargs cat | wc -l
}

git ls-files '*.c' | sloc ".c"
git ls-files '*.h' | grep -vF "stb_" | grep -vF "webgpu.h" | grep -vF "gpudl.h" | grep -vF "sokol_" | sloc ".h"
git ls-files '*.wgsl' | sloc ".wgsl"
