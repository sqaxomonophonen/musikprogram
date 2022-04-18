fn xymap(xy: vec2<f32>) -> vec4<f32> {
	return vec4<f32>(
		(xy.x / u.width)  *  2.0 - 1.0,
		(xy.y / u.height) * -2.0 + 1.0,
		0.0, 1.0);
}

fn fragmap(rgba: vec4<f32>) -> vec4<f32> {
	return rgba * vec4<f32>(u.scalar, u.scalar, u.scalar, 1.0);
}
