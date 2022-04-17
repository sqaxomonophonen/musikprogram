struct vs_out {
	@builtin(position) position: vec4<f32>,
	@location(0) uv: vec2<f32>,
};

struct uniforms {
	dst_dim: vec2<i32>,
};

@group(0) @binding(0) var<uniform> u: uniforms;
@group(0) @binding(1) var tex: texture_2d<f32>;

@stage(vertex)
fn vs_main(@builtin(vertex_index) i: u32) -> vs_out {

	var p0  = vec2<f32>(-1.0,  1.0);
	var p1  = vec2<f32>( 1.0,  1.0);
	var p2  = vec2<f32>( 1.0, -1.0);
	var p3  = vec2<f32>(-1.0, -1.0);

	var w = u.dst_dim.x;
	var h = u.dst_dim.y;
	var uv0 = vec2<i32>( 0,  0 );
	var uv1 = vec2<i32>( w,  0 );
	var uv2 = vec2<i32>( w,  h );
	var uv3 = vec2<i32>( 0,  h );

	var pos: vec2<f32>;
	var uv:  vec2<i32>;

	if (i == 0u) {
		pos = p0;
		uv  = uv0;
	} else if (i == 1u) {
		pos = p1;
		uv  = uv1;
	} else if (i == 2u) {
		pos = p2;
		uv  = uv2;
	} else if (i == 3u) {
		pos = p0;
		uv  = uv0;
	} else if (i == 4u) {
		pos = p2;
		uv  = uv2;
	} else {
		pos = p3;
		uv  = uv3;
	}

	var out: vs_out;
	out.position = vec4<f32>(pos.x, pos.y, 0.0, 1.0);
	out.uv = vec2<f32>(uv);
	return out;
}

@stage(fragment)
fn fs_main(in: vs_out) -> @location(0) vec4<f32> {
	var uv = vec2<i32>(in.uv);

	var acc = vec4<f32>(0.0, 0.0, 0.0, 0.0);
	var N = 30;
	var i = -N;
	loop {
		if (i > N) { break; }
		let d = f32(i);
		var gauss = exp(-(d*d*0.005));
		var x = textureLoad(tex, uv + vec2<i32>(i,0), 0);
		acc += x * gauss;
		i=i+1;
	}

	acc *= 1.0/f32(N);

	return acc;
}
