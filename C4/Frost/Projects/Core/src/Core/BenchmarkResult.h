#pragma once

#include "Core/Time.h"
#include "Core/String.h"
#include <vector>
#include <numeric>
#include <algorithm>
#include <cmath>
#include <limits>
#include <functional>

namespace Core
{
    /// Statistics computed from a single benchmark run's timing samples.
    struct BenchmarkStats
    {
        double mean = 0.0;
        double median = 0.0;
        double stddev = 0.0;
        double minTime = std::numeric_limits<double>::max();
        double maxTime = std::numeric_limits<double>::lowest();
        uint32 iterations = 0;

        /// Compute statistics from a collection of per-iteration timing values (seconds).
        void ComputeFromSamples(const std::vector<double>& samples)
        {
            if (samples.empty())
            {
                mean = 0.0;
                median = 0.0;
                stddev = 0.0;
                minTime = 0.0;
                maxTime = 0.0;
                iterations = 0;
                return;
            }

            iterations = static_cast<uint32>(samples.size());
            minTime = *std::min_element(samples.begin(), samples.end());
            maxTime = *std::max_element(samples.begin(), samples.end());

            // Mean
            double sum = std::accumulate(samples.begin(), samples.end(), 0.0);
            mean = sum / samples.size();

            // Median
            std::vector<double> sortedSamples = samples;
            std::sort(sortedSamples.begin(), sortedSamples.end());
            size_t mid = sortedSamples.size() / 2;
            if (sortedSamples.size() % 2 == 0)
            {
                median = (sortedSamples[mid - 1] + sortedSamples[mid]) * 0.5;
            }
            else
            {
                median = sortedSamples[mid];
            }

            // Standard deviation (population)
            double sqSum = 0.0;
            for (double s : samples)
            {
                double diff = s - mean;
                sqSum += diff * diff;
            }
            stddev = std::sqrt(sqSum / samples.size());
        }
    };

    /// Result of a single benchmark execution, pairing a name with computed stats.
    struct BenchmarkResult
    {
        String name;
        BenchmarkStats stats;
        double totalWallTime = 0.0;

        /// Convenience: run a benchmark function N times and fill this result.
        static BenchmarkResult Run(const String& testName, const std::function<void()>& func, uint32 iterations);
    };

    /// Comparison of two benchmark results, expressing relative performance change.
    struct BenchmarkComparison
    {
        String nameA;
        String nameB;

        double meanA = 0.0;
        double meanB = 0.0;

        /// Ratio B/A (>1 means B is slower, <1 means B is faster).
        double ratio = 0.0;
        /// Percentage change: ((B - A) / A) * 100.
        double percentChange = 0.0;

        static BenchmarkComparison Compare(const BenchmarkResult& resultA, const BenchmarkResult& resultB);
    };

    /// Historical record of benchmark results for tracking regression over time.
    struct HistoricalBenchmarkData
    {
        String benchmarkName;
        std::vector<BenchmarkResult> history;

        /// Append a new result to the history log.
        void AddResult(const BenchmarkResult& result);

        /// Get the earliest recorded mean for this benchmark (baseline).
        double GetBaselineMean() const;

        /// Get the most recent recorded mean.
        double GetLatestMean() const;

        /// Compare latest result against baseline; returns 0.0 if no history.
        double RegressionPercent() const;
    };

    /// Lightweight stopwatch wrapping Core::Time::GetTime().
    class Stopwatch
    {
    public:
        Stopwatch() = default;

        /// Start (or restart) the stopwatch.
        void Start();

        /// Stop the stopwatch and record elapsed time since last Start/Reset.
        void Stop();

        /// Reset the stopwatch without recording — discards any running state.
        void Reset();

        /// Elapsed time in seconds since Start (updates live if still running).
        double GetElapsedSeconds() const;

        /// Elapsed time in milliseconds.
        double GetElapsedMilliseconds() const;

        /// Whether the stopwatch is currently running.
        bool IsRunning() const { return m_Running; }

    private:
        double m_StartTime = 0.0;
        double m_ElapsedTime = 0.0;
        bool m_Running = false;
    };

    // ------------------------------------------------------------------
    // Inline implementations
    // ------------------------------------------------------------------

    inline void Stopwatch::Start()
    {
        if (m_Running)
        {
            // Accumulate existing elapsed before restarting
            m_ElapsedTime += Time::GetTime() - m_StartTime;
        }
        m_StartTime = Time::GetTime();
        m_Running = true;
    }

    inline void Stopwatch::Stop()
    {
        if (m_Running)
        {
            m_ElapsedTime += Time::GetTime() - m_StartTime;
            m_Running = false;
        }
    }

    inline void Stopwatch::Reset()
    {
        m_StartTime = 0.0;
        m_ElapsedTime = 0.0;
        m_Running = false;
    }

    inline double Stopwatch::GetElapsedSeconds() const
    {
        if (m_Running)
        {
            return m_ElapsedTime + (Time::GetTime() - m_StartTime);
        }
        return m_ElapsedTime;
    }

    inline double Stopwatch::GetElapsedMilliseconds() const
    {
        return GetElapsedSeconds() * 1000.0;
    }

    inline void HistoricalBenchmarkData::AddResult(const BenchmarkResult& result)
    {
        history.push_back(result);
    }

    inline double HistoricalBenchmarkData::GetBaselineMean() const
    {
        if (history.empty()) return 0.0;
        return history.front().stats.mean;
    }

    inline double HistoricalBenchmarkData::GetLatestMean() const
    {
        if (history.empty()) return 0.0;
        return history.back().stats.mean;
    }

    inline double HistoricalBenchmarkData::RegressionPercent() const
    {
        if (history.size() < 2) return 0.0;
        double baseline = GetBaselineMean();
        if (baseline <= 0.0) return 0.0;
        return ((GetLatestMean() - baseline) / baseline) * 100.0;
    }

    // Non-inline out-of-line definitions (implemented in header for self-containment)
    inline BenchmarkResult BenchmarkResult::Run(const String& testName, const std::function<void()>& func, uint32 iterations)
    {
        BenchmarkResult result;
        result.name = testName;

        std::vector<double> samples;
        samples.reserve(iterations);

        Stopwatch sw;
        sw.Start();

        for (uint32 i = 0; i < iterations; ++i)
        {
            double iterStart = Time::GetTime();
            func();
            double iterEnd = Time::GetTime();
            samples.push_back(iterEnd - iterStart);
        }

        result.totalWallTime = sw.GetElapsedSeconds();
        sw.Stop();

        result.stats.ComputeFromSamples(samples);
        return result;
    }

    inline BenchmarkComparison BenchmarkComparison::Compare(const BenchmarkResult& resultA, const BenchmarkResult& resultB)
    {
        BenchmarkComparison comp;
        comp.nameA = resultA.name;
        comp.nameB = resultB.name;
        comp.meanA = resultA.stats.mean;
        comp.meanB = resultB.stats.mean;

        if (resultA.stats.mean > 0.0)
        {
            comp.ratio = resultB.stats.mean / resultA.stats.mean;
            comp.percentChange = ((resultB.stats.mean - resultA.stats.mean) / resultA.stats.mean) * 100.0;
        }

        return comp;
    }
}
