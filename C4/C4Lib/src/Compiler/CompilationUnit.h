#pragma once
#include "SourceManager/SourceManager.h"
#include "Lexer/Lexer.h"
#include "Compiler/ErrorTracker.h"
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/IncludeDeclarationNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Core/Core.h"
#include <thread>

namespace Scripting
{
	class CompilerInstance;
	class CompilerMessageQueue;
	class ProgramModel;

	class CompilationUnit : public Refable
	{
		friend class CompilerInstance;
	public:
		CompilationUnit(uint32 sourceId, SourceManager& sourceManager, CompilerInstance* compilerInstance = nullptr);

		void StartParse();
		void Join() { m_ProcessingThread.join(); }
		bool IsWorking() { return m_ProcessingThread.joinable(); }

		Lexer& GetLexer() { return m_Lexer; }
		const Lexer& GetLexer() const { return m_Lexer; }

		ErrorTracker& GetErrorTracker() { return m_ErrorTracker; }
		const ErrorTracker& GetErrorTracker() const { return m_ErrorTracker; }

		CompilerMessageQueue* GetMessageQueue() { return m_MessageQueue; }
		const CompilerMessageQueue* GetMessageQueue() const { return m_MessageQueue; }

		uint32 SourceId() const { return m_SourceId; }
		const std::unordered_set<uint32>& GetIncludes() const { return m_IncludesIDS; }

		/// Return the parsed root AST node (the implicit file-level NamespaceNode).
		/// Returns nullptr if parsing has not yet completed.
		const Ref<NamespaceNode>& GetRootAST() const { return m_RootASTNode; }

		/// Return the original unparsed children for include collection (preserves pre-merge state).
		const Core::Vector<Ref<AstNode>>& GetOriginalChildren() const { return m_OriginalChildren; }

		/// Return the program model for this compilation (doc 03): after AnalyzeProgram() every unit
		/// shares ONE ProgramModel - the single object that owns all type/symbol/constant facts and is
		/// the only thing phases query. Invalid until AnalyzeProgram() has run; callers guard on IsValid().
		const ProgramModel& GetProgramModel() const;

		/// Non-const model reference for LOWERING phases (doc 06): scope push/pop and local
		/// registration mutate the scope chain, so a lowerer holds this. Query-only consumers use
		/// the const GetProgramModel().
		ProgramModel& GetProgramModelForLowering();

		// Splice this unit's include closure into its root AST (nodes shared by Ref). Pure AST prep for
		// LSP position-walks; no semantic state is produced here. Safe to call more than once after an edit.
		void MergeIncludes();

	private:
		void ReportLexicalErrors();
		void MergeIncludedASTs(std::unordered_set<uint32>& mergedIds);
		void CollectFromOriginalAST(Ref<CompilationUnit> targetUnit, std::unordered_set<uint32>& mergedIds, Core::Vector<Ref<AstNode>>& out);
		uint32 m_SourceId;
		SourceManager& m_SourceManager;
		CompilerInstance* m_CompilerInstance;
		CompilerMessageQueue* m_MessageQueue;
		Lexer m_Lexer;
		Ref<NamespaceNode> m_RootASTNode;
		ErrorTracker m_ErrorTracker;
		std::unordered_set<uint32> m_IncludesIDS; // list of source ids for file that this file includes. this is the full collapsed hierarchy of includes

		uint32 m_LastResolvedIncludeIndex = 0;
		bool m_AstParsed = false;
		std::thread m_ProcessingThread;

		// Snapshot of original parsed children (before any merge) - used for include collection
		Core::Vector<Ref<AstNode>> m_OriginalChildren;
	};
}
