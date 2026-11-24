#pragma once
#include "Backend/CodeGen/ImportPlanner.h"
#include "Backend/CodeGen/UnwindInference.h"
#include <string>
#include <unordered_set>
#include <vector>

namespace C4Compiler
{
	struct FunctionRecord;
	struct SymbolInfo;
	class Type;

	/// A static local a partition defines, which partition 0 declares for the GC roots.
	struct StaticStorageFact
	{
		std::string name;
		const Type* type = nullptr;
		bool threadLocal = false;

		bool operator==(const StaticStorageFact&) const = default;
	};

	/// What a partition's module tells the others while the program is emitted (docs/compiler/07 stage 2a). A build server
	/// keeps each partition's from the build that last emitted it, to emit again only the partitions an edit changed when
	/// the others would come out as they are.
	struct PartitionFacts
	{
		FactsByRecord functions;                     // its functions' facts before copies (ImportEmitter): the ones it defines
		std::vector<const FunctionRecord*> copies;   // what it copied from the others
		UnwindPart unwind;                           // its part of the call graph, and the answers it took
		std::vector<const SymbolInfo*> namedGlobals; // the globals out of reflection's scope its code names
		bool namedMore = false;                      // its code named more of them once their storage was made
		std::vector<StaticStorageFact> statics;      // its static locals
	};

	/// A build server's emission after an edit (ProgramBackend::EmitAgain): the partitions emitted this time - partition 0,
	/// the program's own, only when every partition is - and every partition's facts, the others' read, the emitted ones'
	/// kept. The emission says which other partitions would come out otherwise now (`stale`): they are emitted too.
	struct PartialEmission
	{
		std::vector<bool> emitted;                          // by partition
		const std::vector<PartitionFacts>* facts = nullptr; // by partition: what the build before kept
		std::unordered_set<const FunctionRecord*> changed;  // the functions whose trees changed since the facts were kept
		std::vector<bool> stale;                            // set by the emission: by partition
		std::vector<PartitionFacts> made;                   // set by the emission: by partition, the emitted ones'

		bool Emits(unsigned partition) const { return emitted[partition]; }
		bool Everything() const { return emitted[0]; }
	};
} // namespace C4Compiler
