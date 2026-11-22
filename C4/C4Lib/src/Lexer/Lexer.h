#pragma once
#include "Token.h"
#include "LiteralScanner.h"
#include "TokenArena.h"
#include "SourceManager/Source.h"
#include "Macro/MacroLayout.h"
#include "Core/Core.h"
#include <map>
#include <vector>

namespace C4Compiler
{
	struct LexicalDiagnostic
	{
		SourceLocation location;
		Core::String message;
	};

	/// Tokeniser that reads from a Source and produces tokens via an arena.
	///
	/// It reads the file's own text through what the macro pass made of it (its MacroLayout, R8): a part left out (a
	/// directive, an inactive #if group) is passed over, and a macro use is read as its expansion, each of whose tokens is
	/// located where the file spells it (an argument's) or at the use, and knows the expansion it is in.
	///
	/// All operator symbols are lexed as atomic single-character tokens internally.
	/// Compound operators (e.g. <<, +=, &&) are formed by the caller using
	/// NextCompoundToken() which greedily combines adjacent atomic tokens.
	/// NextToken() always returns single atomic tokens.
	class Lexer
	{
	public:
		/// `macros`: what the macro pass made of the file (null: nothing; the text is read as it is).
		Lexer(const Source& source, uint32 sourceId, const MacroLayout* macros = nullptr);
		~Lexer();

		// Non-copyable, non-movable (holds a reference to Source).
		Lexer(const Lexer&) = delete;
		Lexer& operator=(const Lexer&) = delete;
		Lexer(Lexer&&) = delete;
		Lexer& operator=(Lexer&&) = delete;

		/// Source ID this lexer is tokenising.
		uint32 GetSourceId() const { return m_SourceId; }

		/// The tokens read, given back once the file's parse is over (AST nodes keep copies of theirs): a program's
		/// tokens are as big as its AST, and nothing reads them after the parse. The lexer reads nothing more.
		void ReleaseTokens();

		/// A file whose `{` and `}` do not pair up, mended before it is parsed (BraceRecovery.h): by the indentation of its
		/// lines, a `}` goes where a block that is never closed seems to end, and a `}` that closes nothing is left out, each
		/// with an error at the brace. So a brace forgotten while typing does not swallow the rest of the file. Reads every
		/// token; returns the errors (none when the braces pair up).
		std::vector<LexicalDiagnostic> RecoverBraces();

		// -----------------------------------------------------------------------
		/// @name Token Access
		/// @{

		/// Return the next single atomic token from the stream.
		/// Symbols are always returned as individual tokens (e.g. "<<" yields two Less tokens).
		const Token& NextToken();

		/// Return the next compound token from the stream.
		/// Greedily combines adjacent atomic operator tokens (no whitespace or comment between) into the longest matching
		/// compound operator (e.g. "<<" yields one DoubleLess, "<<=" yields one LessLessEquals).
		/// Non-operator tokens (identifiers, numbers, strings) are returned as-is.
		/// When in template context, '>' + '>' is NOT combined into '>>'.
		const Token& NextCompoundToken();

		/// Peek ahead at a token without consuming it. Returns the atomic token.
		/// Index 0 (default) is the next unconsumed token.
		const Token& Peek(uint32 index = 0) const;

		/// Peek ahead and return a compound token without consuming.
		/// Combines adjacent atomic operators into compounds for the peeked range.
		/// Index 0 (default) is the next unconsumed token.
		/// When in template context, '>' + '>' is NOT combined into '>>'.
		const Token& PeekCompound(uint32 index = 0) const;

		/// The lexical errors found so far (a bad literal, an unclosed comment), one per token start: a token
		/// lexed again after an undo replaces its entry instead of repeating it. The parse driver reports them.
		const std::map<size_t, LexicalDiagnostic>& Diagnostics() const { return m_Diagnostics; }

		/// Number of tokens consumed so far (tokens that have been returned by Next*Token).
		uint32 GetOffset() const { return m_Offset; }

		/// Total number of tokens currently in the arena (consumed + unconsumed).
		uint32 Count() const { return m_Arena.Count(); }

		// @}

		// -----------------------------------------------------------------------
		/// @name Template Context
		/// @{

		/// Push template context depth. When depth > 0, consecutive '>' tokens are NOT combined into '>>'.
		/// Call when entering a '<' for template arguments.
		void EnterTemplateContext();

		/// Pop template context depth. Call when leaving a '>' closing template arguments.
		void LeaveTemplateContext();

		/// Returns current template context depth.
		uint32 GetTemplateDepth() const { return m_TemplateDepth; }

		// @}

	private:
	/// The text being read: the file's own, or the expansion of a macro use (`expansion` its id, 0 for the file).
	struct Reading
	{
		const char* data = nullptr;
		size_t size = 0;
		size_t at = 0;
		uint32 expansion = 0;

		int Peek() const { return at < size ? static_cast<unsigned char>(data[at]) : -1; }
		std::string_view Rest() const { return std::string_view(data + at, size - at); }
	};

	/// Read the next raw token from the source stream into the arena.
	Token* ReadToken();
	/// The token at the reading place of `reading` (its white space already passed) into `token`: its text and kind, and
	/// where it ends in `reading`. Returns whether the text ended inside it (a `/*` with no `*/`).
	bool Scan(Token& token, Reading& reading);
	/// Past the white space of the file and the parts the macro pass left out; into an expansion when a use starts there.
	/// Returns whether white space or a comment came first.
	bool PassToNextToken();
	void EnterExpansion(uint32 id);
	void LeaveExpansion();
	/// Where a token that starts at `start` and ends at `end` of the text being read is, as the file has it.
	void Locate(Token& token, size_t start, size_t end) const;
	SourceLocation LocationAt(size_t byteOffset) const;
	/// A number, character or string scanned whole (Lexer/LiteralScanner.h).
	void ReadLiteral(Token& token, const ScannedLiteral& literal, Reading& reading);
	bool ReadByAppending(Token& token, Reading& reading);
	void Report(const Token& token, const Core::String& message);

	/// Advance pos past any comment tokens, reading from the source as needed so the arena holds
	/// enough. Shared by NextCompoundToken and PeekCompound so both treat comments identically
	/// (transparent): a Peek that surfaced a comment while Next skipped it desynced the parser.
	void SkipComments(uint32& pos);

	/// The compound token that starts at atomic `pos` (the longest run of adjacent atomic operators
	/// that combine, or the token itself) into `out`; returns how many atomics it spans.
	uint32 CombineAt(uint32 pos, Token& out);
	bool ContinuesCompound(const Token& compound, const Token& next) const;
	static void MarkEndOfFile(Token& token);
	/// Every token up to the end of the file, read.
	void ReadAll();

		const Source& m_Source;
		uint32 m_SourceId;
		const MacroLayout* m_Macros;
		Reading m_File;      // the file's own text
		Reading m_Expansion; // the expansion being read, when m_Expansion.expansion != 0
		uint32 m_NextRegion = 0;     // the first of the layout's regions the file reading has not reached
		SourceLocation m_UseStart;   // the use being read as its expansion, as the file has it
		SourceLocation m_UseEnd;
		size_t m_LastEnd = 0;        // where the token read last ends, in the text it came from
		uint32 m_LastIn = 0;         // and which text that is (an expansion's id; 0: the file)
		TokenArena m_Arena;
		uint32 m_Offset = 0;  // Number of consumed tokens (front of queue)

		// Separate ring buffer for compound tokens returned by NextCompoundToken/PeekCompound.
		// Keeps them out of the main arena so m_Offset indexing into atomics stays correct.
		static constexpr uint32 kCompoundBufferSize = 16;
		Token m_CompoundBuffer[kCompoundBufferSize];
		uint32 m_CompoundIndex = 0;

		/// Allocate one slot in the compound buffer and return a pointer to it.
		Token* AllocCompound() {
			Token* slot = &m_CompoundBuffer[m_CompoundIndex % kCompoundBufferSize];
			m_CompoundIndex++;
			return slot;
		}

		uint32 m_TemplateDepth = 0; // Template argument nesting depth
		std::map<size_t, LexicalDiagnostic> m_Diagnostics;

		// -----------------------------------------------------------------------
		// Undo / Speculative Parse - private, only accessible via LexerUndoScope
		// -----------------------------------------------------------------------

		/// Start an undo checkpoint. Tokens consumed after this point can be rolled back.
		void StartUndo();

		/// Roll back to the most recent undo checkpoint.
		void Undo();

		/// Commit the tokens consumed since the last checkpoint, discarding them from the front.
		void ApplyUndo();

	// Undo state
	struct UndoCheckpoint
	{
		uint32 offset;
		size_t arenaByteOffset; // byte offset in arena buffer to discard from on undo
		uint32 templateDepth;   // template context depth at checkpoint
	};
		Core::Vector<UndoCheckpoint> m_UndoStack;

		// RAII wrapper is the only allowed access path to undo methods
		friend class LexerUndoScope;

	public:
		// Test-only raw access to private undo methods
		friend class LexerTestAccess;
	};

	/// RAII wrapper for lexer undo checkpoints.
	///
	/// Creates an undo checkpoint on construction. On destruction, rolls back to the
	/// checkpoint (Undo) unless Commit() was explicitly called (ApplyUndo).
	///
	/// Nested scopes each create their own checkpoint. When an inner scope commits,
	/// it removes its checkpoint but does not physically remove tokens from the queue;
	/// the outermost scope handles all token removal. This prevents corruption of the
	/// outer scope's view when inner speculative parses succeed or fail independently.
	///
	/// This is the **only** way parser nodes may access lexer undo. Direct calls to
	/// Lexer::StartUndo/Undo/ApplyUndo are prohibited in parser code.
	///
	/// Usage:
	/// ```cpp
	/// auto undo = LexerUndoScope(lexer);
	/// // ... speculative parse (may call other TryParse methods that also use LexerUndoScope) ...
	/// if (!success) return nullptr;          // destructor calls Undo automatically
	/// undo.Commit();                         // commits consumed tokens
	/// return node;                           // or call Commit before early return
	/// ```
	class LexerUndoScope
	{
	public:
		LexerUndoScope(const LexerUndoScope&) = delete;
		LexerUndoScope& operator=(const LexerUndoScope&) = delete;

		explicit LexerUndoScope(Lexer& lexer)
			: m_Lexer(lexer), m_Committed(false), m_Cancelled(false)
		{
			m_Lexer.StartUndo();
		}

		~LexerUndoScope()
		{
			if (m_Cancelled) return; // already handled by Cancel()
			if (!m_Committed)
				m_Lexer.Undo();
			else
				m_Lexer.ApplyUndo();
		}

		/// Commit the speculative parse, making consumed tokens permanent.
		/// Must be called before this scope is destroyed if the parse succeeded.
		void Commit()
		{
			m_Committed = true;
		}

		/// Cancel: atomically rolls back to checkpoint and prevents destructor action.
		/// Use when you need manual rollback before continuing with non-speculative parsing.
		void Cancel()
		{
			m_Lexer.Undo();
			m_Cancelled = true;
		}

		/// Returns whether Commit() has been called.
		bool IsCommitted() const { return m_Committed; }

	private:
		Lexer& m_Lexer;
		bool m_Committed;
		bool m_Cancelled;
	};

	/// Test helper to expose private lexer methods for unit tests.
	class LexerTestAccess
	{
	public:
		static void StartUndo(Lexer& lexer) { lexer.StartUndo(); }
		static void Undo(Lexer& lexer) { lexer.Undo(); }
		static void ApplyUndo(Lexer& lexer) { lexer.ApplyUndo(); }
	};
}
