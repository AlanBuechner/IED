#pragma once

#include "Core/BenchmarkResult.h"
#include <algorithm>
#include <cmath>

namespace Core
{
    /// Configuration for auto-scaling benchmark execution.
    struct BenchmarkConfig
    {
        /// Target total wall time in seconds; the runner scales iterations to approach this.
        double targetWallTime = 1.0;
        /// Number of iterations to start with before scaling.
        uint32 initialIterations = 16;
        /// Maximum number of iterations allowed (safety cap).
        uint32 maxIterations = 1048576;
        /// Geometric growth factor applied each scaling round.
        double growthFactor = 2.0;
        /// Minimum wall time before we consider the run stable enough to stop scaling.
        double minWallTime = 0.1;
    };

    /// Result of a Compare call, extending BenchmarkComparison with regression flagging.
    struct BenchmarkCompareResult
    {
        BenchmarkComparison comparison;

        /// True if B is slower than A by more than the regression threshold (percentage).
        bool isRegression = false;
        /// The regression threshold used for the comparison (percentage).
        double thresholdPercent = 0.0;
    };

    /// Core benchmark runner: executes callables with auto-scaling iterations and compares results.
    class BenchmarkRunner
    {
    public:
        /// Run a benchmark with geometric iteration scaling to reach target wall time.
        /// \param testName Name of the benchmark for reporting.
        /// \param func Callable to benchmark (executed once per iteration).
        /// \param config Scaling configuration; use defaults if unsure.
        /// \return Filled BenchmarkResult with computed statistics.
        static BenchmarkResult RunBenchmark(const String& testName, const std::function<void()>& func, const BenchmarkConfig& config = {});

        /// Compare two benchmark results and flag regression based on a percentage threshold.
        /// \param resultA Baseline result (reference).
        /// \param resultB Candidate result to compare against baseline.
        /// \param regressionThreshold Percent change threshold; positive values flag slowdowns as regressions.
        /// \return BenchmarkCompareResult with ratio, percent change, and regression flag.
        static BenchmarkCompareResult Compare(const BenchmarkResult& resultA, const BenchmarkResult& resultB, double regressionThreshold = 5.0);

    private:
        BenchmarkRunner() = default;
        ~BenchmarkRunner() = default;
        BenchmarkRunner(const BenchmarkRunner&) = delete;
        BenchmarkRunner& operator=(const BenchmarkRunner&) = delete;
    };
}
