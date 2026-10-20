// Host functions a script calls with 256- and 512-bit vectors. They recurse, so they are not inlined into the
// script and every call crosses the host/script boundary.
namespace Lanes
{
	packed float[8] Doubled(packed float[8] v, int32 depth);
	packed int64[8] Negated(packed int64[8] v, int32 depth);
	const int32 kHostFloatLanes = SIMD_FLOAT_LANES;
}
