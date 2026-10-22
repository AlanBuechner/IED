#pragma once
#include "Core/Core.h"
#include <string>
#include <unordered_map>

namespace Scripting
{
	class ProgramModel;
	struct FunctionRecord;
	struct SymbolInfo;

	/// Where a split program's functions and globals go (D-81), decided from the model before any code exists.
	/// Partition 0 holds the program's own code and data (global initialization, invoke thunks, reflection, the GC
	/// roots, the entry point); a function goes by the file it is defined in (a template instance by its template's),
	/// over that file's share of the other partitions by name, so an edit changes only its file's partitions; one the
	/// compiler wrote by its name; a global by the file that declares it. One partition: everything is partition 0.
	class ProgramPlacement
	{
	public:
		ProgramPlacement(const ProgramModel& model, unsigned count);

		unsigned Count() const { return m_Count; }
		unsigned Of(const FunctionRecord& rec) const;
		unsigned Of(const SymbolInfo& global) const;

		/// How many partitions `model`'s program is worth: one per kFunctionsPerPartition functions written with a
		/// body, rounded down to a power of two (so the count changes only when the program doubles or halves), 1 to
		/// kMaxPartitions.
		static unsigned CountFor(const ProgramModel& model);

	private:
		unsigned InFile(uint32 sourceId, const std::string& name) const;

		unsigned m_Count;
		std::unordered_map<uint32, uint64> m_FileHashes; // by source id: its path's
		std::unordered_map<uint32, uint64> m_Shares;     // by source id: how many partitions its functions spread over
	};
} // namespace Scripting
