#include "BenchmarkHistory.h"
#include <fstream>
#include <algorithm>
#include <ctime>
#include <iomanip>
#include <sstream>

namespace Core
{
    // ------------------------------------------------------------------
    // BenchmarkHistoryEntry
    // ------------------------------------------------------------------

    BenchmarkHistoryEntry BenchmarkHistoryEntry::From(const BenchmarkResult& result, const String& label)
    {
        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);

        std::tm tmBuf{};
#ifdef _WIN32
        localtime_s(&tmBuf, &timeT);
#else
        localtime_r(&timeT, &tmBuf);
#endif

        std::ostringstream oss;
        oss << std::put_time(&tmBuf, "%Y-%m-%dT%H:%M:%S");
        String timestamp = oss.str();

        BenchmarkHistoryEntry entry;
        entry.timestamp = std::move(timestamp);
        entry.label = label;
        entry.result = result;
        return entry;
    }

    // ------------------------------------------------------------------
    // BenchmarkHistory
    // ------------------------------------------------------------------

    BenchmarkHistory::BenchmarkHistory(const BenchmarkHistoryConfig& config)
        : m_Config(config), m_PinnedTimestamp()
    {
        EnsureDirectory();
    }

    void BenchmarkHistory::EnsureDirectory() const
    {
        fs::create_directories(m_Config.historyDirectory);
    }

    String BenchmarkHistory::GenerateSnapshotFilename() const
    {
        auto now = std::chrono::system_clock::now();
        auto timeT = std::chrono::system_clock::to_time_t(now);
        auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()) % 1000;

        std::tm tmBuf{};
#ifdef _WIN32
        localtime_s(&tmBuf, &timeT);
#else
        localtime_r(&timeT, &tmBuf);
#endif

        std::ostringstream oss;
        // Format: benchmark-YYYYMMDD-HHMMSS-mmm.json  (guarantees uniqueness within ms)
        oss << "benchmark-"
            << std::put_time(&tmBuf, "%Y%m%d-%H%M%S")
            << "-" << std::setfill('0') << std::setw(3) << millis.count()
            << ".json";
        return oss.str();
    }

    fs::path BenchmarkHistory::GetSnapshotPath(const String& filename) const
    {
        return m_Config.historyDirectory / filename;
    }

    // ------------------------------------------------------------------
    // Serialization helpers
    // ------------------------------------------------------------------

    Core::ObjectDescription BenchmarkHistory::ResultToDescription(const BenchmarkResult& result)
    {
        ObjectDescription desc(ObjectDescription::Type::Object);

        auto& map = desc.GetAsDescriptionMap();
        map["name"] = ObjectDescription::CreateFrom(result.name);
        map["totalWallTime"] = ObjectDescription::CreateFrom(result.totalWallTime);

        // Serialize BenchmarkStats as a nested object
        ObjectDescription statsDesc(ObjectDescription::Type::Object);
        auto& statsMap = statsDesc.GetAsDescriptionMap();
        statsMap["mean"] = ObjectDescription::CreateFrom(result.stats.mean);
        statsMap["median"] = ObjectDescription::CreateFrom(result.stats.median);
        statsMap["stddev"] = ObjectDescription::CreateFrom(result.stats.stddev);
        statsMap["minTime"] = ObjectDescription::CreateFrom(result.stats.minTime);
        statsMap["maxTime"] = ObjectDescription::CreateFrom(result.stats.maxTime);
        statsMap["iterations"] = ObjectDescription::CreateFrom(result.stats.iterations);

        map["stats"] = std::move(statsDesc);

        return desc;
    }

    BenchmarkResult BenchmarkHistory::DescriptionToResult(const Core::ObjectDescription& desc)
    {
        BenchmarkResult result;

        const auto& map = desc.GetAsDescriptionMap();

        auto itName = map.find("name");
        if (itName != map.end())
            result.name = itName->second.GetAsString();

        auto itWall = map.find("totalWallTime");
        if (itWall != map.end())
            result.totalWallTime = itWall->second.GetAsFloat();

        auto itStats = map.find("stats");
        if (itStats != map.end() && itStats->second.IsObject())
        {
            const auto& statsMap = itStats->second.GetAsDescriptionMap();

            auto getStat = [&](const String& key) -> double
            {
                auto it = statsMap.find(key);
                return (it != statsMap.end()) ? it->second.GetAsFloat() : 0.0;
            };

            result.stats.mean = getStat("mean");
            result.stats.median = getStat("median");
            result.stats.stddev = getStat("stddev");
            result.stats.minTime = getStat("minTime");
            result.stats.maxTime = getStat("maxTime");

            auto itIter = statsMap.find("iterations");
            if (itIter != statsMap.end())
                result.stats.iterations = static_cast<uint32>(itIter->second.GetAsUint());
        }

        return result;
    }

    // ------------------------------------------------------------------
    // Save / Load
    // ------------------------------------------------------------------

    String BenchmarkHistory::SaveResult(const BenchmarkResult& result, const String& label)
    {
        EnsureDirectory();

        String filename = GenerateSnapshotFilename();
        fs::path filePath = GetSnapshotPath(filename);

        // Build the full JSON object: { "timestamp": "...", "label": "...", "result": { ... } }
        ObjectDescription rootDesc(ObjectDescription::Type::Object);
        auto& rootMap = rootDesc.GetAsDescriptionMap();

        auto entry = BenchmarkHistoryEntry::From(result, label);
        rootMap["timestamp"] = ObjectDescription::CreateFrom(entry.timestamp);
        rootMap["label"] = ObjectDescription::CreateFrom(entry.label);
        rootMap["result"] = ResultToDescription(result);

        String json = SaveObjectDescriptionToJson(rootDesc);

        std::ofstream file(filePath, std::ios::binary | std::ios::trunc);
        if (!file.is_open())
        {
            CORE_ERROR("BenchmarkHistory: failed to open file for writing: {}", filePath.string());
            return "";
        }
        file.write(json.data(), static_cast<std::streamsize>(json.size()));
        file.close();

        // Prune old snapshots after saving.
        PruneOldSnapshots();

        return filePath.string();
    }

    Core::Vector<BenchmarkHistoryEntry> BenchmarkHistory::LoadAll() const
    {
        Core::Vector<BenchmarkHistoryEntry> entries;

        if (!fs::exists(m_Config.historyDirectory) || !fs::is_directory(m_Config.historyDirectory))
            return entries;

        // Collect all .json files and sort by name (timestamped names sort chronologically).
        Core::Vector<fs::path> jsonFiles;
        for (const auto& entry : fs::directory_iterator(m_Config.historyDirectory))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                jsonFiles.Push(entry.path());
        }

        std::sort(jsonFiles.begin(), jsonFiles.end());

        for (const auto& filePath : jsonFiles)
        {
            try
            {
                ObjectDescription desc = LoadObjectDescriptionFromJsonFile(filePath);

                if (!desc.IsObject())
                    continue;

                const auto& map = desc.GetAsDescriptionMap();

                BenchmarkHistoryEntry entry;

                auto itTs = map.find("timestamp");
                if (itTs != map.end())
                    entry.timestamp = itTs->second.GetAsString();

                auto itLabel = map.find("label");
                if (itLabel != map.end())
                    entry.label = itLabel->second.GetAsString();

                auto itResult = map.find("result");
                if (itResult != map.end() && itResult->second.IsObject())
                    entry.result = DescriptionToResult(itResult->second);
                else
                    continue; // skip corrupt files

                entries.Push(entry);
            }
            catch (...)
            {
                CORE_ERROR("BenchmarkHistory: failed to load {}", filePath.string());
            }
        }

        return entries;
    }

    std::optional<BenchmarkHistoryEntry> BenchmarkHistory::LoadLatest() const
    {
        auto all = LoadAll();
        if (all.Empty())
            return std::nullopt;
        return all[all.Count() - 1]; // sorted oldest-first, so last is latest.
    }

    // ------------------------------------------------------------------
    // CompareWithHistory
    // ------------------------------------------------------------------

    BenchmarkHistoryComparison BenchmarkHistory::CompareWithHistory(
        const BenchmarkResult& currentResult, double threshold) const
    {
        if (threshold <= 0.0)
            threshold = m_Config.defaultRegressionThreshold;

        BenchmarkHistoryComparison comp;
        comp.thresholdPercent = threshold;

        auto all = LoadAll();
        if (all.Empty())
            return comp;

        // Latest entry is the last in the sorted list.
        comp.latestEntry = all[all.Count() - 1];

        // Percentage change vs latest
        double latestMean = comp.latestEntry->result.stats.mean;
        if (latestMean > 0.0)
        {
            comp.percentChangeVsLatest = ((currentResult.stats.mean - latestMean) / latestMean) * 100.0;
            comp.regressionVsLatest = comp.percentChangeVsLatest > threshold;
        }

        // Find pinned baseline if set.
        if (!m_PinnedTimestamp.empty())
        {
            for (const auto& entry : all)
            {
                if (entry.timestamp == m_PinnedTimestamp)
                {
                    comp.pinnedBaseline = entry;
                    break;
                }
            }

            if (comp.pinnedBaseline.has_value())
            {
                double pinnedMean = comp.pinnedBaseline->result.stats.mean;
                if (pinnedMean > 0.0)
                {
                    comp.percentChangeVsPinned = ((currentResult.stats.mean - pinnedMean) / pinnedMean) * 100.0;
                    comp.regressionVsPinned = comp.percentChangeVsPinned > threshold;
                }
            }
        }

        return comp;
    }

    // ------------------------------------------------------------------
    // Pin / Unpin
    // ------------------------------------------------------------------

    void BenchmarkHistory::PinBaseline(const String& timestamp)
    {
        m_PinnedTimestamp = timestamp;
    }

    void BenchmarkHistory::UnpinBaseline()
    {
        m_PinnedTimestamp.clear();
    }

    // ------------------------------------------------------------------
    // Prune / Clear
    // ------------------------------------------------------------------

    uint32 BenchmarkHistory::PruneOldSnapshots() const
    {
        if (m_Config.maxSnapshots == 0)
            return 0; // unlimited

        // Collect all JSON files in the directory, sorted by name (oldest first).
        Core::Vector<fs::path> jsonFiles;
        if (!fs::exists(m_Config.historyDirectory) || !fs::is_directory(m_Config.historyDirectory))
            return 0;

        for (const auto& entry : fs::directory_iterator(m_Config.historyDirectory))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
                jsonFiles.Push(entry.path());
        }

        std::sort(jsonFiles.begin(), jsonFiles.end());

        uint32 fileCount = jsonFiles.Count();
        if (fileCount <= m_Config.maxSnapshots)
            return 0; // nothing to prune

        uint32 removed = 0;
        uint32 toRemove = fileCount - m_Config.maxSnapshots;
        for (uint32 i = 0; i < toRemove; ++i)
        {
            try
            {
                fs::remove(jsonFiles[i]);
                ++removed;
            }
            catch (...)
            {
                CORE_ERROR("BenchmarkHistory: failed to remove {}", jsonFiles[i].string());
            }
        }

        return removed;
    }

    uint32 BenchmarkHistory::ClearHistory() const
    {
        uint32 removed = 0;

        if (!fs::exists(m_Config.historyDirectory) || !fs::is_directory(m_Config.historyDirectory))
            return 0;

        for (const auto& entry : fs::directory_iterator(m_Config.historyDirectory))
        {
            if (entry.is_regular_file() && entry.path().extension() == ".json")
            {
                try
                {
                    fs::remove(entry.path());
                    ++removed;
                }
                catch (...)
                {
                    CORE_ERROR("BenchmarkHistory: failed to remove {}", entry.path().string());
                }
            }
        }

        return removed;
    }
}
