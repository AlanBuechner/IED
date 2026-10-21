#pragma once
#include "Core/Core.h"
#include "Lexer/Lexer.h"
#include "Compiler/ErrorTracker.h"
#include <algorithm>
#include <mutex>
#include <vector>
#include <condition_variable>

namespace Scripting
{
	class CompilerMessageQueue;

	class ParserState
	{
	public:
		// A parse reports one error a place: what follows a syntax error at the token it stopped on is its consequence.
		ParserState(Lexer& lexer, ErrorTracker& errorTracker, CompilerMessageQueue* messageQueue = nullptr)
			: m_Lexer(lexer), m_ErrorTracker(errorTracker), m_MessageQueue(messageQueue)
		{
			errorTracker.ReportOncePerLocation();
		}

		ParserState(const ParserState&)            = delete;
		ParserState& operator=(const ParserState&) = delete;

		Lexer& GetLexer() { return m_Lexer; }
		const Lexer& GetLexer() const { return m_Lexer; }

		ErrorTracker& GetErrorTracker() { return m_ErrorTracker; }
		const ErrorTracker& GetErrorTracker() const { return m_ErrorTracker; }

		CompilerMessageQueue* GetMessageQueue() { return m_MessageQueue; }
		const CompilerMessageQueue* GetMessageQueue() const { return m_MessageQueue; }

		/// Whether the parser is currently in a type context.
		/// When true, keywords that can also serve as type names (e.g. `class`, `struct`)
		/// are treated as valid identifiers for type resolution.
		bool IsTypeContext() const { return m_IsTypeContext; }
		void SetTypeContext(bool value) { m_IsTypeContext = value; }

		// Parsing is recursive descent, so each level of expression nesting consumes stack.
		// Pathologically deep input (e.g. thousands of nested parens) would otherwise overflow the
		// compiler's stack and crash it. This cap turns that into a clean "nesting too deep" error.
		static constexpr uint32 kMaxParseDepth = 256;

		uint32 GetParseDepth() const { return m_ParseDepth; }
		void EnterParseLevel() { ++m_ParseDepth; }
		void LeaveParseLevel() { if (m_ParseDepth > 0) --m_ParseDepth; }

		// Separate cap for block nesting: each `{ ... }` block is a recursive descent frame
		// (CodeBlockNode::TryParse -> ParseBody -> nested statement -> nested block). Deeply nested
		// blocks are a different recursion path than expressions, so they need their own guard.
		static constexpr uint32 kMaxBlockDepth = 256;

		uint32 GetBlockDepth() const { return m_BlockDepth; }
		void EnterBlockLevel() { ++m_BlockDepth; }
		void LeaveBlockLevel() { if (m_BlockDepth > 0) --m_BlockDepth; }

		/// A type parameter of a template whose declaration is being parsed: inside `template<T> ...` the name `T`
		/// is a type, so `(T)-1` casts as `(int32)-1` does.
		bool IsTypeParameter(const Core::String& name) const
		{
			return std::find(m_TypeParameters.begin(), m_TypeParameters.end(), name) != m_TypeParameters.end();
		}

	private:
		friend class TypeParameterScope;
		Lexer& m_Lexer;
		ErrorTracker& m_ErrorTracker;
		CompilerMessageQueue* m_MessageQueue;
		bool m_IsTypeContext = false; // controls whether we're parsing type names
		uint32 m_ParseDepth = 0;      // current expression nesting depth (recursive descent)
		uint32 m_BlockDepth = 0;      // current block nesting depth (recursive descent)
		std::vector<Core::String> m_TypeParameters;

	};

	/// The type parameters one declaration brings into scope, until the scope ends.
	class TypeParameterScope
	{
	public:
		explicit TypeParameterScope(ParserState& state) : m_State(state), m_Mark(state.m_TypeParameters.size()) {}
		~TypeParameterScope() { m_State.m_TypeParameters.resize(m_Mark); }
		TypeParameterScope(const TypeParameterScope&) = delete;
		TypeParameterScope& operator=(const TypeParameterScope&) = delete;

		void Add(const Core::String& name) { m_State.m_TypeParameters.push_back(name); }

	private:
		ParserState& m_State;
		size_t m_Mark;
	};
}
