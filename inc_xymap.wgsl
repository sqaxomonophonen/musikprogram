fn xymap(xy: vec2<f32>) -> vec4<f32> {
	return vec4<f32>(
		(xy.x / f32(u.dst_dim.x)) *  2.0 - 1.0,
		(xy.y / f32(u.dst_dim.y)) * -2.0 + 1.0,
		0.0, 1.0);
}
