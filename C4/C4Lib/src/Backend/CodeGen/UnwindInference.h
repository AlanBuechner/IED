#pragma once
#include <llvm/ADT/StringMap.h>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace llvm
{
	class Module;
}

namespace C4Compiler
{
	class WorkerPool;

	/// A function of a module that may unwind, and what it calls directly that may: a node of the program's call graph.
	struct UnwindCaller
	{
		std::string name;
		bool opaque = false;                               // it calls through a pointer
		std::vector<std::pair<std::string, bool>> callees; // by name, and whether its module defines the callee
	};

	/// What one module puts into the program's call graph, by name, so a build server can keep a partition's while it does
	/// not emit the partition again (docs/compiler/07), and what the graph answered it.
	struct UnwindPart
	{
		std::vector<UnwindCaller> callers;                // its definitions that may unwind
		std::vector<std::pair<std::string, bool>> shared; // its definitions another module may call: name, never unwinds itself
		/// Set when the answers are applied: each function of the module that may unwind - name, defined there - and
		/// whether the graph says it never does.
		std::vector<std::tuple<std::string, bool, bool>> answered;
	};

	/// The program's call graph over the parts of its modules, and which functions can unwind: the ones that call
	/// something opaque - a call through a pointer, a declaration no module defines that is not nounwind (the runtime's
	/// are, but for the few that run C4 code) - and their callers; a noexcept function never does.
	class UnwindGraph
	{
	public:
		explicit UnwindGraph(const std::vector<const UnwindPart*>& parts);

		/// Whether the function `name` of part `part` - defined there, or declared and defined in another - never unwinds.
		bool NeverUnwinds(size_t part, const std::string& name, bool definedThere) const;

	private:
		/// The node a direct call reaches, c_NeverUnwinds, or c_Opaque.
		int Resolve(size_t part, const std::string& name, bool definedThere) const;
		/// Whether node `node` calls something opaque; it is recorded as a caller of the nodes it calls.
		bool Link(int node, std::vector<std::vector<int>>& callersOf) const;
		void FindUnwinding();

		std::vector<const UnwindCaller*> m_Callers;
		std::vector<size_t> m_PartOf;                // each node's part
		std::vector<llvm::StringMap<int>> m_NodeIn;  // per part: a caller's node by its name
		llvm::StringMap<int> m_Defined;              // a shared definition's node (or c_NeverUnwinds) by its name
		std::vector<bool> m_Unwinds;
	};

	/// The part `module` puts into the program's call graph.
	UnwindPart UnwindPartOf(const llvm::Module& module);
	/// Marks nounwind each function of `module` (part `part` of `graph`) that cannot unwind, and turns every invoke of one
	/// into a call, its exception path dropped; what it asked is kept in `kept`.
	void ApplyNoUnwind(llvm::Module& module, size_t part, const UnwindGraph& graph, UnwindPart& kept);
	/// Whether the answers `kept` took (part `part`) are what `graph` gives now.
	bool SameAnswers(size_t part, const UnwindPart& kept, const UnwindGraph& graph);

	/// Marks nounwind every function of the program that cannot unwind, in each module that defines or declares it, and
	/// turns every invoke of one into a call, its exception path dropped (doc 04, Exceptions). LLVM's function-attrs infers
	/// the same within one module once it optimizes; this sees the whole program, where a split one's other partitions are
	/// only declarations to each, and runs at -O0 too.
	void InferNoUnwind(const std::vector<llvm::Module*>& modules, WorkerPool& pool);
} // namespace C4Compiler
