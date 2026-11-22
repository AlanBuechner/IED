#include "Lexer.h"
#include "Lexer/LiteralScanner.h"
#include "Lexer/BraceRecovery.h"
#include "Core/Core.h"
#include "Core/StringFormat.h"
#include <algorithm>

namespace C4Compiler
{

	// -----------------------------------------------------------------------
	// Construction / Destruction
	// -----------------------------------------------------------------------


	Lexer::Lexer(const Source& source, uint32 sourceId, const MacroLayout* macros)
		: m_Source(source), m_SourceId(sourceId), m_Macros(macros && !macros->Empty() ? macros : nullptr)
	{
		m_File.data = source.Data();
		m_File.size = source.Size();
		m_LastEnd = static_cast<size_t>(-1);
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
		const bool follows = out.FollowsDirectly();
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
			if (!ContinuesCompound(out, next))
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
		out.SetFollowsDirectly(follows);
		return count;
	}

	bool Lexer::ContinuesCompound(const Token& compound, const Token& next) const
	{
		if (!IsAtomicOperator(next.GetType()))
			return false;
		// A compound is a run of adjacent characters: whitespace or a comment ends it (`* ==` is not `*=`); so does the
		// edge of a macro use with white space at it.
		if (!next.FollowsDirectly())
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
		bool IsSpace(int c) { return c == ' ' || c == '\n' || c == '\r' || c == '\t'; }

		// A '.' before a digit is a number missing its leading digit (`.5`), never a member access; the last
		// '.' of `..` or `...` (`[1...3]`) belongs to that operator.
		bool StartsNumber(std::string_view rest, bool afterDot)
		{
			return IsDigit(rest[0]) || (rest[0] == '.' && !afterDot && rest.size() > 1 && IsDigit(rest[1]));
		}

		constexpr size_t kNowhere = static_cast<size_t>(-1);
	}

	Token* Lexer::ReadToken()
	{
		const bool spaced = PassToNextToken();
		Reading& reading = m_Expansion.expansion != 0 ? m_Expansion : m_File;
		Token* slot = m_Arena.Alloc();
		const size_t start = reading.at;
		if (start >= reading.size) // only the file's reading ends here: an expansion read to its end is left
		{
			MarkEndOfFile(*slot);
			Locate(*slot, start, start);
			return slot;
		}
		const bool ranOut = Scan(*slot, reading);
		Locate(*slot, start, reading.at);
		slot->SetFollowsDirectly(!spaced && m_LastIn == reading.expansion && m_LastEnd == start);
		m_LastEnd = reading.at;
		m_LastIn = reading.expansion;
		if (slot->GetLiteralFacts().issue != LiteralIssue::None)
			Report(*slot, LiteralIssueMessage(*slot));
		if (ranOut && slot->GetType() == TokenType::MultiComment)
			Report(*slot, "this '/*' comment has no closing '*/'");
		return slot;
	}

	bool Lexer::PassToNextToken()
	{
		bool spaced = false;
		for (;;)
		{
			Reading& reading = m_Expansion.expansion != 0 ? m_Expansion : m_File;
			while (IsSpace(reading.Peek())) { ++reading.at; spaced = true; }
			if (m_Expansion.expansion != 0)
			{
				if (m_Expansion.at < m_Expansion.size) return spaced;
				LeaveExpansion();
				continue;
			}
			if (!m_Macros) return spaced;
			const std::vector<MacroLayout::Region>& regions = m_Macros->Regions();
			while (m_NextRegion < regions.size() && regions[m_NextRegion].end <= m_File.at) ++m_NextRegion; // passed (a comment took it)
			if (m_NextRegion >= regions.size() || regions[m_NextRegion].begin > m_File.at) return spaced;
			const MacroLayout::Region& region = regions[m_NextRegion++];
			if (region.expansion == 0)
			{
				m_File.at = region.end;
				spaced = true;
				continue;
			}
			if (spaced || m_LastIn != 0 || m_LastEnd != region.begin) m_LastEnd = kNowhere; // the use is apart from the token before
			else m_LastEnd = 0;
			EnterExpansion(region.expansion);
			m_LastIn = region.expansion;
			spaced = false;
		}
	}

	void Lexer::EnterExpansion(uint32 id)
	{
		const MacroExpansion& expansion = m_Macros->Expansion(id);
		m_Expansion = { expansion.text.data(), expansion.text.size(), 0, id };
		m_UseStart = LocationAt(expansion.begin);
		m_UseEnd = LocationAt(expansion.end);
	}

	void Lexer::LeaveExpansion()
	{
		const MacroExpansion& expansion = m_Macros->Expansion(m_Expansion.expansion);
		const bool endsAtEnd = m_LastIn == m_Expansion.expansion && m_LastEnd == m_Expansion.size;
		m_File.at = expansion.end;
		m_Expansion = {};
		m_LastIn = 0;
		m_LastEnd = endsAtEnd ? expansion.end : kNowhere;
	}

	void Lexer::Locate(Token& token, size_t start, size_t end) const
	{
		if (m_Expansion.expansion == 0)
		{
			token.SetByteOffset(start);
			token.SetStartLocation(LocationAt(start));
			token.SetEndLocation(LocationAt(end));
			return;
		}
		const MacroExpansion& expansion = m_Macros->Expansion(m_Expansion.expansion);
		SourceLocation first = m_UseStart, last = m_UseEnd;
		if (const MacroExpansion::Piece* piece = expansion.PieceAt(static_cast<uint32>(start)); piece && piece->fileOffset != MacroToken::kNotInFile)
		{
			first = LocationAt(piece->fileOffset + (start - piece->textBegin)); // spelled in the file: an argument's own place
			last = LocationAt(piece->fileOffset + (std::min<size_t>(end, piece->textEnd) - piece->textBegin));
		}
		first.expansion = last.expansion = m_Expansion.expansion;
		first.inExpansion = static_cast<uint32>(start);
		last.inExpansion = static_cast<uint32>(end);
		token.SetByteOffset(first.byteOffset);
		token.SetStartLocation(first);
		token.SetEndLocation(last);
	}

	SourceLocation Lexer::LocationAt(size_t byteOffset) const
	{
		SourceLocation location{};
		location.sourceId = m_SourceId;
		location.byteOffset = static_cast<uint32>(byteOffset);
		m_Source.ByteOffsetToLineColumn(byteOffset, location.line, location.column);
		return location;
	}

	bool Lexer::Scan(Token& token, Reading& reading)
	{
		const std::string_view rest = reading.Rest();
		const size_t quotePrefix = QuotedLiteralPrefix(rest);
		if (StartsNumber(rest, reading.at > 0 && reading.data[reading.at - 1] == '.'))
			ReadLiteral(token, ScanNumber(rest), reading);
		else if (quotePrefix != std::string_view::npos)
			ReadLiteral(token, ScanQuoted(rest, quotePrefix), reading);
		else
			return ReadByAppending(token, reading);
		return false;
	}

	void Lexer::ReadLiteral(Token& token, const ScannedLiteral& literal, Reading& reading)
	{
		token.SetType(literal.type);
		token.SetText(literal.text);
		token.SetLiteralFacts(literal.facts);
		reading.at += literal.length;
	}

	bool Lexer::ReadByAppending(Token& token, Reading& reading)
	{
		// Identifiers, operators and comments grow a character at a time until AppendChar closes or rejects.
		Token::TokenAppendMethod result = Token::TokenAppendMethod::Consume;
		bool ranOut = false;
		while (result == Token::TokenAppendMethod::Consume)
		{
			const int c = reading.Peek();
			if (c == -1) { ranOut = true; break; }
			++reading.at;
			result = token.AppendChar(static_cast<char>(c));
		}
		if (result == Token::TokenAppendMethod::Reject)
			--reading.at;
		token.PromoteKeyword();
		return ranOut;
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

} // namespace C4Compiler
