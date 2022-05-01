struct vs_out {
	@builtin(position) position: vec4<f32>,
	@location(0) pattern_uv: vec2<f32>,
	@location(1) atlas_uv:     vec2<f32>,
	@location(2) rgba:   vec4<f32>,
};

struct uniforms {
	width: f32,
	height: f32,
	seed: f32,
	scalar: f32,
};
@group(0) @binding(0) var<uniform> u: uniforms;
@group(0) @binding(1) var atlas: texture_2d<f32>;

struct pat_uniforms {
	x0:  f32,
	y0:  f32,
	bxx: f32,
	bxy: f32,
	byx: f32,
	byy: f32,
};
@group(1) @binding(0) var<uniform> pu: pat_uniforms;
@group(1) @binding(1) var pattern: texture_2d<f32>;
@group(1) @binding(2) var pattern_smpl: sampler;

#include "inc_draw.wgsl"

@stage(vertex)
fn vs_main(
	@location(0) xy: vec2<f32>,
	@location(1) atlas_uv: vec2<f32>,
	@location(2) rgba: vec4<f32>,
) -> vs_out {
	var out: vs_out;
	out.position = xymap(xy);
	out.pattern_uv = vec2<f32>(
		pu.x0 + xy.x*pu.bxx + xy.y*pu.bxy,
		pu.y0 + xy.x*pu.byx + xy.y*pu.byy
	);
	out.atlas_uv = atlas_uv;
	out.rgba = rgba;
	return out;
}

@stage(fragment)
fn fs_main(in: vs_out) -> @location(0) vec4<f32> {
	return fragmap(
		in.rgba
		* textureSample(pattern, pattern_smpl, in.pattern_uv) * 16.0
		* textureLoad(atlas, vec2<i32>(i32(in.atlas_uv.x), i32(in.atlas_uv.y)), 0).r);
}
