namespace M {
	template<T> T Clamp(T v, T lo, T hi) { return v < lo ? lo : (v > hi ? hi : v); }
	float Round(float v) { return v; }
}
