#include "Backend/CodeGen/ProgramEmitter.h"
#include "Backend/CodeGen/DebugInfo.h"
#include "Backend/CodeGen/GcRoots.h"
#include "Backend/CodeGen/ImportEmitter.h"
#include "Backend/CodeGen/TargetAttributes.h"
#include "Backend/CodeGen/UnwindInference.h"
#include "Backend/CodeGen/VectorAlignment.h"
#include "Backend/CodeGen/VtableEmitter.h"
#include "Backend/HIR/GlobalInit.h"
#include "Model/ProgramModel.h"
#include "Parser/Nodes/Statements/AsmStatementNode.h"
#include "Script/ScriptModulePasses.h"
#include <cstdio>

namespace Scripting
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

	bool ProgramEmitter::Emit(ProgramModel& model, LoweredProgram& program, const EmitSettings& settings, WorkerPool& pool, std::string* error)
	{
		setvbuf(stderr, nullptr, _IONBF, 0); // unbuffered so EH debug markers survive a crash
		auto step = std::chrono::steady_clock::now();
		m_Startup.ForScript(settings.script);
		m_Reflection.ForScript(settings.script);
		m_Partitions.Prepare(model, program, settings.modules, pool);
		EmitFileAssembly(model);
		if (settings.script)
			m_Partitions.Partition(0).StartThreadLocalsOnFirstUse([script = settings.script, &model](const SymbolInfo& variable) {
				return !script->IsHostStorage(variable) && StartsWithCode(model, variable);
			});
		DeclareWrittenFunctions(model);
		m_Partitions.Emit(model, Placements(program), pool);
		EmitSharedVtables(program);
		if (!EmitEntry(model, settings, error)) return false;
		Timed("emit", step);
		if (!m_Startup.EmitStarts(program.units, model, error)) return false;
		MaterializeGlobals(model);
		Timed("starts", step);
		m_Reflection.FinishThunks(program, pool);
		if (!m_Reflection.Emit(model, program, settings.reflectionTag, m_Errors, pool)) return false;
		Timed("reflection", step);
		m_Partitions.DefineCensusCounters(pool); // what the startup code and the harness allocate
		if (!EverythingNamedLowered(program, error)) return false;
		if (m_Partitions.Split()) EmitImports(m_Partitions, model, program, settings.onlyAlwaysInline, pool);
		Timed("imports", step);
		InferNoUnwind(m_Partitions.Modules(), pool);
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
			if (IsPlaced(unit.kind)) placed.push_back({ unit.tree.get(), m_Partitions.Of(*unit.placedBy) });
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
	// has none: code uses its value, and reflection lists no nameless variable.
	void ProgramEmitter::MaterializeGlobals(const ProgramModel& model)
	{
		model.Symbols()->ForEachGlobalVariable([&](const SymbolInfo& info, const Scope& declaredIn) {
			if (info.kind != SymbolKind::Variable || !info.type || info.mangledName.empty()) return;
			ModuleContext& home = m_Partitions.Partition(m_Partitions.Of(info));
			llvm::GlobalVariable* storage = home.GlobalAddress(info);
			if (DebugInfo* debug = home.Debug()) debug->DescribeGlobal(*storage, info, declaredIn);
		});
	}

	bool ProgramEmitter::EverythingNamedLowered(const LoweredProgram& program, std::string* error)
	{
		const std::vector<std::string> missing = m_Partitions.NamedButNotLowered(program);
		if (missing.empty()) return true;
		std::string names;
		for (const std::string& name : missing) names += (names.empty() ? "" : ", ") + name;
		if (error) *error = "internal: codegen names what lowering did not lower: " + names;
		return false;
	}

	// Every body is emitted, so every static local and every global exists for the GC roots.
	void ProgramEmitter::FinishModules(const ProgramModel& model, const EmitSettings& settings, WorkerPool& pool)
	{
		m_Partitions.DeclareOthersStaticStorage();
		EmitGcRoots(m_Partitions.Partition(0), model, settings.script ? settings.script->hostSymbols : nullptr);
		if (settings.script) ApplyScriptLowering(*settings.script); // after the roots: it removes host thread_locals the module's caches point at
		pool.Run(m_Partitions.Count(), [&](size_t i) {
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
} // namespace Scripting
