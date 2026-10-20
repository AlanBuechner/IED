// A host built for x86-64-v3 (flags.txt): its scripts have AVX2 and FMA too.
namespace Fused
{
	packed float[8] Step(packed float[8] position, packed float[8] velocity, float dt, int32 depth);
}
