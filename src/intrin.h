inline f32
kh_clamp_f32(f32 min, f32 max, f32 val) {
	f32 res = val;
	if(res < min) {
		res = min;
	}
	if(res > max) {
		res = max;
	}
	return(res);
}

inline i32
kh_abs_i32(i32 val) {
	i32 res = abs(val);
	return(res);
}
