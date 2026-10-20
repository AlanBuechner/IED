#pragma once
#include "Core/Core.h"
#include <cstddef>
#include <cstdint>
#include <typeinfo>

namespace Scripting
{

	enum class TokenType
	{
		// --- Meta ---
		None,
		EndOfFile,

		// --- Literals ---
		Identifier,
		Text,       // string literal
		Number,
		Char,       // character literal

		// --- Delimiters & Punctuation ---
		OpenCurl,      // {
		CloseCurl,     // }
		OpenBracket,   // [
		CloseBracket,  // ]
		OpenParen,     // (
		CloseParen,    // )
		Colon,         // :
		DoubleColon,   // ::
		SemiColon,     // ;
		Comma,         // ,
		Dot,           // .

		// --- Arithmetic Operators ---
		Plus,              // +
		DoublePlus,        // ++
		Minus,             // -
		DoubleMinus,       // --
		Star,              // *
		DoubleStar,        // **
		ForwardSlash,      // /
		BackSlash,         /* \ */
		BackSlashBackSlash,/* \\ */
		Percent,           // %

		// --- Comparison Operators ---
		Equal,             // =
		DoubleEqual,       // ==
		ExclamationEquals, // !=
		Less,              // <
		Greater,           // >
		LessOrEqual,       // <=
		GreaterOrEqual,    // >=

		// --- Assignment Operators ---
		PlusEquals,         // +=
		MinusEquals,        // -=
		StarEquals,         // *=
		SlashEquals,        // /=
		PercentEquals,      // %=
		AmpersandEquals,    // &=
		PipeEquals,         // |=
		CaretEquals,        // ^=
		TildeEquals,        // ~=
		ColonEquals,        // :=
		LessLessEquals,     // <<=
		GreaterGreaterEquals,// >>=
		PipePipeEquals,     // ||=

		// --- Logical Operators ---
		DoubleAmpersand, // &&
		DoublePipe,      // ||
		Exclamation,     // !

		// --- Bitwise & Shift Operators ---
		Ampersand,   // &
		Pipe,        // |
		Caret,       // ^
		Tilde,       // ~
		DoubleLess,  // <<
		DoubleGreater,// >>

		// --- Other Operators ---
		Arrow,         // ->
		QuestionMark,  // ?
		DotDot,        // ..
		Ellipsis,      // ...
		At,            // @
		AtAt,          // @@

		// --- Comments ---
		Comment,
		MultiComment,

		// --- Keywords (reserved words) ---
		// Types & Declarations
		Namespace,
		Class,
		Struct,
		Enum,
		Union,
		Interface,
		Alias,        // alias Name = Type;
		FunPtr,       // funptr<R(P)> function pointer type
		Template,
		Extern,       // extern "c"/"c++" include|source "path"; (C/C++ interop)

		// Access Modifiers
		Public,
		Private,
		Protected,

		// Method Qualifiers
		Virtual,
		Override,

		// Storage & Type Qualifiers
		Const,
		Static,

		// Control Flow - Conditionals
		If,
		Else,
		Switch,
		Case,
		Default,
		Fallthrough,

		// Control Flow - Loops
		For,
		While,
		Do,
		Break,
		Continue,
		// Control Flow - Misc
		Return,
		Defer,
		Goto,
		Try,
		Catch,
		Finally,
		Throw,

		// Memory & Objects
		Delete,
		New,
		This,

		// Type Operators
		Is,       // is - type-check operator (value is int)
		As,       // as - safe cast operator (value as String*)
		Extends,  // extends - extension method target (void Add(P) extends Point { })

		// Literals (reserved)
		True,
		False,
		Null,
	};

	/// Returns true if this token type is an atomic single-character operator
	/// that can participate in compound operator formation via CombineWith().
	inline bool IsAtomicOperator(TokenType type)
	{
		switch (type) {
			case TokenType::Colon:
			case TokenType::Plus:
			case TokenType::Minus:
			case TokenType::Equal:
			case TokenType::Less:
			case TokenType::Greater:
			case TokenType::Star:
			case TokenType::ForwardSlash:
			case TokenType::Percent:
			case TokenType::Ampersand:
			case TokenType::Pipe:
			case TokenType::Caret:
			case TokenType::Tilde:
			case TokenType::Exclamation:
			case TokenType::Dot:
			case TokenType::BackSlash:
			case TokenType::At:
				return true;
			default:
				return false;
		}
	}

	/// Return the character associated with a token type that is formed by appending
	/// a second character (used for comment promotion: Comment -> '/', MultiComment -> '*').
	inline char TokenTypeSuffixChar(TokenType type)
	{
		switch (type) {
			case TokenType::Comment: return '/';
			case TokenType::MultiComment: return '*';
			default: return 0;
		}
	}

	struct SourceLocation
	{
		uint32 line = 0;       // 1-based line number
		uint32 column = 0;     // 1-based column number (within the line)
		size_t byteOffset = 0; // byte offset from the beginning of the source
		uint32 sourceId = 0;   // identifier for the source file

		bool operator==(const SourceLocation& other) const
		{
			return line == other.line && column == other.column && byteOffset == other.byteOffset && sourceId == other.sourceId;
		}
		bool operator!=(const SourceLocation& other) const { return !(*this == other); }
	};

	/// What is wrong with a literal token. A token is lexed once, so the parse that consumes it reports the
	/// issue: an undone speculative parse neither loses nor repeats it.
	enum class LiteralIssue : uint8
	{
		None,
		InvalidSuffix,        // `10q`, `1.5u`: detail is where the suffix starts in the text
		FloatSuffixOnInteger, // `1f`
		NoLeadingDigit,       // `.5`: no digit before the '.'
		InvalidDigit,         // `0b12`, `09`: detail is the digit
		MissingDigits,        // `0x`, `1e`: detail is 'x', 'b' or 'e'
		UnknownEscape,        // `\q`: detail is the character after the backslash
		BadUniversalEscape,   // `\u12`, a surrogate, beyond U+10FFFF: detail is 'u' or 'U'
		EscapeOutOfRange,     // `\777` in a char8: detail is the value
		Unterminated,         // no closing quote on the line: detail is the quote
		EmptyCharacter,       // `''`
		MultiCharacter,       // `'ab'`
		CharacterOutOfRange,  // `'\u00e9'` (two UTF-8 bytes) in a char8: detail is the code point
		InvalidUtf8,          // the source bytes of a u"" / U"" literal are not UTF-8
	};

	/// The facts the lexer records on a literal token: the code-unit width of a character or string
	/// (1 char8, 2 char16, 4 char32) and the first issue found.
	struct LiteralFacts
	{
		uint8 unitWidth = 1;
		LiteralIssue issue = LiteralIssue::None;
		uint32 detail = 0;
	};

	class Token
	{
	public:

		enum TokenAppendMethod
		{
			Reject,
			Consume,
			ConsumeAndClose,
			Combine,  // Two atomic tokens were successfully combined into a compound token
		};

	public:
		Token() : m_Type(TokenType::None), m_ByteOffset(0) {}
		Token(TokenType type) : m_Type(type), m_ByteOffset(0) {}
		Token(TokenType type, size_t byteOffset)
			: m_Type(type), m_ByteOffset(byteOffset) {
		}

		TokenType GetType() const { return m_Type; }
		const Core::String& GetText() const { return m_Text; }
		const SourceLocation& GetStartLocation() const { return m_StartLocation; }
		const SourceLocation& GetEndLocation() const { return m_EndLocation; }

		/// Absolute byte offset of the token start from the beginning of the source file.
		size_t GetByteOffset() const { return m_ByteOffset; }
		void SetByteOffset(size_t byteOffset) { m_ByteOffset = byteOffset; }

		void SetStartLocation(const SourceLocation& loc) { m_StartLocation = loc; }
		void SetEndLocation(const SourceLocation& loc) { m_EndLocation = loc; }
		Token& SetType(TokenType type) { m_Type = type; return *this; }
		void SetText(const Core::String& text) { m_Text = text; }

		TokenAppendMethod AppendChar(char c);

		/// A character or string's code-unit width and a literal's issue (`LiteralIssueMessage` words it).
		const LiteralFacts& GetLiteralFacts() const { return m_LiteralFacts; }
		void SetLiteralFacts(const LiteralFacts& facts) { m_LiteralFacts = facts; }

		/// Try to combine this atomic token with the next character into a compound token.
		/// If successful, writes the combined token to outToken and returns Combine.
		/// Otherwise returns Reject and leaves outToken unchanged.
		TokenAppendMethod CombineWith(char nextChar, Token& outToken);

		/// An identifier that spells a keyword becomes that keyword's token.
		void PromoteKeyword();

	private:
		TokenType m_Type;
		size_t m_ByteOffset;           // Absolute byte offset from the start of the source.
		Core::String m_Text;
		SourceLocation m_StartLocation;
		SourceLocation m_EndLocation;
		LiteralFacts m_LiteralFacts;
};
}
