#include "CoreBenchmarks.h"

void Testing::CoreBenchmarks::BenchVectorPushBack()
{
	Core::BenchmarkResult result = Core::BenchmarkRunner::RunBenchmark(
		"VectorPushBack", []() {
			Core::Vector<int32> vec;
			vec.Reserve(1024);
			for (int32 i = 0; i < 1024; ++i)
				vec.Push(i);
		});
	BENCHMARK_PASSED(result);
}

void Testing::CoreBenchmarks::BenchStringHash()
{
	Core::BenchmarkResult result = Core::BenchmarkRunner::RunBenchmark(
		"CompileTimeCRC32", []() {
			constexpr char str1[] = "benchmark_test_string_1";
			constexpr char str2[] = "benchmark_test_string_2";
			volatile uint32 h1 = COMPILE_TIME_CRC32_STR(str1);
			volatile uint32 h2 = COMPILE_TIME_CRC32_STR(str2);
			(void)h1; (void)h2;
		});
	BENCHMARK_PASSED(result);
}

void Testing::CoreBenchmarks::BenchTrigonometry()
{
	Core::BenchmarkResult result = Core::BenchmarkRunner::RunBenchmark(
		"Trigonometry", []() {
			double angle = 0.0;
			for (uint32 i = 0; i < 10000; ++i)
			{
				angle += 0.001;
				double s = Math::Sin(angle);
				double c = Math::Cos(angle);
				double t = Math::Tan(angle);
				(void)s; (void)c; (void)t;
			}
		});
	BENCHMARK_PASSED(result);
}
