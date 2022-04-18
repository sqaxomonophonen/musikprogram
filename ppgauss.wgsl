struct vs_out {
	@builtin(position) position: vec4<f32>,
	@location(0) xy: vec2<f32>,
};

struct uniforms {
	dst_dim: vec2<f32>,
	seed: f32,
	sigma: f32,
	rstep: f32,
	broken: f32,
	n0: i32,
	n1: i32,
};

@group(0) @binding(0) var<uniform> u: uniforms;
@group(0) @binding(1) var tex: texture_2d<f32>;
@group(0) @binding(2) var smpl: sampler;

@stage(vertex)
fn vs_main(@builtin(vertex_index) i: u32) -> vs_out {

	var p0  = vec2<f32>(-1.0,  1.0);
	var p1  = vec2<f32>( 1.0,  1.0);
	var p2  = vec2<f32>( 1.0, -1.0);
	var p3  = vec2<f32>(-1.0, -1.0);

	var w = i32(u.dst_dim.x);
	var h = i32(u.dst_dim.y);
	var xy0 = vec2<i32>( 0,  0 );
	var xy1 = vec2<i32>( w,  0 );
	var xy2 = vec2<i32>( w,  h );
	var xy3 = vec2<i32>( 0,  h );

	var pos: vec2<f32>;
	var xy:  vec2<i32>;

	if (i == 0u) {
		pos = p0;
		xy  = xy0;
	} else if (i == 1u) {
		pos = p1;
		xy  = xy1;
	} else if (i == 2u) {
		pos = p2;
		xy  = xy2;
	} else if (i == 3u) {
		pos = p0;
		xy  = xy0;
	} else if (i == 4u) {
		pos = p2;
		xy  = xy2;
	} else {
		pos = p3;
		xy  = xy3;
	}

	var out: vs_out;
	out.position = vec4<f32>(pos.x, pos.y, 0.0, 1.0);
	out.xy = vec2<f32>(xy);
	return out;
}

#include "inc_rnd.wgsl"

fn rvec(d: f32, theta: f32) -> vec2<f32> {
	return vec2<f32>(d*sin(theta), d*cos(theta));
}

@stage(fragment)
fn fs_main(in: vs_out) -> @location(0) vec4<f32> {
	var acc = textureLoad(tex, vec2<i32>(in.xy), 0);
	var diminish = f32(0.0);
	var intensity = 0.40 * (1.0 - diminish * 0.3);
	var i0 = i32(0);
	var broken_rstep = u.rstep * (1.0 - u.broken);
	loop {
		if (i0 >= u.n0) { break; }
		var r = rnd(in.xy, u.seed) * broken_rstep - broken_rstep*0.5;
		var scalar = (exp(-(r*r*u.sigma)) / f32(u.n1));
		var i1 = i32(0);
		loop {
			if (i1 >= u.n1) { break; }
			i1+=1;
			var rtheta = rnd(in.xy, u.seed+f32(i1))*6.2830;
			var uv = (in.xy + rvec(r, rtheta)) / u.dst_dim;
			acc += textureSample(tex, smpl, uv) * scalar;

		}
		i0+=1;
	}
	return acc;
}
