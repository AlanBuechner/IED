#include "CompilationUnit.h"
#include "Parser/ParserState.h"
#include "Compiler/CompilerInstance.h"
#include "Compiler/DeclarationFingerprint.h"
#include "Core/StringFormat.h"

namespace C4Compiler
{
	CompilationUnit::CompilationUnit(uint32 sourceId, SourceManager& sourceManager, CompilerInstance* compilerInstance) :
		m_SourceId(sourceId), m_SourceManager(sourceManager), m_CompilerInstance(compilerInstance),
		m_MessageQueue(compilerInstance ? &compilerInstance->GetMessageQueue() : nullptr),
		m_Lexer(*sourceManager.GetInfo(sourceId).source.Get(), sourceId, sourceManager.GetInfo(sourceId).macros.Get())
	{
		SourceInfo& info = sourceManager.GetInfo(sourceId);
		m_Source = info.source;
		m_Macros = info.macros;
		for (uint32 binding : info.bindings) m_Bindings.push_back(sourceManager.GetInfo(binding).absPath);
		if (!info.source)
		{
			m_ErrorTracker.AddError(
				SourceLocation{},
				Core::StringFormat::Format("CompilationUnit: source ID {0} not found in SourceManager.", sourceId),
				ErrorSeverity::Fatal);
		}
	}

	void CompilationUnit::StartReplay(const PriorParse& prior)
	{
		m_ProcessingThread = std::thread([this, &prior]() {
			m_RootASTNode = prior.root;
			m_OriginalChildren = prior.originalChildren;
			m_ErrorTracker.GetErrorsMutable() = prior.errors;
			for (const IncludeAsked& include : prior.includes)
				AskAgain(include);
			m_MessageQueue->SendMessage(m_SourceId, MessageType::FinishedParsing, "");
		});
	}

	uint64 CompilationUnit::Fingerprint() const
	{
		return m_Source ? DeclarationFingerprint(*m_Source, m_SourceId, m_Macros.Get(), m_OriginalChildren) : 0;
	}

	void CompilationUnit::AskAgain(const IncludeAsked& include)
	{
		const CompilerResponse response = m_MessageQueue->SendMessageAndAwaitResponse(m_SourceId, MessageType::RequestFileInclude, include.path);
		IncludeAsked asked = include;
		asked.error = response.type == ResponseType::Error ? response.data : Core::String();
		// The parse reported it with AddError, which drops an error at the place of the error just before it.
		if (!asked.error.empty() && !asked.afterErrorHere)
			m_ErrorTracker.GetErrorsMutable().Push({ asked.at, ErrorSeverity::Error, asked.error });
		m_IncludesAsked.Push(std::move(asked));
	}

	bool CompilationUnit::AnswersAnInclude(const CompilationError& error) const
	{
		for (const IncludeAsked& include : m_IncludesAsked)
			if (!include.error.empty() && include.at == error.location && include.error == error.message) return true;
		return false;
	}

	PriorParse CompilationUnit::ToPriorParse() const
	{
		PriorParse parse{ m_Source, m_Macros, m_Bindings, m_RootASTNode, m_OriginalChildren, {}, m_IncludesAsked };
		for (const CompilationError& error : m_ErrorTracker.GetErrors())
			if (!AnswersAnInclude(error)) parse.errors.Push(error);
		return parse;
	}

	void CompilationUnit::StartParse()
	{
		m_ProcessingThread = std::thread([this]() {
			ParserState parserState(m_Lexer, m_ErrorTracker, m_MessageQueue);
			parserState.RecordIncludesIn(&m_IncludesAsked);
			for (const LexicalDiagnostic& unpaired : m_Lexer.RecoverBraces())
				m_ErrorTracker.AddError(unpaired.location, unpaired.message, ErrorSeverity::Error);
			IncludeBindings();

			m_RootASTNode = CreateRef<NamespaceNode>();
			m_RootASTNode->SetIsRoot(true);
			m_RootASTNode->ParseBody(parserState);
			ReportLexicalErrors();
			m_Lexer.ReleaseTokens();

			// Save original children before any merge happens
			const auto& parsedChildren = m_RootASTNode->GetMutableChildren();
			m_OriginalChildren.Resize(parsedChildren.Count());
			for (uint32 i = 0; i < parsedChildren.Count(); ++i)
			{
				m_OriginalChildren[i] = parsedChildren[i];
			}

			m_MessageQueue->SendMessage(m_SourceId, MessageType::FinishedParsing, "");
		});
	}

	// The binding units its `extern ... include`s bring in (R8): included as a header is, so the file and its includers see
	// them; asked for as includes are, so a parse taken again asks again.
	void CompilationUnit::IncludeBindings()
	{
		if (!m_MessageQueue) return;
		for (const std::string& binding : m_Bindings)
		{
			const CompilerResponse response = m_MessageQueue->SendMessageAndAwaitResponse(m_SourceId, MessageType::RequestFileInclude, binding.c_str());
			IncludeAsked asked{ binding.c_str(), SourceLocation{} };
			asked.at.sourceId = m_SourceId;
			if (response.type == ResponseType::Error)
			{
				asked.error = response.data;
				m_ErrorTracker.AddError(asked.at, response.data, ErrorSeverity::Error);
			}
			m_IncludesAsked.Push(std::move(asked));
		}
	}

	void CompilationUnit::ReportLexicalErrors()
	{
		// First: a bad literal or an unclosed comment is the cause of the parse errors after it.
		Core::Vector<CompilationError> errors;
		for (const auto& entry : m_Lexer.Diagnostics())
			errors.Push({ entry.second.location, ErrorSeverity::Error, entry.second.message });
		for (const CompilationError& error : m_ErrorTracker.GetErrors())
			errors.Push(error);
		m_ErrorTracker.GetErrorsMutable() = std::move(errors);
	}

	void CompilationUnit::MergeIncludes()
	{
		if (!m_RootASTNode) return;

		// Splice this unit's include closure into its root AST (nodes shared by Ref, not copied).
		// LSP position-walks need the merged tree to resolve a reference that lands in an included
		// header. The program-model build does NOT depend on this - it reads each unit's original
		// children directly - so the merge is purely AST prep for navigation, not semantic analysis.
		std::unordered_set<uint32> mergedIds;
		mergedIds.insert(m_SourceId);
		MergeIncludedASTs(mergedIds);
	}

	const ProgramModel& CompilationUnit::GetProgramModel() const
	{
		// The single program-wide pass is the only model build: every unit shares one ProgramModel
		// (doc 03). Before it has run there is nothing to return, so hand back a never-dereferenced
		// empty model - every caller already guards on IsValid().
		static const ProgramModel kEmpty;
		if (!m_CompilerInstance) return kEmpty;
		return m_CompilerInstance->GetProgramModel();
	}

	ProgramModel& CompilationUnit::GetProgramModelForLowering()
	{
		// Non-const twin for lowering phases (doc 06): the scope chain is mutated by PushScope /
		// RegisterLocal, so a lowerer needs a mutable reference to the one shared model.
		static ProgramModel kEmpty;
		if (!m_CompilerInstance) return kEmpty;
		return m_CompilerInstance->GetProgramModelForLowering();
	}

	void CompilationUnit::MergeIncludedASTs(std::unordered_set<uint32>& mergedIds)
	{
		if (!m_CompilerInstance || !m_RootASTNode) return;

		auto& children = m_RootASTNode->GetMutableChildren();

		// Single forward pass: expand each include in place. Nodes are shared by Ref and the AST is
		// immutable after parsing, so one included unit's nodes can be referenced from every includer.
		// Collecting into a temporary and assigning once keeps this O(total nodes) instead of the old
		// per-include back-to-front Insert loop, which was O(includes * children).
		Core::Vector<Ref<AstNode>> expanded;
		expanded.Reserve(children.Count());
		for (uint32 i = 0; i < children.Count(); ++i)
		{
			if (auto includeNode = children[i].PointerCast<IncludeDeclarationNode>())
			{
				uint32 targetId = m_SourceManager.ResolveIncludePath(includeNode->FilePath(), m_SourceId);
				Ref<CompilationUnit> targetUnit = (targetId != 0) ? m_CompilerInstance->GetCompilationUnit(targetId) : nullptr;
				if (!targetUnit || !targetUnit->GetRootAST())
				{
					expanded.Push(children[i]); // unresolvable include: keep the node for downstream diagnostics
					continue;
				}
				if (!mergedIds.count(targetId))
					CollectFromOriginalAST(targetUnit, mergedIds, expanded);
				// An already-merged include (duplicate) expands to nothing.
				continue;
			}
			expanded.Push(children[i]);
		}
		children = std::move(expanded);
	}

	void CompilationUnit::CollectFromOriginalAST(Ref<CompilationUnit> targetUnit,
		std::unordered_set<uint32>& mergedIds, Core::Vector<Ref<AstNode>>& out)
	{
		uint32 targetId = targetUnit->SourceId();
		if (mergedIds.count(targetId)) return;
		mergedIds.insert(targetId);

		// Use the original unparsed children to avoid collecting from already-merged ASTs
		const auto& children = targetUnit->GetOriginalChildren();
		for (uint32 i = 0; i < children.Count(); ++i)
		{
			if (!children[i]) continue;

			if (auto includeNode = children[i].PointerCast<IncludeDeclarationNode>())
			{
				uint32 nestedTargetId = m_SourceManager.ResolveIncludePath(includeNode->FilePath(), targetId);
				if (nestedTargetId != 0 && !mergedIds.count(nestedTargetId))
				{
					Ref<CompilationUnit> nestedUnit = m_CompilerInstance->GetCompilationUnit(nestedTargetId);
					if (nestedUnit && nestedUnit->GetRootAST())
					{
						CollectFromOriginalAST(nestedUnit, mergedIds, out);
					}
				}
			}
			else
			{
				out.Push(children[i]);
			}
		}
	}

}
