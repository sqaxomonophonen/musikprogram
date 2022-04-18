struct vs_out {
	@builtin(position) position: vec4<f32>,
	@location(0) rgba: vec4<f32>,
};

struct uniforms {
	dst_dim: vec2<i32>,
	seed: f32,
	scalar: f32,
};
@group(0) @binding(0) var<uniform> u: uniforms;

#include "inc_xymap.wgsl"

@stage(vertex)
fn vs_main(
	@location(0) xy: vec2<f32>,
	@location(1) rgba: vec4<f32>,
) -> vs_out {
	var out: vs_out;
	out.position = xymap(xy);
	out.rgba = rgba;
	return out;
}

@stage(fragment)
fn fs_main(in: vs_out) -> @location(0) vec4<f32> {
	return in.rgba * vec4<f32>(u.scalar, u.scalar, u.scalar, 1.0);
}
