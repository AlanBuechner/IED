#pragma once

#include "ObjectDescription/JsonObjectDescription.h"
#include "Core/BenchmarkResult.h"
#include "Core/StringFormat.h"
#include <filesystem>
#include <optional>
#include <chrono>

namespace Core
{
    /// Configuration for the benchmark history persistence system.
    struct BenchmarkHistoryConfig
    {
        /// Directory where timestamped JSON files are stored.
        fs::path historyDirectory = "benchmark-history";
        /// Maximum number of JSON snapshots to retain (0 = unlimited).
        uint32 maxSnapshots = 64;
        /// Default regression threshold in percent for CompareWithHistory.
        double defaultRegressionThreshold = 5.0;
    };

    /// Single persisted benchmark entry, wrapping a result with metadata.
    struct BenchmarkHistoryEntry
    {
        /// ISO-8601 timestamp string (e.g. "2026-06-29T10:12:45").
        String timestamp;
        /// Human-readable label for the run (commit hash, build tag, etc.).
        String label;
        /// The benchmark result captured at this point in time.
        BenchmarkResult result;

        /// Create an entry from a result and optional label.
        static BenchmarkHistoryEntry From(const BenchmarkResult& result, const String& label = "");
    };

    /// Result of comparing the current benchmark against historical data.
    struct BenchmarkHistoryComparison
    {
        /// The latest persisted entry used as the comparison baseline.
        std::optional<BenchmarkHistoryEntry> latestEntry;
        /// The pinned baseline entry (if a pin is set).
        std::optional<BenchmarkHistoryEntry> pinnedBaseline;

        /// Percentage change vs latest: ((currentMean - latestMean) / latestMean) * 100.
        double percentChangeVsLatest = 0.0;
        /// Percentage change vs pinned baseline (N/A if no pin).
        double percentChangeVsPinned = 0.0;

        /// True when the current result regresses beyond threshold vs latest.
        bool regressionVsLatest = false;
        /// True when the current result regresses beyond threshold vs pinned baseline.
        bool regressionVsPinned = false;

        /// Regression threshold used for both comparisons.
        double thresholdPercent = 0.0;
    };

    /// Manages persistent benchmark history as timestamped JSON snapshots.
    class BenchmarkHistory
    {
    public:
        /// Construct with a configuration (directory, retention, etc.).
        explicit BenchmarkHistory(const BenchmarkHistoryConfig& config = {});

        // Prevent copying/moving.
        BenchmarkHistory(const BenchmarkHistory&) = delete;
        BenchmarkHistory& operator=(const BenchmarkHistory&) = delete;

        /// Save a benchmark result as a new timestamped JSON file.
        /// \param result  The benchmark result to persist.
        /// \param label   Optional human-readable label for the snapshot.
        /// \return The path of the created JSON file, or empty string on failure.
        String SaveResult(const BenchmarkResult& result, const String& label = "");

        /// Load all historical entries from the configured directory (oldest first).
        Core::Vector<BenchmarkHistoryEntry> LoadAll() const;

        /// Load the most recent entry, if any exist.
        std::optional<BenchmarkHistoryEntry> LoadLatest() const;

        /// Compare a current result against the latest (and optionally pinned) history.
        /// \param currentResult  The freshly-run benchmark to compare.
        /// \param threshold      Regression threshold in percent.
        /// \return Comparison struct with percentage changes and regression flags.
        BenchmarkHistoryComparison CompareWithHistory(const BenchmarkResult& currentResult, double threshold = 0.0) const;

        /// Set a pinned baseline by timestamp string (must match an existing entry's timestamp).
        void PinBaseline(const String& timestamp);

        /// Clear the pinned baseline.
        void UnpinBaseline();

        /// Get the currently pinned baseline timestamp, or empty string if none.
        const String& GetPinnedTimestamp() const { return m_PinnedTimestamp; }

        /// Remove old snapshots beyond maxSnapshots (oldest first). Returns count removed.
        uint32 PruneOldSnapshots() const;

        /// Delete all JSON files in the history directory. Returns count removed.
        uint32 ClearHistory() const;

    private:
        BenchmarkHistoryConfig m_Config;
        String m_PinnedTimestamp;

        /// Generate a unique timestamped filename for a new snapshot.
        String GenerateSnapshotFilename() const;

        /// Convert a BenchmarkResult into an ObjectDescription for JSON serialization.
        static Core::ObjectDescription ResultToDescription(const BenchmarkResult& result);

        /// Convert an ObjectDescription back into a BenchmarkResult.
        static BenchmarkResult DescriptionToResult(const Core::ObjectDescription& desc);

        /// Build the full path to a snapshot file from its filename.
        fs::path GetSnapshotPath(const String& filename) const;

        /// Ensure the history directory exists on disk.
        void EnsureDirectory() const;
    };
}
