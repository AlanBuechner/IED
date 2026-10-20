#pragma once

#include "UnitTesting/BenchmarkTest.h"
#include "Core/Vector.h"
#include "Core/Hash.h"
#include "Math/Math.h"

namespace Testing
{
	CLASS(GROUP = Benchmarking) CoreBenchmarks : public BenchmarkTest
	{
	public:
		REFLECTED_BODY(Testing::CoreBenchmarks);

		BENCHMARK_FUNC(Benchmarks.Vector)
		void BenchVectorPushBack();

		BENCHMARK_FUNC(Benchmarks.Hash)
		void BenchStringHash();

		BENCHMARK_FUNC(Benchmarks.Math)
		void BenchTrigonometry();
	};
}
