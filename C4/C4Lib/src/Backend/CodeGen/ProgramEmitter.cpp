#include "Backend/CodeGen/ProgramEmitter.h"
#include "Backend/CodeGen/DebugInfo.h"
#include "Backend/CodeGen/GcRoots.h"
#include "Backend/CodeGen/ImportEmitter.h"
#include "Backend/CodeGen/TargetAttributes.h"
#include "Backend/CodeGen/UnwindInference.h"
#include "Backend/CodeGen/VectorAlignment.h"
#include <algorithm>
#include <unordered_set>
#include "Backend/CodeGen/VtableEmitter.h"
#include "Backend/Reflection/ReflectionScope.h"
#include "Backend/HIR/GlobalInit.h"
#include "Model/ProgramModel.h"
#include "Parser/Nodes/Statements/AsmStatementNode.h"
#include "Script/ScriptModulePasses.h"
#include <cstdio>

namespace C4Compiler
{
	namespace
	{
		bool IsWritten(const FunctionRecord& record)
		{
			return record.templateOrigin == TemplateOrigin::None && record.synthesized == LifecycleOp::None;
		}

		bool IsPlaced(UnitKind kind)
		{
			return kind == UnitKind::Function || kind == UnitKind::InvokeThunk || kind == UnitKind::TypeThunk;
		}
	}

	bool ProgramEmitter::Emit(ProgramModel& model, LoweredProgram& program, const EmitSettings& settings, WorkerPool& pool, std::string* error,
		PartialEmission* partial)
	{
		setvbuf(stderr, nullptr, _IONBF, 0); // unbuffered so EH debug markers survive a crash
		auto step = std::chrono::steady_clock::now();
		m_Partial = partial;
		if (partial)
		{
			partial->stale.assign(m_Partitions.Count(), false);
			partial->made.assign(m_Partitions.Count(), {});
			m_Partitions.EmitOnly(&partial->emitted);
		}
		const bool whole = m_Partitions.Emits(0); // partition 0, the program's own, is emitted only with every other
		m_Startup.ForScript(settings.script);
		m_Reflection.ForScript(settings.script);
		m_Partitions.Prepare(model, program, settings.modules, pool);
		if (whole) EmitFileAssembly(model);
		if (settings.script)
			m_Partitions.Partition(0).StartThreadLocalsOnFirstUse([script = settings.script, &model](const SymbolInfo& variable) {
				return !script->IsHostStorage(variable) && StartsWithCode(model, variable);
			});
		if (whole) DeclareWrittenFunctions(model);
		m_Partitions.Emit(model, Placements(program), pool);
		if (whole) EmitSharedVtables(program);
		if (whole && !EmitEntry(model, settings, error)) return false;
		Timed("emit", step);
		if (!m_Startup.EmitStarts(program.units, model, error)) return false;
		MaterializeGlobals(model, settings.everyGlobal);
		Timed("starts", step);
		m_Reflection.FinishThunks(program, pool);
		if (whole && !m_Reflection.Emit(model, program, settings.reflectionTag, m_Errors, pool)) return false;
		Timed("reflection", step);
		m_Partitions.DefineCensusCounters(pool); // what the startup code and the harness allocate
		if (!EverythingNamedLowered(program, settings.script, error)) return false;
		if (m_Partitions.Split() && settings.copies != PartitionCopies::None)
			EmitImports(m_Partitions, model, program, settings.copies == PartitionCopies::Inline, pool, partial);
		MaterializeNamedGlobals();
		Timed("imports", step);
		MarkNoUnwind(pool);
		Timed("nounwind", step);
		FinishModules(model, settings, pool);
		Timed("finish", step);
		if (settings.verifyLater) return true;
		const std::optional<std::string> invalid = m_Partitions.FirstInvalid();
		if (invalid && error) *error = *invalid;
		return !invalid;
	}

	void ProgramEmitter::EmitFileAssembly(const ProgramModel& model)
	{
		// Intel syntax unless the block says att_syntax; each block switches back so the next one starts in AT&T,
		// the module assembler's own default.
		for (const AsmStatementNode* block : model.FileAssembly())
		{
			const bool att = !block->GetOptions().Empty();
			std::string text = att ? "" : ".intel_syntax noprefix\n";
			for (const Core::String& line : block->GetLines()) text += std::string(line.c_str()) + "\n";
			if (!att) text += ".att_syntax prefix\n";
			m_Partitions.Partition(0).module().appendModuleInlineAsm(text);
		}
	}

	void ProgramEmitter::DeclareWrittenFunctions(const ProgramModel& model)
	{
		Core::Vector<const FunctionRecord*> records;
		model.AllFunctionRecords(records);
		for (const FunctionRecord* record : records)
			if (IsWritten(*record)) m_Partitions.Partition(0).GetOrCreateFunction(*record);
	}

	// A function goes by its own record, an own invoke thunk by its target's, a shared thunk by its name.
	std::vector<Placed> ProgramEmitter::Placements(const LoweredProgram& program) const
	{
		std::vector<Placed> placed;
		for (const LoweredUnit& unit : program.units)
			if (IsPlaced(unit.kind))
				if (const unsigned partition = m_Partitions.Of(*unit.placedBy); m_Partitions.Emits(partition)) placed.push_back({ unit.tree.get(), partition });
		return placed;
	}

	void ProgramEmitter::EmitSharedVtables(const LoweredProgram& program)
	{
		for (const ObjectType* type : program.sharedVtables) m_Partitions.Partition(0).vtables().VptrSites(type);
	}

	// A -test build of a program with tests runs them as its main; otherwise `@main` calls the program's entry.
	bool ProgramEmitter::EmitEntry(ProgramModel& model, const EmitSettings& settings, std::string* error)
	{
		const bool harness = settings.tests && TestHarnessEmitter(m_Partitions.Partition(0), model.Tests(), *settings.tests).Emit();
		if (harness || settings.entryName.empty()) return true;
		return m_Startup.EmitEntryPoint(model, settings.entryName, error);
	}

	// Each in the partition that defines it, described there. A constant with no symbol (an unscoped enum's member)
	// has none: code uses its value, and reflection lists no nameless variable. One out of reflection's scope (D-89) waits
	// for MaterializeNamedGlobals: it has storage only where code names it (or the collector's roots need it), unless
	// `every` (a script host, whose table lists every global for its scripts).
	void ProgramEmitter::MaterializeGlobals(const ProgramModel& model, bool every)
	{
		const ReflectionScope scope(model);
		model.Symbols()->ForEachGlobalVariable([&](const SymbolInfo& info, const Scope& declaredIn) {
			if (info.kind != SymbolKind::Variable || !info.type || info.mangledName.empty()) return;
			if (every || scope.Has(info)) Materialize(info, declaredIn);
			else m_OutOfScope.push_back({ &info, &declaredIn });
		});
	}

	// A global out of reflection's scope that a partition's code (or a copy) names: its home partition defines it, though
	// only another partition may have named it.
	void ProgramEmitter::MaterializeNamedGlobals()
	{
		if (m_Partial) KeepNamedGlobals();
		for (const auto& [info, declaredIn] : m_OutOfScope)
			if (NamedAnywhere(*info)) Materialize(*info, *declaredIn);
		if (m_Partial) FindStaleNamedGlobals();
		m_OutOfScope.clear();
	}

	// A partition not emitted again names what it named the build before.
	bool ProgramEmitter::NamedAnywhere(const SymbolInfo& global)
	{
		for (unsigned i = 0; i < m_Partitions.Count(); ++i)
		{
			const bool named = m_Partitions.Emits(i) ? m_Partitions.Partition(i).module().getNamedGlobal(global.mangledName.c_str()) != nullptr
				: std::find(Before(i).namedGlobals.begin(), Before(i).namedGlobals.end(), &global) != Before(i).namedGlobals.end();
			if (named) return true;
		}
		return false;
	}

	// The globals out of reflection's scope each emitted partition's code names, kept in its facts.
	void ProgramEmitter::KeepNamedGlobals()
	{
		for (unsigned i = 0; i < m_Partitions.Count(); ++i)
			if (m_Partitions.Emits(i)) m_Partial->made[i].namedGlobals = GlobalsNamedIn(i);
	}

	std::vector<const SymbolInfo*> ProgramEmitter::GlobalsNamedIn(unsigned partition)
	{
		std::vector<const SymbolInfo*> named;
		for (const auto& [info, declaredIn] : m_OutOfScope)
			if (m_Partitions.Partition(partition).module().getNamedGlobal(info->mangledName.c_str())) named.push_back(info);
		return named;
	}

	// A global's storage made in a partition not emitted again is stale when the global is named anywhere now and was not
	// before, or the other way round; storage that names more such globals (a pointer in its image) makes the order of
	// the decisions matter, so then every partition is emitted again.
	void ProgramEmitter::FindStaleNamedGlobals()
	{
		for (unsigned i = 0; i < m_Partitions.Count(); ++i)
		{
			if (m_Partitions.Emits(i)) m_Partial->made[i].namedMore = GlobalsNamedIn(i).size() > m_Partial->made[i].namedGlobals.size();
			const bool namedMore = m_Partitions.Emits(i) ? m_Partial->made[i].namedMore : Before(i).namedMore;
			if (namedMore) m_Partial->stale[0] = true;
		}
		std::unordered_set<const SymbolInfo*> before;
		for (unsigned i = 0; i < m_Partitions.Count(); ++i)
			before.insert(Before(i).namedGlobals.begin(), Before(i).namedGlobals.end());
		for (const auto& [info, declaredIn] : m_OutOfScope)
			if (const unsigned home = m_Partitions.Of(*info); !m_Partitions.Emits(home) && (before.count(info) != 0) != NamedAnywhere(*info))
				m_Partial->stale[home] = true;
	}

	const PartitionFacts& ProgramEmitter::Before(unsigned partition) const
	{
		static const PartitionFacts none;
		return m_Partial && m_Partial->facts && partition < m_Partial->facts->size() ? (*m_Partial->facts)[partition] : none;
	}

	void ProgramEmitter::Materialize(const SymbolInfo& info, const Scope& declaredIn)
	{
		if (!m_Partitions.Emits(m_Partitions.Of(info))) return;
		ModuleContext& home = m_Partitions.Partition(m_Partitions.Of(info));
		llvm::GlobalVariable* storage = home.GlobalAddress(info);
		if (DebugInfo* debug = home.Debug()) debug->DescribeGlobal(*storage, info, declaredIn);
	}

	bool ProgramEmitter::EverythingNamedLowered(const LoweredProgram& program, const ScriptLowering* script, std::string* error)
	{
		const std::vector<std::string> missing = m_Partitions.NamedButNotLowered(program, script);
		if (missing.empty()) return true;
		std::string names;
		for (const std::string& name : missing) names += (names.empty() ? "" : ", ") + name;
		if (error) *error = "internal: codegen names what lowering did not lower: " + names;
		return false;
	}

	// Every partition's call graph at once; with a partial emission, a partition not emitted again gives the build before's
	// part and is stale when the answers it took are not the graph's now.
	void ProgramEmitter::MarkNoUnwind(WorkerPool& pool)
	{
		if (!m_Partial)
		{
			InferNoUnwind(m_Partitions.Modules(), pool);
			return;
		}
		const unsigned count = m_Partitions.Count();
		std::vector<UnwindPart> made(count);
		pool.Run(count, [&](size_t i) {
			if (m_Partitions.Emits(static_cast<unsigned>(i))) made[i] = UnwindPartOf(m_Partitions.Partition(static_cast<unsigned>(i)).module());
		});
		std::vector<const UnwindPart*> parts;
		for (unsigned i = 0; i < count; ++i) parts.push_back(m_Partitions.Emits(i) ? &made[i] : &Before(i).unwind);
		const UnwindGraph graph(parts);
		pool.Run(count, [&](size_t i) {
			const unsigned partition = static_cast<unsigned>(i);
			if (m_Partitions.Emits(partition)) ApplyNoUnwind(m_Partitions.Partition(partition).module(), i, graph, made[i]);
			else if (!SameAnswers(i, Before(partition).unwind, graph)) m_Partial->stale[i] = true;
		});
		for (unsigned i = 0; i < count; ++i)
			if (m_Partitions.Emits(i)) m_Partial->made[i].unwind = std::move(made[i]);
	}

	// A partition's static locals, which partition 0 declares for the GC roots: when an emitted one's are not what they
	// were, partition 0 - and so every partition - is emitted again.
	void ProgramEmitter::KeepStatics()
	{
		for (unsigned i = 0; i < m_Partitions.Count(); ++i)
		{
			if (!m_Partitions.Emits(i)) continue;
			std::vector<StaticStorageFact>& statics = m_Partial->made[i].statics;
			for (const ModuleContext::StaticStorage& s : m_Partitions.Partition(i).StaticStorages())
				statics.push_back({ s.storage->getName().str(), s.type, s.storage->isThreadLocal() });
			if (!m_Partitions.Emits(0) && statics != Before(i).statics) m_Partial->stale[0] = true;
		}
	}

	// Every body is emitted, so every static local and every global exists for the GC roots.
	void ProgramEmitter::FinishModules(const ProgramModel& model, const EmitSettings& settings, WorkerPool& pool)
	{
		if (m_Partial) KeepStatics();
		if (m_Partitions.Emits(0))
		{
			m_Partitions.DeclareOthersStaticStorage();
			EmitGcRoots(m_Partitions.Partition(0), model, settings.script ? settings.script->hostSymbols : nullptr);
		}
		if (settings.script) ApplyScriptLowering(*settings.script); // after the roots: it removes host thread_locals the module's caches point at
		pool.Run(m_Partitions.Count(), [&](size_t i) {
			if (!m_Partitions.Emits(static_cast<unsigned>(i))) return;
			ModuleContext& partition = m_Partitions.Partition(static_cast<unsigned>(i));
			AlignVectorAccesses(partition.module());
			ApplyTarget(partition.module(), model.Target());
			ApplyFunctionTargets(partition, model);
			// A script anchors its tables after its own passes (ScriptCompiler: KeepImageOffsetsInImage first).
			if (!settings.script) partition.Target().AnchorTables(partition.module());
			if (DebugInfo* debug = partition.Debug()) debug->Finish();
		});
	}

	void ProgramEmitter::ApplyScriptLowering(const ScriptLowering& script)
	{
		llvm::Module& module = m_Partitions.Partition(0).module();
		UseHostThreadStorage(module, *script.hostSymbols);
		UseHostDefinitions(module, *script.hostSymbols);
		UseScriptRuntime(module);
	}

	void ProgramEmitter::Timed(const char* step, std::chrono::steady_clock::time_point& since)
	{
		const auto now = std::chrono::steady_clock::now();
		m_StepSeconds.push_back({ step, std::chrono::duration<double>(now - since).count() });
		since = now;
	}
} // namespace C4Compiler
