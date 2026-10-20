#include "CompilerInstance.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Model/Builder/ModelBuilder.h" // ModelBuilder fills the program model
#include "Core/StringFormat.h"
#include <filesystem>
#include <iostream>
#include <cstdio>
#include <sstream>
#include <algorithm>
#include <thread>
#include <mutex>
#include <atomic>
#include <unordered_set>
#include <vector>
#include <condition_variable>
#include <iomanip>

namespace fs = std::filesystem;

namespace Scripting
{

	CompilerInstance::CompilerInstance()
		: m_MaxThreads(std::max(1u, static_cast<uint32>(std::thread::hardware_concurrency())))
	{
	}

	CompilerInstance::~CompilerInstance()
	{
	}

	Ref<CompilationUnit> CompilerInstance::GetCompilationUnit(uint32 sourceId)
	{
		if (!m_SourceManager.HasInfo(sourceId)) return nullptr;

		auto it = m_CompilationUnits.find(sourceId);
		if (it != m_CompilationUnits.end()) return it->second;

		Ref<CompilationUnit> compilationUnit = CreateRef<CompilationUnit>(sourceId, m_SourceManager, this);
		it = m_CompilationUnits.emplace(sourceId, compilationUnit).first;
		return it->second;
	}

	Core::Vector<uint32> CompilerInstance::VisibleFrom(uint32 sourceId) const
	{
		Core::Vector<uint32> visible;
		auto it = m_CompilationUnits.find(sourceId);
		if (it == m_CompilationUnits.end()) return visible;
		visible.Push(sourceId);
		for (uint32 included : it->second->GetIncludes())
			visible.Push(included);
		return visible;
	}

	// ------------------------------------------------------------------
	// Parse scheduler.
	//
	// CompileAll drives a worklist of compilation units across up to m_MaxThreads parser
	// threads. Each unit parses its own file on its own thread and asks the instance (via
	// CompilerMessageQueue) to resolve includes; the instance parks the requesting unit until
	// the included file finishes, then requeues it. The loop in CompileAll is just that
	// protocol: fill free slots, wait for one event, act on it. Each step has its own function.
	// ------------------------------------------------------------------

	void CompilerInstance::CompileAll()
	{
		uint32 totalUnits = EnqueueAllSources();
		if (!totalUnits) return;

		while (m_FilesCompleted < totalUnits)
		{
			DispatchAvailableUnits();

			// Nothing queued and nothing running means no unit can make progress - stop rather than spin.
			if (NoProgressPossible() && !ReleaseDeadlockedUnits())
				break;

			CompilerMessage msg = m_MessageQueue.WaitPopRequest();
			if (msg.type == MessageType::FinishedParsing)
				HandleUnitFinished(msg.sourceId);
			else if (msg.type == MessageType::RequestFileInclude)
				HandleIncludeRequest(msg);
		}
	}

	uint32 CompilerInstance::EnqueueAllSources()
	{
		for (uint32 id = 1; id <= m_SourceManager.Count(); ++id)
		{
			if (!m_SourceManager.HasInfo(id)) continue;

			GetCompilationUnit(id);
			m_ParseQueue.Push(id);
			m_QueueIds.insert(id); // Track which source IDs are in the parse queue for fast lookup
		}

		m_FilesCompleted = 0;
		return m_ParseQueue.Count();
	}

	void CompilerInstance::DispatchAvailableUnits()
	{
		while (!m_ParseQueue.Empty() && m_ActiveThreads < m_MaxThreads)
		{
			uint32 sourceId = m_ParseQueue.Pop();
			m_QueueIds.erase(sourceId);

			Ref<CompilationUnit> unit = GetCompilationUnit(sourceId);
			if (!unit) continue;

			m_ActiveThreads++;
			if (unit->IsWorking())
				m_MessageQueue.FlushPendingResponse(sourceId); // a response may already be pending - wake the thread
			else
				unit->StartParse();                            // first dispatch: start the parse thread
		}
	}

	bool CompilerInstance::NoProgressPossible() const
	{
		return m_ParseQueue.Empty() && m_ActiveThreads == 0;
	}

	void CompilerInstance::HandleUnitFinished(uint32 sourceId)
	{
		m_ActiveThreads--;
		m_FilesCompleted++;

		Ref<CompilationUnit> unit = GetCompilationUnit(sourceId);
		unit->Join();
		m_CompletedUnits.insert(sourceId);

		WakeBlockedDependents(sourceId, unit);
	}

	void CompilerInstance::WakeBlockedDependents(uint32 sourceId, const Ref<CompilationUnit>& finishedUnit)
	{
		auto it = m_BlockedUnits.find(sourceId);
		if (it == m_BlockedUnits.end()) return;

		for (uint32 i = 0; i < it->second.Count(); ++i)
			ResumeBlockedUnit(it->second[i], finishedUnit);
		m_BlockedUnits.erase(it);
	}

	void CompilerInstance::ResumeBlockedUnit(uint32 blockedId, const Ref<CompilationUnit>& dependency)
	{
		m_MessageQueue.PushResponse(blockedId, ResponseType::Success, Core::String());

		// Requeue the unit so it resumes parsing.
		if (m_QueueIds.count(blockedId) || m_CompletedUnits.count(blockedId))
			return;

		m_ParseQueue.Push(blockedId);
		m_QueueIds.insert(blockedId);

		// Fold the dependency's includes into this unit so its include set is the full transitive closure.
		Ref<CompilationUnit> unit = GetCompilationUnit(blockedId);
		for (auto& includedId : dependency->m_IncludesIDS)
			unit->m_IncludesIDS.insert(includedId);
	}

	void CompilerInstance::HandleIncludeRequest(const CompilerMessage& msg)
	{
		uint32 requesterId = msg.sourceId;
		const Core::String includePath = msg.data;

		const IncludeTarget target = m_SourceManager.ResolveInclude(includePath.c_str(), requesterId);
		const uint32 targetId = target.id;

		// Include not found, or found where the includer's project may not reach - the parser reports it.
		if (targetId == 0)
		{
			RespondToRequester(requesterId, ResponseType::Error,
				target.error.empty() ? "include not found: '" + includePath + "'" : Core::String(target.error.c_str()));
			return;
		}

		// Direct self-include: a file including itself is always a cycle. This must be caught here
		// (not by the transitive check below) because on the FIRST self-include the edge isn't in the
		// include set yet - it is only recorded after this check, in BlockRequesterOnDependency.
		if (targetId == requesterId)
		{
			RespondToRequester(requesterId, ResponseType::Error,
				Core::StringFormat::Format("Cyclic include detected: '{0}' includes itself", includePath));
			return;
		}

		// Dependency already parsed - resume immediately. The edge must STILL be recorded (and the
		// target's closure folded in, mirroring ResumeBlockedUnit): m_IncludesIDS is the include
		// closure that VisibleFrom() and the program model rely on, and skipping it here made the set
		// depend on parse-thread timing - an include whose target happened to finish first was silently
		// missing from the closure.
		if (m_CompletedUnits.count(targetId))
		{
			auto& requester = *GetCompilationUnit(requesterId);
			requester.m_IncludesIDS.insert(targetId);
			for (auto& includedId : GetCompilationUnit(targetId)->m_IncludesIDS)
				requester.m_IncludesIDS.insert(includedId);
			RespondToRequester(requesterId, ResponseType::Success, Core::String());
			return;
		}

		// Cycle detection: the requested file must not (transitively) include this one.
		if (IncludesTransitively(targetId, requesterId))
		{
			Core::String cycleMsg = Core::StringFormat::Format(
				"Cyclic include detected: '{0}' includes this file (directly or through the files it includes)",
				includePath);
			RespondToRequester(requesterId, ResponseType::Error, cycleMsg);
			return;
		}

		BlockRequesterOnDependency(requesterId, targetId);
	}

	// Through the include edges recorded so far, a parked unit's edge to the file it waits on included: a cycle of any
	// length (a -> b -> c -> a) is found when its last edge is asked for.
	bool CompilerInstance::IncludesTransitively(uint32 from, uint32 target)
	{
		std::unordered_set<uint32> seen;
		std::vector<uint32> pending{ from };
		while (!pending.empty())
		{
			const uint32 id = pending.back();
			pending.pop_back();
			if (id == target) return true;
			if (!seen.insert(id).second) continue;
			for (uint32 next : GetCompilationUnit(id)->GetIncludes()) pending.push_back(next);
		}
		return false;
	}

	// Units still waiting on each other's includes can never resume: each is told so and finishes, rather than leave
	// its parse thread blocked.
	bool CompilerInstance::ReleaseDeadlockedUnits()
	{
		if (m_BlockedUnits.empty()) return false;
		for (auto& [target, waiting] : m_BlockedUnits)
			for (uint32 i = 0; i < waiting.Count(); ++i)
			{
				m_ActiveThreads++;
				RespondToRequester(waiting[i], ResponseType::Error, "Cyclic include detected: the files this one includes wait on each other");
			}
		m_BlockedUnits.clear();
		return true;
	}

	void CompilerInstance::BlockRequesterOnDependency(uint32 requesterId, uint32 targetId)
	{
		// Record the dependency edge (used for cycle detection on later includes).
		GetCompilationUnit(requesterId)->m_IncludesIDS.insert(targetId);

		// Park the requester until targetId finishes; its thread slot is freed in the meantime.
		m_BlockedUnits[targetId].Push(requesterId);
		m_ActiveThreads--;

		PromoteDependencyInQueue(targetId);
	}

	void CompilerInstance::PromoteDependencyInQueue(uint32 targetId)
	{
		// The worklist is LIFO (Pop takes from the back), so swapping with the back brings the
		// dependency to the head of dispatch order. If it is not queued, it is already parsing.
		if (!m_QueueIds.count(targetId)) return;

		uint32 loc = m_ParseQueue.Find(targetId);
		std::swap(m_ParseQueue[loc], m_ParseQueue.Back());
	}

	void CompilerInstance::RespondToRequester(uint32 requesterId, ResponseType type, const Core::String& message)
	{
		m_MessageQueue.PushResponse(requesterId, type, message);
		m_MessageQueue.FlushPendingResponse(requesterId); // the requester's thread is blocked waiting for this
	}

	// ------------------------------------------------------------------
	// Program model (docs/compiler/02-program-model.md). One ModelBuilder over every reachable unit's OWN
	// top-level declarations fills the single shared model. Each declaration is owned by exactly one
	// unit (the file where it is written), so the union of all units' original children contains each
	// definition once - no merge, no per-unit re-analysis, and include-based visibility (doc 09) is
	// enforced at query time from the recorded defining unit.
	// ------------------------------------------------------------------

	bool CompilerInstance::AnalyzeProgram()
	{
		// LSP navigation walks a document's merged tree (a reference may land in an included
		// header), so splice every unit's include closure into its root AST first. This is pure
		// AST prep - it produces no semantic state and the program model does not depend on it.
		for (auto& [id, unit] : m_CompilationUnits)
			if (unit)
				unit->MergeIncludes();

		DetectUserMain();
		return BuildProgramModel();
	}

	bool CompilerInstance::BuildProgramModel()
	{
		m_ProgramModelErrorTracker.Clear();

		ModelBuilder builder(&m_ProgramModelErrorTracker);
		builder.Build(*this, m_ProgramModel);

		for (const auto& err : m_ProgramModelErrorTracker.GetErrors())
			if (err.severity != ErrorSeverity::Warning)
				return false;
		return true;
	}

	// A global free function named `main` at file scope is the entry point even when it is static
	// (file-local): the generated wrapper calls it from within the same module. Only lowercase
	// `main` counts - a capital-M `Main` is never the entry point.
	static bool HasGlobalMain(const AstNode* node)
	{
		if (!node) return false;
		if (auto fn = dynamic_cast<const FunctionDefinitionNode*>(node))
		{
			if (!fn->IsDefinition() || fn->IsTemplated()) return false;
			const auto& name = fn->GetName();
			return name && name->SegmentCount() == 1 &&
				name->GetSegments()[0].nameToken.GetText() == "main";
		}
		if (auto ns = dynamic_cast<const NamespaceNode*>(node))
		{
			for (const auto& child : ns->Children())
				if (HasGlobalMain(child.Get())) return true;
		}
		return false;
	}

	void CompilerInstance::DetectUserMain()
	{
		for (uint32 id = 1; id <= m_SourceManager.Count(); ++id)
		{
			if (!m_SourceManager.HasInfo(id)) continue;
			auto unit = GetCompilationUnit(id);
			if (!unit || !IsEntryFile(m_SourceManager.GetInfo(id).displayName))
				continue;

			const auto& children = unit->GetOriginalChildren();
			for (uint32 i = 0; i < children.Count(); ++i)
				if (HasGlobalMain(children[i].Get()))
					m_HasUserMain = true;
		}
	}

	Core::Vector<CompilationError> CompilerInstance::GetAllErrors() const
	{
		// A projection of the single diagnostic surface (parse + program-model errors), filtered to
		// hard errors only. Deriving it from GetAllDiagnostics keeps the two accessors from diverging -
		// there is one source of truth, not two parallel collections.
		Core::Vector<CompilationError> allErrors;
		for (const auto& err : GetAllDiagnostics())
			if (err.severity != ErrorSeverity::Warning)
				allErrors.Push(err);
		return allErrors;
	}

	void CompilerInstance::AddMacroDiagnostics(const std::vector<MacroPass::FileResult>& results)
	{
		for (const MacroPass::FileResult& result : results)
		{
			if (result.diagnostics.empty()) continue;
			uint32 sourceId = 0;
			for (uint32 id = 1; id <= m_SourceManager.Count() && sourceId == 0; ++id)
				if (m_SourceManager.HasInfo(id) && m_SourceManager.GetInfo(id).absPath == result.absPath) sourceId = id;
			if (sourceId == 0) continue; // a file in a scanned folder that is not compiled
			for (const MacroDiagnostic& diagnostic : result.diagnostics)
			{
				SourceLocation location;
				location.sourceId = sourceId;
				location.line = diagnostic.line;
				location.column = diagnostic.column;
				location.byteOffset = m_SourceManager.GetInfo(sourceId).source->LineToByteOffset(diagnostic.line) + diagnostic.column - 1;
				m_MacroErrorTracker.AddError(location, diagnostic.message, diagnostic.severity);
			}
		}
	}

	Core::Vector<CompilationError> CompilerInstance::GetAllDiagnostics() const
	{
		Core::Vector<CompilationError> allDiagnostics;
		for (const auto& err : m_MacroErrorTracker.GetErrors())
			allDiagnostics.Push(err);
		for (const auto& [id, unit] : m_CompilationUnits)
		{
			const auto& errors = unit->GetErrorTracker().GetErrors();
			for (uint32 i = 0; i < errors.Count(); ++i)
				allDiagnostics.Push(errors[i]);
		}
		// Semantic diagnostics come from the single program-wide pass, not per unit. The two sets
		// are disjoint (per-unit trackers hold only parse-phase errors), so this is a plain union -
		// one diagnostic surface for both the CLI driver and the LSP.
		if (m_ProgramModel.IsValid())
			for (const auto& err : m_ProgramModelErrorTracker.GetErrors())
				allDiagnostics.Push(err);
		return allDiagnostics;
	}

	const char* CompilerInstance::SeverityText(ErrorSeverity severity)
	{
		switch (severity) {
		case ErrorSeverity::Error: return "error";
		case ErrorSeverity::Warning: return "warning";
		default: return "info";
		}
	}

	void CompilerInstance::PrintAllErrors() const
	{
		for (const auto& err : GetAllDiagnostics())
		{
			std::string file = "(unknown)";
			if (err.location.sourceId != 0 && m_SourceManager.HasInfo(err.location.sourceId))
				file = m_SourceManager.GetInfo(err.location.sourceId).displayName;
			std::printf("%s(%u,%u): %s: %s\n", file.c_str(), err.location.line, err.location.column,
				SeverityText(err.severity), err.message.c_str());
		}
	}

	bool CompilerInstance::IsEntryFile(const std::string& displayName)
	{
		size_t dotPos = displayName.rfind('.');
		return dotPos != std::string::npos && displayName.substr(dotPos) == ".cf";
	}

}
