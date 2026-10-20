#include "Token.h"
#include <string_view>
#include <unordered_map>

namespace Scripting
{

	// ---------------------------------------------------------------------------
	// Compound operator table: maps (firstTokenType, secondChar) -> compoundType
	// Used by Token::CombineWith for post-hoc compound formation.
	// ---------------------------------------------------------------------------
	struct CompoundRule
	{
		TokenType first;
		char secondChar;
		TokenType result;
	};

	static constexpr CompoundRule g_CompoundRules[] =
	{
		// Two-char operators
		{ TokenType::Colon,           ':',  TokenType::DoubleColon },
		{ TokenType::Plus,            '+',  TokenType::DoublePlus },
		{ TokenType::Minus,           '-',  TokenType::DoubleMinus },
		{ TokenType::Minus,           '>',  TokenType::Arrow },
		{ TokenType::Equal,           '=',  TokenType::DoubleEqual },
		{ TokenType::Less,            '=',  TokenType::LessOrEqual },
		{ TokenType::Greater,         '=',  TokenType::GreaterOrEqual },
		{ TokenType::Plus,            '=',  TokenType::PlusEquals },
		{ TokenType::Minus,           '=',  TokenType::MinusEquals },
		{ TokenType::Star,            '=',  TokenType::StarEquals },
		{ TokenType::ForwardSlash,    '=',  TokenType::SlashEquals },
		{ TokenType::Percent,         '=',  TokenType::PercentEquals },
		{ TokenType::Exclamation,     '=',  TokenType::ExclamationEquals },
		{ TokenType::Ampersand,       '&',  TokenType::DoubleAmpersand },
		{ TokenType::Ampersand,       '=',  TokenType::AmpersandEquals },
		{ TokenType::Pipe,            '|',  TokenType::DoublePipe },
		{ TokenType::Pipe,            '=',  TokenType::PipeEquals },
		{ TokenType::Caret,           '=',  TokenType::CaretEquals },
		{ TokenType::Star,            '*',  TokenType::DoubleStar },
		{ TokenType::Tilde,           '=',  TokenType::TildeEquals },
		{ TokenType::Colon,           '=',  TokenType::ColonEquals },
		{ TokenType::BackSlash,       '\\', TokenType::BackSlashBackSlash },
		{ TokenType::At,              '@',  TokenType::AtAt },

		// Three-char operators (must appear after their two-char prefix rules)
		{ TokenType::Dot,             '.',  TokenType::DotDot },
		{ TokenType::DotDot,          '.',  TokenType::Ellipsis },
		{ TokenType::Less,            '<',  TokenType::DoubleLess },
		{ TokenType::DoubleLess,      '=',  TokenType::LessLessEquals },
		{ TokenType::Greater,         '>',  TokenType::DoubleGreater },
		{ TokenType::DoubleGreater,   '=',  TokenType::GreaterGreaterEquals },
		{ TokenType::DoublePipe,      '=',  TokenType::PipePipeEquals },
	};

	// Try to combine an atomic token with the next character into a compound token.
	// Returns Combine if successful (result written to outToken), or Reject otherwise.
	Token::TokenAppendMethod Token::CombineWith(char nextChar, Token& outToken)
	{
		for (const auto& rule : g_CompoundRules)
		{
			if (m_Type == rule.first && nextChar == rule.secondChar)
			{
				outToken.m_Type = rule.result;
				outToken.m_Text = m_Text;
				outToken.m_Text += nextChar;
				return Combine;
			}
		}
		return Reject;
	}


	Token::TokenAppendMethod Token::AppendChar(char c)
	{
	// append to comment
	if (m_Type == TokenType::Comment)
	{
		if (c == '\n')
			return ConsumeAndClose; // the newline terminates the line but is not part of the token text
		m_Text += c;
		return Consume;
	}

		// append to multi line comment
		if (m_Type == TokenType::MultiComment)
		{
			if (c == '/' && m_Text.size() >= 3 && m_Text.back() == '*') { // `/*/` is not closed: the `*` belongs to `/*`
				m_Text += c;
				return ConsumeAndClose;
			}
			m_Text += c;
			return Consume;
		}

		// check for white space
		bool isWhiteSpace = false;
		char whiteSpace[] = { ' ', '\n', '\r', '\t' };
		for (uint32 i = 0; i < sizeof(whiteSpace) / sizeof(char); i++) {
			if (c == whiteSpace[i])
				isWhiteSpace = true;
		}

		Token singleCharToken;

#define CHECK_CHAR(ch, type) if(c==(ch)) { singleCharToken.m_Type = type; goto EndSingleTokenCheck; }

		CHECK_CHAR('{', TokenType::OpenCurl);
		CHECK_CHAR('}', TokenType::CloseCurl);
		CHECK_CHAR('[', TokenType::OpenBracket);
		CHECK_CHAR(']', TokenType::CloseBracket);
		CHECK_CHAR('(', TokenType::OpenParen);
		CHECK_CHAR(')', TokenType::CloseParen);
		CHECK_CHAR(':', TokenType::Colon);
		CHECK_CHAR(';', TokenType::SemiColon);
		CHECK_CHAR(',', TokenType::Comma);

		CHECK_CHAR('+', TokenType::Plus);
		CHECK_CHAR('-', TokenType::Minus);
		CHECK_CHAR('*', TokenType::Star);
		CHECK_CHAR('/', TokenType::ForwardSlash);
		CHECK_CHAR('\\', TokenType::BackSlash);
		CHECK_CHAR('%', TokenType::Percent);
		CHECK_CHAR('~', TokenType::Tilde);
		CHECK_CHAR('^', TokenType::Caret);
		CHECK_CHAR('@', TokenType::At);

		CHECK_CHAR('.', TokenType::Dot);
		CHECK_CHAR('=', TokenType::Equal);
		CHECK_CHAR('<', TokenType::Less);
		CHECK_CHAR('>', TokenType::Greater);
		CHECK_CHAR('&', TokenType::Ampersand);
		CHECK_CHAR('|', TokenType::Pipe);

		CHECK_CHAR('!', TokenType::Exclamation);
		CHECK_CHAR('?', TokenType::QuestionMark);

		CHECK_CHAR('"', TokenType::Text);
		CHECK_CHAR('\'', TokenType::Char);

	EndSingleTokenCheck:

#undef CHECK_CHAR

		if (m_Type == TokenType::None) {

			if (isWhiteSpace)
				return Consume;

			m_Type = singleCharToken.GetType() != TokenType::None ? singleCharToken.GetType() : TokenType::Identifier;
			m_Text += c;
			return Consume;
		}

		// append to Identifier
		if (m_Type == TokenType::Identifier) {
			if (isWhiteSpace) return Reject;
			if (singleCharToken.GetType() != TokenType::None) return Reject; // check if reserved token
			m_Text += c;
			return Consume;
		}

		// Comments are handled inline because they consume characters until a terminator.
		// Operator promotions removed: operators remain atomic. Use CombineWith for compounds.
#define PROMOTE_TOKEN(oldType, newType) if(m_Type == oldType && c == TokenTypeSuffixChar(newType)) { m_Text += c; m_Type = newType; return Consume; }

		PROMOTE_TOKEN(TokenType::ForwardSlash, TokenType::Comment);
		PROMOTE_TOKEN(TokenType::ForwardSlash, TokenType::MultiComment);

#undef PROMOTE_TOKEN

		return Reject;

	}

	namespace
	{
		using KeywordTable = std::unordered_map<std::string_view, TokenType>;

		const KeywordTable& Keywords()
		{
			static const KeywordTable table = {
				{ "namespace", TokenType::Namespace }, { "class", TokenType::Class }, { "struct", TokenType::Struct },
				{ "interface", TokenType::Interface }, { "enum", TokenType::Enum }, { "union", TokenType::Union },
				{ "public", TokenType::Public }, { "private", TokenType::Private }, { "protected", TokenType::Protected },
				{ "virtual", TokenType::Virtual }, { "override", TokenType::Override },
				{ "const", TokenType::Const }, { "static", TokenType::Static },
				{ "if", TokenType::If }, { "else", TokenType::Else }, { "for", TokenType::For }, { "while", TokenType::While },
				{ "do", TokenType::Do }, { "switch", TokenType::Switch }, { "case", TokenType::Case },
				{ "default", TokenType::Default }, { "break", TokenType::Break }, { "continue", TokenType::Continue },
				{ "return", TokenType::Return }, { "defer", TokenType::Defer }, { "goto", TokenType::Goto },
				{ "fallthrough", TokenType::Fallthrough }, { "try", TokenType::Try }, { "catch", TokenType::Catch },
				{ "finally", TokenType::Finally }, { "throw", TokenType::Throw },
				{ "delete", TokenType::Delete }, { "new", TokenType::New },
				{ "true", TokenType::True }, { "false", TokenType::False }, { "null", TokenType::Null },
				{ "nullptr", TokenType::Null }, { "this", TokenType::This }, { "funcptr", TokenType::FunPtr },
				{ "alias", TokenType::Alias }, { "template", TokenType::Template }, { "extern", TokenType::Extern },
				{ "is", TokenType::Is }, { "as", TokenType::As }, { "extends", TokenType::Extends },
			};
			return table;
		}
	}

	void Token::PromoteKeyword()
	{
		if (m_Type != TokenType::Identifier)
			return;
		const KeywordTable& keywords = Keywords();
		const auto it = keywords.find(std::string_view(m_Text));
		if (it != keywords.end())
			m_Type = it->second;
	}
}
