#include "Core/BenchmarkRunner.h"

namespace Core
{
    BenchmarkResult BenchmarkRunner::RunBenchmark(const String& testName, const std::function<void()>& func, const BenchmarkConfig& config)
    {
        double targetWallTime = config.targetWallTime;
        uint32 currentIterations = config.initialIterations;
        double growthFactor = config.growthFactor;
        uint32 maxIterations = config.maxIterations;
        double minWallTime = config.minWallTime;

        // Warm-up run to get a rough per-iteration time estimate.
        std::vector<double> warmupSamples;
        warmupSamples.reserve(config.initialIterations);
        for (uint32 i = 0; i < config.initialIterations; ++i)
        {
            double iterStart = Time::GetTime();
            func();
            double iterEnd = Time::GetTime();
            warmupSamples.push_back(iterEnd - iterStart);
        }

        // Estimate mean iteration time from warm-up.
        double warmupSum = 0.0;
        for (double s : warmupSamples)
        {
            warmupSum += s;
        }
        double estimatedPerIteration = warmupSum / static_cast<double>(warmupSamples.size());

        // Estimate how many iterations we need to reach target wall time.
        uint32 estimatedIterations = 1;
        if (estimatedPerIteration > 0.0)
        {
            estimatedIterations = static_cast<uint32>(std::ceil(targetWallTime / estimatedPerIteration));
        }

        // Start from the larger of initialIterations and the estimate, but cap at maxIterations.
        currentIterations = std::max(currentIterations, estimatedIterations);
        currentIterations = std::min(currentIterations, maxIterations);

        // Run scaling rounds: execute the benchmark, check wall time, and scale up if needed.
        BenchmarkResult result;
        Stopwatch sw;

        while (currentIterations <= maxIterations)
        {
            sw.Reset();
            sw.Start();

            std::vector<double> samples;
            samples.reserve(currentIterations);

            for (uint32 i = 0; i < currentIterations; ++i)
            {
                double iterStart = Time::GetTime();
                func();
                double iterEnd = Time::GetTime();
                samples.push_back(iterEnd - iterStart);
            }

            sw.Stop();
            double wallTime = sw.GetElapsedSeconds();

            // If we've reached minimum wall time and are within a reasonable factor of target, stop scaling.
            if (wallTime >= minWallTime && wallTime >= targetWallTime / growthFactor)
            {
                result.name = testName;
                result.totalWallTime = wallTime;
                result.stats.ComputeFromSamples(samples);
                return result;
            }

            // Scale up iterations geometrically.
            uint32 nextIterations = static_cast<uint32>(std::ceil(static_cast<double>(currentIterations) * growthFactor));
            nextIterations = std::min(nextIterations, maxIterations);

            // If scaling doesn't increase iteration count further, break to avoid an infinite loop.
            if (nextIterations <= currentIterations)
            {
                result.name = testName;
                result.totalWallTime = wallTime;
                result.stats.ComputeFromSamples(samples);
                return result;
            }

            currentIterations = nextIterations;
        }

        // Fallback: should not reach here, but return the last run's data.
        result.name = testName;
        result.totalWallTime = sw.GetElapsedSeconds();
        return result;
    }

    BenchmarkCompareResult BenchmarkRunner::Compare(const BenchmarkResult& resultA, const BenchmarkResult& resultB, double regressionThreshold)
    {
        BenchmarkCompareResult compareResult;
        compareResult.comparison = BenchmarkComparison::Compare(resultA, resultB);
        compareResult.thresholdPercent = regressionThreshold;

        // A regression is flagged when B is slower than A by more than the threshold percentage.
        // percentChange > 0 means B is slower (higher mean time).
        if (resultA.stats.mean > 0.0)
        {
            double percentChange = ((resultB.stats.mean - resultA.stats.mean) / resultA.stats.mean) * 100.0;
            compareResult.isRegression = percentChange > regressionThreshold;
        }
        else
        {
            compareResult.isRegression = false;
        }

        return compareResult;
    }
}
