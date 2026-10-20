#include "Features/Formatting.h"
#include "Protocol/LspTypes.h"
#include "Lexer/Lexer.h"
#include "SourceManager/Source.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <vector>

namespace C4LSP::Features
{
	namespace
	{
		using Scripting::TokenType;

		struct Tok
		{
			TokenType type = TokenType::None;
			size_t start = 0; // byte offset of first character in the document
			size_t end = 0;   // one past last character (filled after the full pass)
		};

		bool IsWordLike(TokenType t)
		{
			switch (t)
			{
			case TokenType::Identifier: case TokenType::Number:
			case TokenType::Char:       case TokenType::Text:
			case TokenType::True:       case TokenType::False:  case TokenType::Null:
			case TokenType::Namespace:  case TokenType::Class:    case TokenType::Struct:
			case TokenType::Interface:  case TokenType::Enum:     case TokenType::Union:
			case TokenType::Alias:      case TokenType::FunPtr:   case TokenType::Template:
			case TokenType::Extern:     case TokenType::Public:   case TokenType::Private:
			case TokenType::Protected:  case TokenType::Virtual:  case TokenType::Override:
			case TokenType::Const:      case TokenType::Static:
			case TokenType::If:         case TokenType::Else:     case TokenType::Switch:
			case TokenType::Case:       case TokenType::Default:  case TokenType::Fallthrough:
			case TokenType::For:        case TokenType::While:    case TokenType::Do:
			case TokenType::Break:      case TokenType::Continue:
			case TokenType::Return:     case TokenType::Defer:    case TokenType::Goto:
			case TokenType::Try:        case TokenType::Catch:    case TokenType::Finally:
			case TokenType::Throw:      case TokenType::Delete:   case TokenType::New:
			case TokenType::This:       case TokenType::Is:       case TokenType::As:
			case TokenType::Extends:
				return true;
			default:
				return false;
			}
		}

		bool IsControlKeyword(TokenType t)
		{
			switch (t)
			{
			case TokenType::If: case TokenType::For: case TokenType::While: case TokenType::Switch:
				return true;
			default:
				return false;
			}
		}

		bool IsBinaryOp(TokenType t)
		{
			switch (t)
			{
			case TokenType::Plus: case TokenType::Minus: case TokenType::ForwardSlash:
			case TokenType::Percent:
			case TokenType::Equal: case TokenType::DoubleEqual: case TokenType::ExclamationEquals:
			case TokenType::LessOrEqual: case TokenType::GreaterOrEqual:
			case TokenType::DoubleAmpersand: case TokenType::DoublePipe: // && / || have no unary form, so the word-like-prev rule cannot misfire
				// NOTE: Less/Greater are deliberately NOT here. They double as generic delimiters
				// (`Vector<int32>`), and a lexer-only formatter cannot tell `a < b` from a template,
				// so their spacing is left exactly as the author wrote it (spacing there can never
				// change meaning anyway). <= / >= are unambiguous comparisons.
				return true;
			default:
				return false;
			}
		}

		bool IsCloser(TokenType t) { return t == TokenType::CloseCurl || t == TokenType::CloseParen || t == TokenType::CloseBracket; }

		int Delta(const Tok& t)
		{
			switch (t.type)
			{
			case TokenType::OpenCurl: case TokenType::OpenParen: case TokenType::OpenBracket: return +1;
			case TokenType::CloseCurl: case TokenType::CloseParen: case TokenType::CloseBracket: return -1;
			default: return 0;
			}
		}

		bool IsComment(TokenType t) { return t == TokenType::Comment || t == TokenType::MultiComment; }

		struct Formatter
		{
		const std::string& text;
		const Formatting::Options options;
			std::vector<size_t> lineStarts;   // byte offset of first char on each 0-based line
			std::vector<Tok> tokens;          // in source order, no EOF token
			std::vector<Protocol::TextEdit> edits;

			Formatter(const std::string& inText, const Formatting::Options& inOptions) : text(inText), options(inOptions) {}

		void Run()
		{
			BuildLineTable();
			LexAll();
			if (tokens.empty())
				return;
			FixIndentationAndTrailing();
			FixSpacing();
		}

		uint32 LineOf(size_t byteOffset) const
			{
				auto it = std::upper_bound(lineStarts.begin(), lineStarts.end(), byteOffset);
				return static_cast<uint32>(it - lineStarts.begin()) - 1; // >= 0: first line start is 0 <= offset
			}

			void BuildLineTable()
			{
				lineStarts.push_back(0);
				for (size_t i = text.find('\n'); i != std::string::npos; i = text.find('\n', i + 1))
					lineStarts.push_back(i + 1);
			}

		void LexAll()
		{
			auto source = Scripting::Source::FromMemory(text);
			Scripting::Lexer lexer(*source.Get(), /*sourceId=*/0); // formatter never resolves against a SourceManager

			// The real compiler lexes compound operators (->, ==, <=, <<...): NextCompoundToken greedily
			// combines adjacent atomic operator tokens into the longest match. The formatter must see the
			// same token stream or its spacing rules misfire - e.g. `a -> b` would be spaced as two binary
			// ops (`- >`) and `x == y` becomes `= =`. NextCompoundToken cannot be used directly because it
			// skips comment tokens, which the formatter needs (comment-only lines must keep their indentation;
			// gaps around comments are never reflowed). So combine manually with the same greedy rule -
			// always extending the *result* of the previous combination so 3-char operators work too - and
			// keep comments in place. Template depth (which stops `>>` from combining inside generics) is a
			// parse concern: spacing around < > can never change meaning, so it does not matter here.
			while (true)
			{
				const Scripting::Token& tok = lexer.NextToken();
				if (tok.GetType() == TokenType::EndOfFile)
					break;

				Tok t;
				t.type = tok.GetType();
				t.start = tok.GetByteOffset();
				t.end = tok.GetEndLocation().byteOffset; // the exact source extent (a literal's text is its decoded value)

				Scripting::Token combined = tok; // running copy of what has been consumed so far (atomic ops are one char each)
				if (Scripting::IsAtomicOperator(t.type))
					while (true)
					{
						const Scripting::Token& next = lexer.Peek(0);
						if (!Scripting::IsAtomicOperator(next.GetType()) || next.GetByteOffset() != t.end)
							break; // a compound operator is adjacent characters, as the compiler lexes it
						Scripting::Token attempt;
						if (combined.CombineWith(next.GetText()[0], attempt) != Scripting::Token::TokenAppendMethod::Combine)
							break;
						combined = attempt; // extend the result, not the original atomic token
						t.type = combined.GetType();
						t.end = next.GetEndLocation().byteOffset;
						lexer.NextToken(); // consume the second (or third) half of the compound operator
					}

				if (t.end > text.size()) // unterminated string/comment at EOF: clamp so gap math stays in bounds
					t.end = text.size();
				tokens.push_back(t);
			}
		}

			void FixIndentationAndTrailing()
			{
				int depth = 0; // openers minus closers seen on all previous lines
				for (uint32 li = 0; li < lineStarts.size(); ++li)
				{
					size_t ls = lineStarts[li];
					size_t nl = text.find('\n', ls);
					size_t lineEnd = (nl == std::string::npos) ? text.size() : nl;

					// Non-comment token range on this line.
					uint32 firstIdx = 0, lastIdx = 0; bool hasToken = false;
					for (uint32 i = 0; i < tokens.size(); ++i)
						if (LineOf(tokens[i].start) == li && !IsComment(tokens[i].type))
						{ if (!hasToken) { firstIdx = i; hasToken = true; } lastIdx = i; }

					const Tok* first = nullptr; const Tok* last = nullptr;
					if (hasToken) { first = &tokens[firstIdx]; last = &tokens[lastIdx]; }

				// Leading whitespace: only ever touched when it is pure whitespace, so a line
				// whose content starts inside a multi-line comment (`... */ int x`) is left alone.
				if (first && IsAllWhitespace(text.substr(ls, first->start - ls)))
				{
					uint32 target = depth;
					if (IsCloser(first->type))
						target = depth > 0 ? static_cast<uint32>(depth) - 1 : 0;
					std::string want = IndentString(target);
					if (text.substr(ls, first->start - ls) != want)
						AddEdit(li, ls, first->start, want);
				}

			// A line with no non-comment token at all: collapse stray indentation on otherwise-blank
			// lines to nothing. Comment-only lines are left alone (comments never get re-indented).
			if (!first && !last)
			{
				std::string content = text.substr(ls, lineEnd - ls);
				if (!content.empty() && IsAllWhitespace(content))
					AddEdit(li, ls, lineEnd, "");
			}

					// Trailing whitespace after the last token: same comment-safety rule.
					if (last)
					{
						size_t trailingLen = lineEnd - last->end;
						if (trailingLen > 0 && IsAllWhitespace(text.substr(last->end, trailingLen)))
							AddEdit(li, last->end, lineEnd, "");
					}

					for (uint32 i = firstIdx; hasToken && i <= lastIdx; ++i)
						depth += Delta(tokens[i]);
				}
			}

			void FixSpacing()
			{
				for (size_t i = 0; i + 1 < tokens.size(); ++i)
				{
					const Tok& a = tokens[i];
					const Tok& b = tokens[i + 1];
					if (IsComment(a.type) || IsComment(b.type))
						continue; // never re-flow into or out of comments

					std::string gap = text.substr(a.end, b.start - a.end);
				if (gap.find('\n') != std::string::npos)
					continue; // gaps that span lines are newline layout, not spacing: never touched
					if (!IsAllWhitespace(gap))
						continue; // defensive: adjacent tokens' gap is whitespace by construction

					std::optional<std::string> desired = DesiredGap(a.type, b.type, i > 0 ? tokens[i - 1].type : TokenType::None);
					if (desired && *desired != gap)
						AddEdit(LineOf(a.end), a.end, b.start, *desired);
				}
			}

			/// nullopt = no rule for this pair: keep the existing gap untouched.
			std::optional<std::string> DesiredGap(TokenType aType, TokenType bType, TokenType prevBeforeA) const
			{
				switch (aType)
				{
			case TokenType::Dot:       case TokenType::DoubleColon:
			case TokenType::Arrow:     return ""; // glue with nothing after '.', '::', '->'
			case TokenType::Comma:     return " ";
			// ';' ends a statement/initializer, so it glues to its left side (handled by the bType rule) but
			// opens the next one on its right - VS house style `for (...; i < n; ...)` / `int a = 0; int b;`.
			case TokenType::SemiColon: if (IsWordLike(bType)) return " "; // no rule after non-word tokens (e.g. an empty statement before ')')

				default: break;
				}
				switch (bType)
				{
				case TokenType::CloseParen: case TokenType::CloseBracket:
				case TokenType::SemiColon:  case TokenType::Dot:        return ""; // no space before ')' ']' ';' '.'
				case TokenType::DoubleColon: case TokenType::Arrow:     case TokenType::Comma: return "";

				default: break;
				}

			if (aType == TokenType::OpenParen || aType == TokenType::OpenBracket)
				return ""; // `f(`, `[0]`, constructor calls and generic params (`Vector<int32>`) stay tight - including `{` right after them
				if (bType == TokenType::OpenParen && !IsControlKeyword(aType))
					return ""; // tighten `f (` to `f(`; control keywords keep their space via the rule below
			if (aType == TokenType::OpenCurl)
				return bType == TokenType::CloseCurl ? "" : " "; // `{ statement` house style, but an EMPTY block stays glued: `{}` not `{ }`

				if (bType == TokenType::OpenParen && IsControlKeyword(aType))
					return " "; // `if (` / `while (` / `for (` / `switch (` keep a space before the paren

				if ((aType == TokenType::CloseParen || aType == TokenType::CloseCurl) && bType == TokenType::OpenCurl)
					return " "; // `) {` and `} {` (anonymous bodies, lambdas) get one space

				if (IsBinaryOp(aType))
					// A binary op gets one space on each side; unary (prev is not a value token, e.g. after '(', ',', '=' or another operator) stays tight.
					return IsWordLike(prevBeforeA) ? " " : "";

				if (IsBinaryOp(bType) && IsWordLike(aType))
					return " "; // one space before the binary op too: `x = 1`, `a + b` (unary ops after '('/','/'=' stay tight via the rule above)

				if (aType == TokenType::Else && bType == TokenType::OpenCurl)
					return " "; // `} else {` house style

				// Between two word-like tokens exactly one space (declarations, control flow, calls).
				if (IsWordLike(aType) && IsWordLike(bType))
					return " ";
				return std::nullopt;
			}

			void AddEdit(uint32 line, size_t startByte, size_t endByte, const std::string& newText)
			{
				if (startByte > endByte || (startByte == endByte && newText.empty()))
					return; // malformed range or a true no-op: never emit either
				size_t ls = lineStarts[line];
				Protocol::TextEdit edit;
				edit.range.start.line = line;
				edit.range.start.character = static_cast<uint32>(startByte - ls);
				edit.range.end.line = line;
				edit.range.end.character = static_cast<uint32>(endByte - ls);
				edit.newText = newText;
				edits.push_back(edit);
			}

		std::string IndentString(uint32 levels) const
		{
			if (!options.insertSpaces)
				return std::string(levels, '\t');
			return std::string(static_cast<size_t>(levels) * static_cast<size_t>(options.tabSize), ' ');
		}

			static bool IsAllWhitespace(const std::string& s)
			{
				for (char c : s)
					if (!std::isspace(static_cast<unsigned char>(c)))
						return false;
				return true;
			}
		};
	} // namespace

	std::vector<Protocol::TextEdit> Formatting::Format(const std::string& text, const Options& options)
	{
		Formatter formatter(text, options);
		formatter.Run();
		return std::move(formatter.edits);
	}
} // namespace C4LSP::Features
