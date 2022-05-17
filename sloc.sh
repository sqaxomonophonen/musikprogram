#!/usr/bin/env bash

ARG=$1

sloc() {
	if [ "$ARG" = "1" ] ; then
		xargs wc -l | grep -v " total$"
	else
		echo -ne "$1:\t"
		xargs cat | wc -l
	fi
}

git ls-files '*.c' | sloc ".c"
if [ "$ARG" = "" ] ; then
	# exclude "external deps"
	git ls-files '*.h' | grep -vF "stb_" | grep -vF "webgpu.h" | grep -vF "gpudl.h" | grep -vF "sokol_" | sloc ".h"
else
	git ls-files '*.h' | sloc ".h"
fi
git ls-files '*.wgsl' | sloc ".wgsl"
