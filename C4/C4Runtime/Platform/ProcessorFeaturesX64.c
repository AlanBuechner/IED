// What the processor this program runs on can do, for Bedrock's Platform::HasProcessorFeature (PlatformRuntimeC.h):
// CPUID's feature bits, and XGETBV for the vector registers the system saves on a thread switch, without which AVX and
// AVX-512 instructions fault however the processor supports them.

#include "PlatformRuntimeC.h"

#if defined(_WIN32)
#include <intrin.h>

static void ReadCpuid(int out[4], int leaf, int subleaf) { __cpuidex(out, leaf, subleaf); }
static uint64_t ReadXcr(unsigned index) { return _xgetbv(index); }
#else
#include <cpuid.h>

static void ReadCpuid(int out[4], int leaf, int subleaf)
{
	unsigned a = 0, b = 0, c = 0, d = 0;
	__cpuid_count((unsigned)leaf, (unsigned)subleaf, a, b, c, d);
	out[0] = (int)a; out[1] = (int)b; out[2] = (int)c; out[3] = (int)d;
}

// Spelled out: clang's _xgetbv needs the xsave feature on its caller.
static uint64_t ReadXcr(unsigned index)
{
	unsigned low = 0, high = 0;
	__asm__ volatile("xgetbv" : "=a"(low), "=d"(high) : "c"(index));
	return ((uint64_t)high << 32) | low;
}
#endif

enum
{
	kXsaveSse = 0x2,
	kXsaveAvx = 0x4,
	kXsaveAvx512 = 0xE0,   // the opmask registers and both halves of the upper ZMM state
};

typedef struct Leaves
{
	int basic[4];      // leaf 1: eax, ebx, ecx, edx
	int extended[4];   // leaf 7, subleaf 0
	int amd[4];        // leaf 0x80000001
} Leaves;

static int Bit(int word, int bit) { return (word >> bit) & 1; }

static void ReadLeaves(Leaves* leaves)
{
	int highest[4];
	ReadCpuid(highest, 0, 0);
	ReadCpuid(leaves->basic, 1, 0);
	if (highest[0] >= 7) ReadCpuid(leaves->extended, 7, 0);
	int highestAmd[4];
	ReadCpuid(highestAmd, (int)0x80000000, 0);
	if ((unsigned)highestAmd[0] >= 0x80000001u) ReadCpuid(leaves->amd, (int)0x80000001, 0);
}

// The vector state the system saves: 0 when it does not say (no OSXSAVE).
static uint64_t SavedState(const Leaves* leaves)
{
	return Bit(leaves->basic[2], 27) ? ReadXcr(0) : 0;
}

static uint64_t Feature(int present, int feature) { return present ? (uint64_t)1 << feature : 0; }

static uint64_t Detect(void)
{
	Leaves leaves = { { 0 }, { 0 }, { 0 } };
	ReadLeaves(&leaves);
	const int ecx = leaves.basic[2];
	const int ebx7 = leaves.extended[1];
	const int ecx7 = leaves.extended[2];
	const uint64_t saved = SavedState(&leaves);
	const int avxState = (saved & (kXsaveSse | kXsaveAvx)) == (kXsaveSse | kXsaveAvx);
	const int avx512State = avxState && (saved & kXsaveAvx512) == kXsaveAvx512;
	uint64_t features = 0;
	features |= Feature(Bit(ecx, 0), kProcessorSse3);
	features |= Feature(Bit(ecx, 9), kProcessorSsse3);
	features |= Feature(Bit(ecx, 19), kProcessorSse41);
	features |= Feature(Bit(ecx, 20), kProcessorSse42);
	features |= Feature(Bit(ecx, 23), kProcessorPopcnt);
	features |= Feature(Bit(ecx, 1), kProcessorPclmul);
	features |= Feature(Bit(ecx, 25), kProcessorAes);
	features |= Feature(Bit(ecx, 28) && avxState, kProcessorAvx);
	features |= Feature(Bit(ebx7, 5) && avxState, kProcessorAvx2);
	features |= Feature(Bit(ecx, 12) && avxState, kProcessorFma);
	features |= Feature(Bit(ecx, 29) && avxState, kProcessorF16c);
	features |= Feature(Bit(ebx7, 3), kProcessorBmi1);
	features |= Feature(Bit(ebx7, 8), kProcessorBmi2);
	features |= Feature(Bit(leaves.amd[2], 5), kProcessorLzcnt);
	features |= Feature(Bit(ebx7, 29), kProcessorSha);
	features |= Feature(Bit(ebx7, 16) && avx512State, kProcessorAvx512F);
	features |= Feature(Bit(ebx7, 17) && avx512State, kProcessorAvx512Dq);
	features |= Feature(Bit(ebx7, 30) && avx512State, kProcessorAvx512Bw);
	features |= Feature(Bit(ebx7, 31) && avx512State, kProcessorAvx512Vl);
	features |= Feature(Bit(ecx7, 10) && avxState, kProcessorVpclmulqdq);
	return features;
}

// Found once: every thread finds the same bits, so a race only repeats the work. The top bit says they are known.
uint64_t c4_system_processor_features(void)
{
	static volatile uint64_t known;
	const uint64_t kKnown = (uint64_t)1 << 63;
	uint64_t features = known;
	if (!(features & kKnown))
	{
		features = Detect() | kKnown;
		known = features;
	}
	return features & ~kKnown;
}
