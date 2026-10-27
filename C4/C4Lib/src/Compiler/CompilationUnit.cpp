#include "CompilationUnit.h"
#include "Parser/ParserState.h"
#include "Compiler/CompilerInstance.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	CompilationUnit::CompilationUnit(uint32 sourceId, SourceManager& sourceManager, CompilerInstance* compilerInstance) :
		m_SourceId(sourceId), m_SourceManager(sourceManager), m_CompilerInstance(compilerInstance),
		m_MessageQueue(compilerInstance ? &compilerInstance->GetMessageQueue() : nullptr),
		m_Lexer(*sourceManager.GetInfo(sourceId).source.Get(), sourceId)
	{
		SourceInfo& info = sourceManager.GetInfo(sourceId);
		if (!info.source)
		{
			m_ErrorTracker.AddError(
				SourceLocation{},
				Core::StringFormat::Format("CompilationUnit: source ID {0} not found in SourceManager.", sourceId),
				ErrorSeverity::Fatal);
		}
	}

	void CompilationUnit::StartParse()
	{
		m_ProcessingThread = std::thread([this]() {
			ParserState parserState(m_Lexer, m_ErrorTracker, m_MessageQueue);

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
