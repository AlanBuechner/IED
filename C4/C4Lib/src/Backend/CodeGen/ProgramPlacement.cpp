#include "Backend/CodeGen/ProgramPlacement.h"
#include "Model/ProgramModel.h"
#include <llvm/Support/xxhash.h>
#include <algorithm>
#include <bit>

namespace Scripting
{
	namespace
	{
		constexpr uint64 kFunctionsPerPartition = 24;
		constexpr uint64 kMaxPartitions = 64;
		// A file's functions spread over one partition per this many of them, rounded down to a power of two, so its
		// share changes only when the file doubles or halves.
		constexpr uint64 kFunctionsPerFileShare = 48;

		bool Written(const FunctionRecord& rec)
		{
			return rec.bodyNode && rec.templateOrigin == TemplateOrigin::None && rec.synthesized == LifecycleOp::None && !rec.isLocal;
		}

		std::unordered_map<uint32, uint64> WrittenPerFile(const ProgramModel& model)
		{
			Core::Vector<const FunctionRecord*> records;
			model.AllFunctionRecords(records);
			std::unordered_map<uint32, uint64> counts;
			for (const FunctionRecord* rec : records)
				if (Written(*rec)) counts[rec->definingUnit]++;
			return counts;
		}
	}

	ProgramPlacement::ProgramPlacement(const ProgramModel& model, unsigned count) : m_Count(std::max(count, 1u))
	{
		if (m_Count == 1) return;
		for (const auto& [sourceId, path] : model.SourcePaths()) m_FileHashes[sourceId] = llvm::xxh3_64bits(path);
		for (const auto& [sourceId, functions] : WrittenPerFile(model))
			m_Shares[sourceId] = std::bit_floor(std::clamp<uint64>(functions / kFunctionsPerFileShare, 1, m_Count - 1));
	}

	unsigned ProgramPlacement::CountFor(const ProgramModel& model)
	{
		uint64 written = 0;
		for (const auto& [sourceId, functions] : WrittenPerFile(model)) written += functions;
		return static_cast<unsigned>(std::bit_floor(std::clamp<uint64>(written / kFunctionsPerPartition, 1, kMaxPartitions)));
	}

	unsigned ProgramPlacement::Of(const FunctionRecord& rec) const
	{
		return InFile(rec.bodyNode ? rec.definingUnit : 0, rec.mangledName.c_str());
	}

	unsigned ProgramPlacement::Of(const SymbolInfo& global) const
	{
		return InFile(global.definingUnit, global.mangledName.c_str());
	}

	// A name with no file (one the compiler wrote) goes by its name alone.
	unsigned ProgramPlacement::InFile(uint32 sourceId, const std::string& name) const
	{
		if (m_Count == 1) return 0;
		const uint64 others = m_Count - 1;
		const uint64 byName = llvm::xxh3_64bits(name);
		const auto file = m_FileHashes.find(sourceId);
		if (file == m_FileHashes.end()) return static_cast<unsigned>(1 + byName % others);
		const auto share = m_Shares.find(sourceId);
		return static_cast<unsigned>(1 + (file->second + byName % (share == m_Shares.end() ? 1 : share->second)) % others);
	}
} // namespace Scripting
