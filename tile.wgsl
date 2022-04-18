struct vs_out {
	@builtin(position) position: vec4<f32>,
	@location(0) uv:   vec2<f32>,
	@location(1) rgba: vec4<f32>,
};

struct uniforms {
	width: f32,
	height: f32,
	seed: f32,
	scalar: f32,
};
@group(0) @binding(0) var<uniform> u: uniforms;
@group(0) @binding(1) var atlas: texture_2d<f32>;

#include "inc_draw.wgsl"

@stage(vertex)
fn vs_main(
	@location(0) xy: vec2<f32>,
	@location(1) uv: vec2<f32>,
	@location(2) rgba: vec4<f32>,
) -> vs_out {
	var out: vs_out;
	out.position = xymap(xy);
	out.uv = uv;
	out.rgba = rgba;
	return out;
}

@stage(fragment)
fn fs_main(in: vs_out) -> @location(0) vec4<f32> {
	return fragmap(in.rgba * textureLoad(atlas, vec2<i32>(i32(in.uv.x), i32(in.uv.y)), 0));
}
