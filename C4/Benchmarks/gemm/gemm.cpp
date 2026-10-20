// Integer GEMM benchmark - C++ counterpart of gemm.cf. Identical flat-array layout and index
// math so the only difference is the compiler (C4 codegen vs clang -O2 on C++).
#include <cstdint>
#include <cstdio>

static int32_t gA[262144];
static int32_t gB[262144];
static int32_t gC[262144];

int main() {
	int32_t N = 512;
	int32_t i, j, k;
	for (i = 0; i < N * N; i++) {
		gA[i] = (i % 7) - 3;
		gB[i] = (i % 11) - 5;
	}
	for (i = 0; i < N * N; i++) gC[i] = 0;

	for (i = 0; i < N; i++) {
		for (j = 0; j < N; j++) {
			int32_t acc = 0;
			for (k = 0; k < N; k++) {
				acc += gA[i * N + k] * gB[k * N + j];
			}
			gC[i * N + j] = acc;
		}
	}

	int32_t sum = 0;
	for (i = 0; i < N * N; i++) sum += gC[i];
	printf("GEMM %d\n", (int)sum);
	return 0;
}
