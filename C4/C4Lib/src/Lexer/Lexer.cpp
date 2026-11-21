#include "Lexer.h"
#include "Lexer/LiteralScanner.h"
#include "Lexer/BraceRecovery.h"
#include "Core/Core.h"
#include "Core/StringFormat.h"
#include <algorithm>

namespace Scripting
{

	// -----------------------------------------------------------------------
	// Construction / Destruction
	// -----------------------------------------------------------------------

	Lexer::Lexer(Source& source, uint32 sourceId)
		: m_Source(source), m_SourceId(sourceId), m_SourceData(source.Data())
	{
	}

	Lexer::~Lexer() = default;

	// -----------------------------------------------------------------------
	// Token Access - Atomic (single-symbol) tokens
	// -----------------------------------------------------------------------

	const Token& Lexer::NextToken()
	{
		if (m_Offset >= m_Arena.Count())
			ReadToken();

		const Token& result = m_Arena.At(m_Offset);
		m_Offset++;
		return result;
	}

	const Token& Lexer::Peek(uint32 index) const
	{
		uint32 peekAbsolute = m_Offset + index;

		// If we haven't read far enough, we need to read more.
		// Use const_cast because Peek logically doesn't modify observable state
		// (it just fills the internal cache).
		Lexer& self = const_cast<Lexer&>(*this);
		while (peekAbsolute >= self.m_Arena.Count())
			self.ReadToken();

		if (peekAbsolute < m_Arena.Count())
			return m_Arena.At(peekAbsolute);

		return m_Arena.At(m_Arena.Count() - 1); // EOF
	}

	// -----------------------------------------------------------------------
	// Token Access - Compound tokens
	// -----------------------------------------------------------------------

	void Lexer::SkipComments(uint32& pos)
	{
		while (true)
		{
			if (pos >= m_Arena.Count())
				ReadToken();

			if (pos >= m_Arena.Count())
				return; // EOF

			const Token& current = m_Arena.At(pos);
			if (current.GetType() != TokenType::Comment &&
			    current.GetType() != TokenType::MultiComment)
				return;

			pos++;
		}
	}

	const Token& Lexer::NextCompoundToken()
	{
		SkipComments(m_Offset);
		Token* slot = AllocCompound();
		if (m_Offset >= m_Arena.Count())
		{
			MarkEndOfFile(*slot);
			return *slot;
		}
		const uint32 count = CombineAt(m_Offset, *slot);
		m_Offset += count;
		return *slot;
	}

	const Token& Lexer::PeekCompound(uint32 index) const
	{
		Lexer& self = const_cast<Lexer&>(*this);
		Token* slot = self.AllocCompound();
		uint32 pos = m_Offset;
		for (uint32 i = 0; i <= index; ++i)
		{
			self.SkipComments(pos);
			if (pos >= m_Arena.Count())
			{
				MarkEndOfFile(*slot);
				return *slot;
			}
			pos += self.CombineAt(pos, *slot);
		}
		return *slot;
	}

	uint32 Lexer::CombineAt(uint32 pos, Token& out)
	{
		// By value: reading ahead below may reallocate the arena.
		out = m_Arena.At(pos);
		if (!IsAtomicOperator(out.GetType()))
			return 1;

		const size_t byteOffset = out.GetByteOffset();
		const SourceLocation start = out.GetStartLocation();
		SourceLocation end = out.GetEndLocation();
		uint32 count = 1;
		while (true)
		{
			if (pos + count >= m_Arena.Count())
				ReadToken();
			if (pos + count >= m_Arena.Count())
				break;
			const Token& next = m_Arena.At(pos + count);
			if (!ContinuesCompound(out, byteOffset, next))
				break;
			Token combined;
			if (out.CombineWith(next.GetText()[0], combined) != Token::TokenAppendMethod::Combine)
				break;
			end = next.GetEndLocation();
			out = combined;
			++count;
		}
		out.SetByteOffset(byteOffset);
		out.SetStartLocation(start);
		out.SetEndLocation(end);
		return count;
	}

	bool Lexer::ContinuesCompound(const Token& compound, size_t byteOffset, const Token& next) const
	{
		if (!IsAtomicOperator(next.GetType()))
			return false;
		// A compound is a run of adjacent characters: whitespace or a comment ends it (`* ==` is not `*=`).
		if (next.GetByteOffset() != byteOffset + compound.GetText().size())
			return false;
		// `Map<K<V>>`: inside template arguments `>` `>` stays two closers.
		return !(m_TemplateDepth > 0 && compound.GetType() == TokenType::Greater && next.GetText()[0] == '>');
	}

	void Lexer::MarkEndOfFile(Token& token)
	{
		token.SetType(TokenType::EndOfFile);
		token.SetText("");
	}

	// -----------------------------------------------------------------------
	// Internal Token Reading
	// -----------------------------------------------------------------------

	namespace
	{
		bool IsDigit(char c) { return c >= '0' && c <= '9'; }

		// A '.' before a digit is a number missing its leading digit (`.5`), never a member access; the last
		// '.' of `..` or `...` (`[1...3]`) belongs to that operator.
		bool StartsNumber(std::string_view rest, bool afterDot)
		{
			return IsDigit(rest[0]) || (rest[0] == '.' && !afterDot && rest.size() > 1 && IsDigit(rest[1]));
		}
	}

	Token* Lexer::ReadToken()
	{
		SkipWhitespace();
		Token* slot = m_Arena.Alloc();

		const size_t start = m_Source.GetReadPosition();
		slot->SetByteOffset(start);
		slot->SetStartLocation(LocationAt(start));
		if (start >= m_Source.Size())
		{
			MarkEndOfFile(*slot);
			slot->SetEndLocation(slot->GetStartLocation());
			return slot;
		}

		const std::string_view rest(m_Source.Data() + start, m_Source.Size() - start);
		const size_t quotePrefix = QuotedLiteralPrefix(rest);
		if (StartsNumber(rest, start > 0 && m_Source.Data()[start - 1] == '.'))
			ReadLiteral(*slot, ScanNumber(rest));
		else if (quotePrefix != std::string_view::npos)
			ReadLiteral(*slot, ScanQuoted(rest, quotePrefix));
		else
			ReadByAppending(*slot);
		slot->SetEndLocation(LocationAt(m_Source.GetReadPosition()));
		return slot;
	}

	void Lexer::SkipWhitespace()
	{
		while (true)
		{
			const int c = m_Source.ReadCharAt(m_Source.GetReadPosition());
			if (c != ' ' && c != '\n' && c != '\r' && c != '\t')
				return;
			m_Source.ReadChar();
		}
	}

	SourceLocation Lexer::LocationAt(size_t byteOffset) const
	{
		SourceLocation location{};
		location.sourceId = m_SourceId;
		location.byteOffset = byteOffset;
		m_Source.ByteOffsetToLineColumn(byteOffset, location.line, location.column);
		return location;
	}

	void Lexer::ReadLiteral(Token& token, const ScannedLiteral& literal)
	{
		token.SetType(literal.type);
		token.SetText(literal.text);
		token.SetLiteralFacts(literal.facts);
		m_Source.Seek(token.GetByteOffset() + literal.length);
		if (literal.facts.issue != LiteralIssue::None)
			Report(token, LiteralIssueMessage(token));
	}

	void Lexer::ReadByAppending(Token& token)
	{
		// Identifiers, operators and comments grow a character at a time until AppendChar closes or rejects.
		Token::TokenAppendMethod result = Token::TokenAppendMethod::Consume;
		while (result == Token::TokenAppendMethod::Consume)
		{
			const int c = m_Source.ReadChar();
			if (c == -1)
			{
				if (token.GetType() == TokenType::MultiComment)
					Report(token, "this '/*' comment has no closing '*/'");
				break;
			}
			result = token.AppendChar(static_cast<char>(c));
		}
		if (result == Token::TokenAppendMethod::Reject)
			m_Source.PushBack();
		token.PromoteKeyword();
	}

	void Lexer::Report(const Token& token, const Core::String& message)
	{
		m_Diagnostics[token.GetByteOffset()] = { token.GetStartLocation(), message };
	}

	// -----------------------------------------------------------------------
	// Template Context
	// -----------------------------------------------------------------------

	void Lexer::EnterTemplateContext()
	{
		m_TemplateDepth++;
	}

	void Lexer::LeaveTemplateContext()
	{
		if (m_TemplateDepth > 0)
			m_TemplateDepth--;
	}

	void Lexer::ReleaseTokens()
	{
		m_Arena.Clear();
		m_UndoStack.Clear();
		m_Offset = 0;
	}

	void Lexer::ReadAll()
	{
		while (m_Arena.Empty() || m_Arena.At(m_Arena.Count() - 1).GetType() != TokenType::EndOfFile)
			ReadToken();
	}

	// -----------------------------------------------------------------------
	// Brace recovery (BraceRecovery.h)
	// -----------------------------------------------------------------------

	namespace
	{
		bool IsComment(const Token& token)
		{
			return token.GetType() == TokenType::Comment || token.GetType() == TokenType::MultiComment;
		}

		// The `}` that closes a block brace recovery found open: right after the last token before it.
		Token ClosingAfter(const Token& last)
		{
			Token closing(TokenType::CloseCurl, last.GetEndLocation().byteOffset);
			closing.SetText("}");
			closing.SetStartLocation(last.GetEndLocation());
			closing.SetEndLocation(last.GetEndLocation());
			return closing;
		}
	}

	std::vector<LexicalDiagnostic> Lexer::RecoverBraces()
	{
		ReadAll();
		std::vector<BraceFact> facts;
		std::vector<uint32> tokenOf; // a fact's token in the arena
		uint32 line = 0, indent = 0;
		for (uint32 at = 0; at < m_Arena.Count(); ++at)
		{
			const Token& token = m_Arena.At(at);
			if (IsComment(token)) continue;
			const bool startsLine = token.GetStartLocation().line != line;
			if (startsLine)
			{
				line = token.GetStartLocation().line;
				indent = token.GetStartLocation().column;
			}
			facts.push_back({ token.GetType(), indent, startsLine });
			tokenOf.push_back(at);
		}
		const BraceRepairs repairs = PlanBraceRepairs(facts);
		if (repairs.Empty()) return {};

		std::vector<LexicalDiagnostic> errors;
		for (const BraceRepairs::Closing& closing : repairs.closings)
		{
			const Token& before = m_Arena.At(tokenOf[closing.before]);
			const Core::String where = before.GetType() == TokenType::EndOfFile ? Core::String("at the end of the file")
				: Core::StringFormat::Format("before line {0}", before.GetStartLocation().line);
			errors.push_back({ m_Arena.At(tokenOf[closing.open]).GetStartLocation(),
				"this '{' has no matching '}': by the indentation, its block ends " + where });
		}
		for (uint32 dropped : repairs.dropped)
			errors.push_back({ m_Arena.At(tokenOf[dropped]).GetStartLocation(), "this '}' has no matching '{': it is left out" });

		std::vector<Token> tokens;
		tokens.reserve(m_Arena.Count() + repairs.closings.size());
		size_t nextClosing = 0, nextDropped = 0;
		const Token* last = nullptr; // the last token that is not a comment
		for (uint32 fact = 0, at = 0; at < m_Arena.Count(); ++at)
		{
			const Token& token = m_Arena.At(at);
			const bool isFact = fact < tokenOf.size() && tokenOf[fact] == at;
			if (isFact)
			{
				for (; nextClosing < repairs.closings.size() && repairs.closings[nextClosing].before == fact; ++nextClosing)
					tokens.push_back(ClosingAfter(last ? *last : token));
				const bool drop = nextDropped < repairs.dropped.size() && repairs.dropped[nextDropped] == fact;
				++fact;
				if (drop) { ++nextDropped; continue; }
				last = &token;
			}
			tokens.push_back(token);
		}
		m_Arena.Clear();
		for (Token& token : tokens) *m_Arena.Alloc() = std::move(token);
		return errors;
	}

	// -----------------------------------------------------------------------
	// Undo / Speculative Parse
	// -----------------------------------------------------------------------

	void Lexer::StartUndo()
	{
		UndoCheckpoint checkpoint;
		checkpoint.offset = m_Offset;
		checkpoint.arenaByteOffset = sizeof(Token) * m_Arena.Count();
		checkpoint.templateDepth = m_TemplateDepth;
		m_UndoStack.Push(checkpoint);
	}

	void Lexer::Undo()
	{
		if (m_UndoStack.Empty())
			return;

		UndoCheckpoint checkpoint = m_UndoStack.Back();
		m_UndoStack.Pop();

		uint32 restoredOffset = checkpoint.offset;
		if (restoredOffset > m_Arena.Count())
			restoredOffset = m_Arena.Count();

		m_Offset = restoredOffset;
		m_TemplateDepth = checkpoint.templateDepth; // restore template depth
	}

	void Lexer::ApplyUndo()
	{
		if (m_UndoStack.Empty())
			return;

		// Pop the checkpoint - this commits the undo scope.
		UndoCheckpoint checkpoint = m_UndoStack.Back();
		m_UndoStack.Pop();

		// Do NOT compact the arena. Tokens are already consumed into AST nodes,
		// and m_Offset is already at the correct position for continued parsing.
		// Compaction breaks nested speculative parsing because inner scope commits
		// would corrupt outer scope checkpoint offsets.
	}

} // namespace Scripting
