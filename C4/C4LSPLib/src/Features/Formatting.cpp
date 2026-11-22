#include "Features/Formatting.h"
#include "Protocol/LspTypes.h"
#include "Lexer/Lexer.h"
#include "SourceManager/Source.h"
#include <algorithm>
#include <cctype>
#include <map>
#include <optional>
#include <set>
#include <string_view>
#include <vector>

namespace C4LSP::Features
{
	namespace
	{
		using C4Compiler::TokenType;

		struct Tok
		{
			TokenType type = TokenType::None;
			size_t start = 0; // byte offset of first character in the document
			size_t end = 0;   // one past last character (filled after the full pass)
		};

		bool IsWordLike(TokenType t)
		{
			return t == TokenType::Identifier || t == TokenType::Number || t == TokenType::Char || t == TokenType::Text ||
				C4Compiler::Token::IsKeyword(t);
		}

		bool IsControlKeyword(TokenType t)
		{
			switch (t)
			{
			case TokenType::If: case TokenType::For: case TokenType::While: case TokenType::Switch: case TokenType::Catch:
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

		bool IsSign(TokenType t) { return t == TokenType::Plus || t == TokenType::Minus; }

		// The last token of an operand: an operator after it is binary (`f(a) && b`, `x[0] = 1`), after anything
		// else (`(`, `=`, `return`) it is unary and stays tight.
		bool IsOperandEnd(TokenType t)
		{
			switch (t)
			{
			case TokenType::Identifier: case TokenType::Number: case TokenType::Char: case TokenType::Text:
			case TokenType::True: case TokenType::False: case TokenType::Null: case TokenType::This:
			case TokenType::CloseParen: case TokenType::CloseBracket:
				return true;
			default:
				return false;
			}
		}

		// What a `(` right after it calls or constructs, so the two stay tight: `f(`, `a[0](`, `g()(`, `Make<T>(`.
		// After an operator or another keyword (`= (`, `return (`) the `(` only groups.
		bool IsCallee(TokenType t)
		{
			return t == TokenType::Identifier || t == TokenType::CloseParen || t == TokenType::CloseBracket ||
				t == TokenType::Greater || t == TokenType::This;
		}

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

		// Whether a line ending in `t` has ended its statement or opened a block, a list item or an annotation: the next line
		// starts afresh. Any other ending (an operator, `?`, a string literal, a name) is continued by the next line.
		bool EndsStatement(TokenType t)
		{
			return t == TokenType::SemiColon || t == TokenType::OpenCurl || t == TokenType::CloseCurl || t == TokenType::Comma ||
				t == TokenType::Colon || t == TokenType::CloseBracket || t == TokenType::Greater || t == TokenType::None;
		}

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
			auto source = C4Compiler::Source::FromMemory(text);
			C4Compiler::Lexer lexer(*source.Get(), /*sourceId=*/0); // formatter never resolves against a SourceManager

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
				const C4Compiler::Token& tok = lexer.NextToken();
				if (tok.GetType() == TokenType::EndOfFile)
					break;

				Tok t;
				t.type = tok.GetType();
				t.start = tok.GetByteOffset();
				t.end = tok.GetEndLocation().byteOffset; // the exact source extent (a literal's text is its decoded value)

				C4Compiler::Token combined = tok; // running copy of what has been consumed so far (atomic ops are one char each)
				if (C4Compiler::IsAtomicOperator(t.type))
					while (true)
					{
						const C4Compiler::Token& next = lexer.Peek(0);
						if (!C4Compiler::IsAtomicOperator(next.GetType()) || next.GetByteOffset() != t.end)
							break; // a compound operator is adjacent characters, as the compiler lexes it
						C4Compiler::Token attempt;
						if (combined.CombineWith(next.GetText()[0], attempt) != C4Compiler::Token::TokenAppendMethod::Combine)
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

			// One indentation level per line that leaves openers unclosed, however many it leaves: `Run([&]() {` indents the
			// lambda's body once, and `});` comes back once - so does a line that starts by closing only some of them
			// (`}, problem);`, `}) && more) {`, BUG-418). `bonus` is what a level's opening line had (below).
			struct Level
			{
				int open = 0;
				int bonus = 0;
				bool brace = false; // its innermost opener is a `{`: statements, not an argument list or an index
			};

			// Leading whitespace and trailing whitespace of every line. A brace-less control header (`if (x)`, `for (...)`,
			// `else`) indents the next line once more - its bonus - and a `{` alone on the next line is the header's body, at
			// the header's indentation. A line that continues a statement among statements (`? a` / `: b`, a string literal
			// after another) is indented once more than the statement; inside `(` or `[` the level already indents it.
			void FixIndentationAndTrailing()
			{
				std::vector<Level> levels;
				std::optional<std::pair<size_t, int>> oneLineIf; // the line before: a whole `if (...) statement;`, its depth and bonus
				int pending = 0;        // the next line's bonus
				bool afterHeader = false;
				TokenType previousEnd = TokenType::None; // the last token of the code line before, None after a directive
				size_t cursor = 0;      // tokens are in source order: each line's are found by walking on
				for (uint32 li = 0; li < lineStarts.size(); ++li)
				{
					size_t ls = lineStarts[li];
					size_t nl = text.find('\n', ls);
					size_t lineEnd = (nl == std::string::npos) ? text.size() : nl;
					if (lineEnd > ls && text[lineEnd - 1] == '\r')
						--lineEnd; // a "\r\n" break is both bytes, not trailing whitespace: stripping it mixed a file's breaks (BUG-426)

					size_t firstIdx = 0, lastIdx = 0; bool hasToken = false;
					while (cursor < tokens.size() && tokens[cursor].start < lineEnd + (nl == std::string::npos ? 1 : 0))
					{
						if (tokens[cursor].start >= ls && !IsComment(tokens[cursor].type))
						{ if (!hasToken) { firstIdx = cursor; hasToken = true; } lastIdx = cursor; }
						++cursor;
					}

					if (!hasToken)
					{
						// No non-comment token: collapse stray indentation on a blank line; a comment-only line is left alone.
						std::string content = text.substr(ls, lineEnd - ls);
						if (!content.empty() && IsAllWhitespace(content))
							AddEdit(li, ls, lineEnd, "");
						continue;
					}
					const Tok& first = tokens[firstIdx];
					const Tok& last = tokens[lastIdx];

					// Where the line's leading closers return to, and the bonus of the level they close.
					std::vector<Level> after = levels;
					int bonus = (first.type == TokenType::OpenCurl && afterHeader && pending > 0) ? pending - 1 : pending;
					bool partlyClosed = false; // the last leading closer left its level open
					for (size_t i = firstIdx; i <= lastIdx && IsCloser(tokens[i].type) && !after.empty(); ++i)
					{
						partlyClosed = --after.back().open > 0;
						if (!partlyClosed)
						{
							bonus = after.back().bonus;
							after.pop_back();
						}
					}
					if (partlyClosed) // indented as its level's opening line; the level stays open for the lines after it
					{
						bonus = after.back().bonus;
						after.pop_back();
					}
					// An `else` right after a whole one-line `if (...) statement;` stands where that `if` stood, though a brace-less
					// header put the `if` deeper than the header (`while (x)` / `if (y) return;` / `else Wait();`).
					if (first.type == TokenType::Else && oneLineIf && oneLineIf->first == after.size()) bonus = oneLineIf->second;
					const bool startsWithIf = first.type == TokenType::If ||
						(first.type == TokenType::Else && firstIdx < lastIdx && tokens[firstIdx + 1].type == TokenType::If);
					const size_t depth = after.size();
					const int lineStartBonus = bonus;

					// Leading whitespace: only ever touched when it is pure whitespace, so a line
					// whose content starts inside a multi-line comment (`... */ int x`) is left alone; a directive keeps
					// the indentation it was written with (`#if` at column 0 in a function body is the code base's way).
					const bool directive = text[first.start] == '#' && !DirectiveOf(first).empty();
					// `include "x.ch"` and `extern "c" include "x.h"` end without a semicolon: whole lines of their own.
					const bool includes = first.type == TokenType::Extern ||
						(first.type == TokenType::Identifier && std::string_view(text).substr(first.start, first.end - first.start) == "include");
					const bool amongStatements = after.empty() || after.back().brace;
					const bool continues = !directive && !includes && !afterHeader && amongStatements && !EndsStatement(previousEnd) &&
						!IsCloser(first.type) && first.type != TokenType::OpenCurl;
					if (!directive && IsAllWhitespace(text.substr(ls, first.start - ls)))
					{
						uint32 target = static_cast<uint32>(bonus) + (continues ? 1u : 0u);
						for (const Level& level : after)
							target += 1 + static_cast<uint32>(level.bonus);
						if (target > 0 && (IsAccessLabel(firstIdx, lastIdx) || AnnotatesAccessLabel(firstIdx)))
							--target;
						std::string want = IndentString(target);
						if (text.substr(ls, first.start - ls) != want)
							AddEdit(li, ls, first.start, want);
					}

					// Trailing whitespace after the last token: same comment-safety rule.
					size_t trailingLen = lineEnd - last.end;
					if (last.end < lineEnd && IsAllWhitespace(text.substr(last.end, trailingLen)))
						AddEdit(li, last.end, lineEnd, "");

					int opened = 0;
					std::vector<TokenType> openers; // this line's openers still open, innermost last
					for (size_t i = firstIdx; i <= lastIdx; ++i)
					{
						int delta = Delta(tokens[i]);
						if (delta > 0)
						{
							++opened;
							openers.push_back(tokens[i].type);
						}
						else if (delta < 0 && opened > 0)
						{
							--opened;
							openers.pop_back();
						}
						else if (delta < 0 && !levels.empty() && --levels.back().open == 0)
							levels.pop_back();
					}
					// A continuation line's own opener indents what it opens from the continuation, not from the statement.
					const int lineBonus = bonus + (continues ? 1 : 0);
					if (opened > 0)
						levels.push_back({ opened, lineBonus, openers.back() == TokenType::OpenCurl });
					afterHeader = opened == 0 && EndsWithControlHeader(lastIdx);
					oneLineIf.reset();
					if (startsWithIf && opened == 0 && levels.size() == depth && !afterHeader && EndsStatement(tokens[lastIdx].type))
						oneLineIf = std::make_pair(depth, lineStartBonus);
					pending = afterHeader ? bonus + 1 : 0;
					previousEnd = directive || includes ? TokenType::None : tokens[lastIdx].type;
				}
			}

			// Whether the line ending at token `lastIdx` is a control header whose body is on the next line: it ends with
			// `else`, `do`, or the `)` of `if (`, `for (` or `while (` (a condition may span lines).
			bool EndsWithControlHeader(size_t lastIdx) const
			{
				TokenType type = tokens[lastIdx].type;
				if (type == TokenType::Else || type == TokenType::Do)
					return true;
				return ClosesControlHeader(lastIdx);
			}

			// Whether token `closeIdx` is the `)` that ends the condition of an `if (`, `for (` or `while (`: what follows
			// it is the body, never a call of it (`if (x) (*p)->Run();`).
			bool ClosesControlHeader(size_t closeIdx) const
			{
				if (tokens[closeIdx].type != TokenType::CloseParen)
					return false;
				int balance = 0;
				for (size_t j = closeIdx + 1; j-- > 0;)
				{
					if (tokens[j].type == TokenType::CloseParen)
						++balance;
					else if (tokens[j].type == TokenType::OpenParen && --balance == 0)
					{
						TokenType before = j > 0 ? tokens[j - 1].type : TokenType::None;
						return before == TokenType::If || before == TokenType::For || before == TokenType::While;
					}
				}
				return false;
			}

			// `public:`, `private:`, `protected:` sit at their class's indentation, one level out from the members.
			// A C#-style modifier (`public void F();`) is a member and stays with the others.
			bool IsAccessLabel(size_t firstIdx, size_t lastIdx) const
			{
				TokenType type = tokens[firstIdx].type;
				bool access = type == TokenType::Public || type == TokenType::Private || type == TokenType::Protected;
				return access && firstIdx < lastIdx && tokens[firstIdx + 1].type == TokenType::Colon;
			}

			// A line of annotations a label takes (`[Property]` above `private:`, or `[Saved] private:`): at the label's
			// indentation, since they are its section's, not the next member's.
			bool AnnotatesAccessLabel(size_t firstIdx) const
			{
				size_t at = firstIdx;
				while (at < tokens.size() && (tokens[at].type == TokenType::OpenBracket || IsComment(tokens[at].type)))
				{
					if (IsComment(tokens[at].type)) { ++at; continue; }
					at = PastBrackets(at);
				}
				if (at == firstIdx || at + 1 >= tokens.size()) return false;
				TokenType type = tokens[at].type;
				return (type == TokenType::Public || type == TokenType::Private || type == TokenType::Protected) &&
					tokens[at + 1].type == TokenType::Colon;
			}

			// The token after the `]` that closes the `[` at `openIdx`.
			size_t PastBrackets(size_t openIdx) const
			{
				int depth = 0;
				for (size_t i = openIdx; i < tokens.size(); ++i)
				{
					if (tokens[i].type == TokenType::OpenBracket) ++depth;
					else if (tokens[i].type == TokenType::CloseBracket && --depth == 0) return i + 1;
				}
				return tokens.size();
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
					const std::string_view directive = DirectiveOf(a);
					if (directive == "error" || directive == "warning")
						continue; // the message is text, not code: `only (see docs/x.md)` stays as written
					if (b.type == TokenType::OpenParen && directive == "define")
						continue; // `#define X (y)` is a constant and `#define X(y)` takes a parameter: this gap is the program's

					// A `(` after a control header's `)` starts its body: spaced, not glued as a call would be.
					bool bodyAfterHeader = b.type == TokenType::OpenParen && ClosesControlHeader(i);
					// `a > (b)` compares: a template's `>` is never spaced from what it closes (`Make<T>(`), a comparison's is.
					bool comparedParen = a.type == TokenType::Greater && b.type == TokenType::OpenParen && i > 0 && tokens[i - 1].end < a.start;
					bool castSign = (IsSign(b.type) && ClosesBuiltInCast(i)) || (IsSign(a.type) && i > 0 && ClosesBuiltInCast(i - 1));
					std::optional<std::string> desired = castSign
						? std::optional<std::string>("")
						: IsReturnTypeArrow(i) || IsReturnTypeArrow(i + 1) || bodyAfterHeader || comparedParen
						? std::optional<std::string>(" ")
						: DesiredGap(i);
					if (desired && *desired != gap)
						AddEdit(LineOf(a.end), a.end, b.start, *desired);
				}
			}

			// The directive of the token's line - `define` of `#define X 1`, `error` of `# error ...` - or empty when the line is no
			// directive.
			std::string_view DirectiveOf(const Tok& token) const
			{
				size_t at = lineStarts[LineOf(token.start)];
				while (at < token.start && (text[at] == ' ' || text[at] == '\t')) ++at;
				if (text[at] != '#') return {};   // the line's first token is the `#` itself (`at` is the token) or comes before it
				++at;
				while (at < text.size() && (text[at] == ' ' || text[at] == '\t')) ++at;
				size_t end = at;
				while (end < text.size() && (std::isalnum(static_cast<unsigned char>(text[end])) || text[end] == '_')) ++end;
				return std::string_view(text).substr(at, end - at);
			}

			// `(int64)-1`: the `)` of a cast to a built-in type, after which a sign is unary. `(a) - 1` stays binary: a name in
			// parentheses may be a value, a built-in type never is.
			bool ClosesBuiltInCast(size_t i) const
			{
				if (i < 2 || tokens[i].type != TokenType::CloseParen || tokens[i - 2].type != TokenType::OpenParen)
					return false;
				static const std::set<std::string> builtIn = { "int", "uint", "char", "bool", "float", "double", "int8", "int16", "int32", "int64", "uint8", "uint16", "uint32", "uint64", "char8", "char16", "char32" };
				return builtIn.count(text.substr(tokens[i - 1].start, tokens[i - 1].end - tokens[i - 1].start)) > 0;
			}

			// `) -> T {`: a lambda's return type, spaced on both sides. Member access never reaches a `{` through
			// type tokens alone (`f()->x) {` meets the `)` first), so it stays glued.
			bool IsReturnTypeArrow(size_t i) const
			{
				if (tokens[i].type != TokenType::Arrow || i == 0 || tokens[i - 1].type != TokenType::CloseParen)
					return false;
				for (size_t j = i + 1; j < tokens.size(); ++j)
				{
					switch (tokens[j].type)
					{
					case TokenType::OpenCurl: return j > i + 1;
					case TokenType::Identifier: case TokenType::DoubleColon: case TokenType::Star: case TokenType::Ampersand:
					case TokenType::Less: case TokenType::Greater: case TokenType::Comma: case TokenType::Const:
						continue;
					default:
						return false;
					}
				}
				return false;
			}

			// Whether token `at` ends an operand, so an operator after it is binary: a name, a literal, a `)` or `]`, or the `}` of a
			// value braced after its type (`Color{ x } == c`, `Vector<T>{ 1 } != v`) - not a block's (`{ b(); } -x;`) (BUG-458).
			bool EndsOperand(size_t at) const
			{
				if (IsOperandEnd(tokens[at].type))
					return true;
				if (tokens[at].type != TokenType::CloseCurl)
					return false;
				int depth = 0;
				for (size_t j = at + 1; j-- > 0;)
				{
					if (tokens[j].type == TokenType::CloseCurl)
						++depth;
					else if (tokens[j].type == TokenType::OpenCurl && --depth == 0)
						return j > 0 && (tokens[j - 1].type == TokenType::Identifier || tokens[j - 1].type == TokenType::Greater);
				}
				return false;
			}

			/// The gap between tokens `i` and `i + 1`; nullopt = no rule for this pair: keep the existing gap untouched.
			std::optional<std::string> DesiredGap(size_t i) const
			{
				const TokenType aType = tokens[i].type;
				const TokenType bType = tokens[i + 1].type;
				switch (aType)
				{
				case TokenType::Dot:       case TokenType::DoubleColon:
				case TokenType::Arrow:     return ""; // glue with nothing after '.', '::', '->'
				case TokenType::Comma:     return " ";
				// ';' ends a statement/initializer, so it glues to its left side (handled by the bType rule) but
				// opens the next one on its right - VS house style `for (...; i < n; ...)` / `int a = 0; int b;`.
				case TokenType::SemiColon: if (IsWordLike(bType)) return " "; // no rule after non-word tokens (e.g. an empty statement before ')')
					break;
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
				if (bType == TokenType::OpenParen && IsCallee(aType))
					return ""; // tighten `f (` to `f(`; after operators and keywords the rules below decide
				// `{{` keeps its gaps as written, and so does the `{` glued after another: a template's `{{Module}}` in a file a tool
				// fills in is no code, and nested braces written tight (`{{1, 2}}`) stay so, as `}}` always did (BUG-459).
				if (aType == TokenType::OpenCurl && (bType == TokenType::OpenCurl || (i > 0 && tokens[i - 1].type == TokenType::OpenCurl && tokens[i - 1].end == tokens[i].start)))
					return std::nullopt;
				if (aType == TokenType::OpenCurl)
					return bType == TokenType::CloseCurl ? "" : " "; // `{ statement` house style, but an EMPTY block stays glued: `{}` not `{ }`

				if (bType == TokenType::OpenParen && IsControlKeyword(aType))
					return " "; // `if (` / `while (` / `for (` / `switch (` keep a space before the paren

				if ((aType == TokenType::CloseParen || aType == TokenType::CloseCurl) && bType == TokenType::OpenCurl)
					return " "; // `) {` and `} {` (anonymous bodies, lambdas) get one space

				if (IsBinaryOp(aType))
					// A binary op gets one space on each side; unary (prev ends no operand, e.g. '(', ',', '=', `return` or another operator) stays tight.
					return i > 0 && EndsOperand(i - 1) ? " " : "";

				if (IsBinaryOp(bType) && EndsOperand(i))
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
