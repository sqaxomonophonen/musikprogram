struct VertexOutput {
	@builtin(position) position: vec4<f32>,
	@location(0) rgba: vec4<f32>,
};

struct Uniforms {
	dst_dim: vec2<u32>,
};
@group(0) @binding(0) var<uniform> u: Uniforms;

@stage(vertex)
fn vs_main(
	@location(0) xy: vec2<f32>,
	@location(1) rgba: vec4<f32>,
) -> VertexOutput {
	var out: VertexOutput;
	out.position = vec4<f32>(
		xy.x / f32(u.dst_dim.x),
		xy.y / f32(u.dst_dim.y),
		0.0,
		1.0);
	return out;
}

@stage(fragment)
fn fs_main(in: VertexOutput) -> @location(0) vec4<f32> {
	return in.rgba;
}
