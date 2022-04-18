fn rnd(xy: vec2<f32>, seed: f32) -> f32 {
	return fract(tan(distance(xy*1.618034, xy)*seed)*xy.x);
}
