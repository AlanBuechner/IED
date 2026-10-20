#include "Macro/MacroExpression.h"
#include "Macro/MacroText.h"
#include "Lexer/LiteralScanner.h"
#include <charconv>
#include <cstdint>
#include <vector>

namespace Scripting
{
	using namespace MacroText;

	namespace
	{
		struct ConditionToken
		{
			enum class Kind { Number, Operator, End } kind = Kind::End;
			int64 value = 0;
			std::string text;
		};

		// `defined X` and `defined(X)` become 1 or 0 before the macros expand (C's order).
		std::string ReplaceDefined(const std::string& condition, const MacroTable& table, std::string& error)
		{
			std::string out;
			for (size_t i = 0; i < condition.size();)
			{
				if (!IsIdentStart(condition[i])) { out += condition[i++]; continue; }
				const size_t end = IdentifierEnd(condition, i);
				if (condition.compare(i, end - i, "defined") != 0) { out.append(condition, i, end - i); i = end; continue; }
				size_t name = SkipSpaces(condition, end);
				const bool parenthesized = name < condition.size() && condition[name] == '(';
				if (parenthesized) name = SkipSpaces(condition, name + 1);
				const size_t nameEnd = IdentifierEnd(condition, name);
				size_t after = nameEnd;
				if (parenthesized)
				{
					after = SkipSpaces(condition, nameEnd);
					if (after >= condition.size() || condition[after] != ')') { error = "'defined(' needs a macro name and a ')'"; return ""; }
					++after;
				}
				if (nameEnd == name) { error = "'defined' needs a macro name"; return ""; }
				const std::string macro = condition.substr(name, nameEnd - name);
				out += table.Has(macro) || macro == "__FILE__" || macro == "__LINE__" ? " 1 " : " 0 ";
				i = after;
			}
			return out;
		}

		std::string NumberValue(const std::string& text, int64& value)
		{
			const ScannedLiteral literal = ScanNumber(text);
			if (literal.facts.issue != LiteralIssue::None)
			{
				Token token;
				token.SetText(literal.text);
				token.SetLiteralFacts(literal.facts);
				return LiteralIssueMessage(token);
			}
			const NumberSpelling spelling = SplitNumber(text);
			if (spelling.isFloat) return "a #if condition takes integers, but '" + text + "' is a floating-point number";
			uint64 bits = 0;
			const auto parsed = std::from_chars(spelling.digits.data(), spelling.digits.data() + spelling.digits.size(), bits, static_cast<int>(spelling.radix));
			if (parsed.ec != std::errc()) return "integer literal '" + text + "' is too large";
			value = static_cast<int64>(bits);
			return "";
		}

		std::string CharacterNumber(const std::string& text, int64& value)
		{
			const ScannedLiteral literal = ScanQuoted(text, QuotedLiteralPrefix(text));
			if (literal.facts.issue != LiteralIssue::None)
			{
				Token token;
				token.SetText(literal.text);
				token.SetLiteralFacts(literal.facts);
				return LiteralIssueMessage(token);
			}
			const uint32 unit = CodeUnitCount(literal.text, literal.facts.unitWidth) ? CodeUnitAt(literal.text, literal.facts.unitWidth, 0) : 0;
			value = literal.facts.unitWidth == 1 ? static_cast<int8>(unit) : static_cast<int64>(unit);
			return "";
		}

		const char* const kOperators[] = { "<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
			"+", "-", "*", "/", "%", "<", ">", "&", "|", "^", "!", "~", "?", ":", "(", ")" };

		std::vector<ConditionToken> Tokenize(const std::string& text, std::string& error)
		{
			using Kind = ConditionToken::Kind;
			std::vector<ConditionToken> tokens;
			for (size_t i = 0; i < text.size() && error.empty();)
			{
				if (IsSpace(text[i])) { ++i; continue; }
				if (StartsComment(text, i)) { i = CommentEnd(text, i); continue; }
				ConditionToken token;
				size_t end = i + 1;
				if (text[i] >= '0' && text[i] <= '9')
				{
					end = ScanNumber(std::string_view(text).substr(i)).length + i;
					token.kind = Kind::Number;
					error = NumberValue(text.substr(i, end - i), token.value);
				}
				else if (StartsLiteral(text, i))
				{
					end = LiteralEnd(text, i);
					token.kind = Kind::Number;
					error = CharacterNumber(text.substr(i, end - i), token.value);
				}
				else if (IsIdentStart(text[i]))
				{
					end = IdentifierEnd(text, i);
					token.kind = Kind::Number;
					token.value = text.compare(i, end - i, "true") == 0 ? 1 : 0;
				}
				else
				{
					token.kind = Kind::Operator;
					for (const char* op : kOperators)
						if (text.compare(i, std::char_traits<char>::length(op), op) == 0) { token.text = op; break; }
					if (token.text.empty()) { error = "unexpected '" + std::string(1, text[i]) + "' in a #if condition"; break; }
					end = i + token.text.size();
				}
				if (token.text.empty()) token.text = text.substr(i, end - i);
				tokens.push_back(token);
				i = end;
			}
			tokens.push_back({});
			return tokens;
		}

		// C's binary operators, loosest first; `?:` sits below them all, right associative.
		uint32 BinaryPower(const std::string& op)
		{
			static const std::pair<const char*, uint32> kPowers[] = {
				{ "||", 10 }, { "&&", 20 }, { "|", 30 }, { "^", 40 }, { "&", 50 }, { "==", 60 }, { "!=", 60 },
				{ "<", 70 }, { "<=", 70 }, { ">", 70 }, { ">=", 70 }, { "<<", 80 }, { ">>", 80 },
				{ "+", 90 }, { "-", 90 }, { "*", 100 }, { "/", 100 }, { "%", 100 } };
			for (const auto& entry : kPowers)
				if (op == entry.first) return entry.second;
			return 0;
		}

		constexpr uint32 kConditionalPower = 5;

		class ConditionParser
		{
		public:
			explicit ConditionParser(std::vector<ConditionToken> tokens) : m_Tokens(std::move(tokens)) {}

			ConditionValue Run()
			{
				ConditionValue result;
				if (Peek().kind == ConditionToken::Kind::End) { result.error = "a #if condition is empty"; return result; }
				result.value = Expression(0, true);
				if (m_Error.empty() && Peek().kind != ConditionToken::Kind::End) Fail("unexpected '" + Peek().text + "' in a #if condition");
				result.error = m_Error;
				return result;
			}

		private:
			const ConditionToken& Peek() const { return m_Tokens[m_Position]; }
			const ConditionToken& Next() { return m_Tokens[m_Position < m_Tokens.size() - 1 ? m_Position++ : m_Position]; }
			bool At(const char* op) const { return Peek().kind == ConditionToken::Kind::Operator && Peek().text == op; }
			void Fail(const std::string& message) { if (m_Error.empty()) m_Error = message; }

			int64 Expression(uint32 minPower, bool live)
			{
				int64 left = Unary(live);
				while (m_Error.empty() && Peek().kind == ConditionToken::Kind::Operator)
				{
					if (At("?") && minPower <= kConditionalPower)
					{
						Next();
						const int64 then = Expression(0, live && left != 0);
						if (!At(":")) { Fail("a '?' in a #if condition needs its ':'"); return 0; }
						Next();
						const int64 otherwise = Expression(kConditionalPower, live && left == 0);
						left = left != 0 ? then : otherwise;
						continue;
					}
					const std::string op = Peek().text;
					const uint32 power = BinaryPower(op);
					if (power == 0 || power < minPower) break;
					Next();
					const bool rightLive = op == "&&" ? live && left != 0 : op == "||" ? live && left == 0 : live;
					const int64 right = Expression(power + 1, rightLive);
					left = Binary(op, left, right, live);
				}
				return left;
			}

			int64 Unary(bool live)
			{
				const ConditionToken& token = Next();
				if (token.kind == ConditionToken::Kind::Number) return token.value;
				if (token.kind == ConditionToken::Kind::End) { Fail("a #if condition ends early: an operand is missing"); return 0; }
				if (token.text == "(")
				{
					const int64 inner = Expression(0, live);
					if (!At(")")) { Fail("a '(' in a #if condition has no ')'"); return 0; }
					Next();
					return inner;
				}
				const std::string op = token.text;
				if (op != "!" && op != "~" && op != "-" && op != "+")
				{
					Fail("unexpected '" + op + "' in a #if condition");
					return 0;
				}
				const int64 operand = Unary(live);
				if (op == "!") return operand == 0 ? 1 : 0;
				if (op == "~") return ~operand;
				if (op == "-") return static_cast<int64>(0 - static_cast<uint64>(operand));
				return operand;
			}

			int64 Binary(const std::string& op, int64 a, int64 b, bool live)
			{
				const uint64 ua = static_cast<uint64>(a), ub = static_cast<uint64>(b);
				if ((op == "/" || op == "%") && b == 0)
				{
					if (live) Fail(op == "/" ? "division by zero in a #if condition" : "remainder by zero in a #if condition");
					return 0;
				}
				if ((op == "<<" || op == ">>") && (b < 0 || b > 63))
				{
					if (live) Fail("the shift count " + std::to_string(b) + " in a #if condition is out of range 0..63");
					return 0;
				}
				if (op == "+") return static_cast<int64>(ua + ub);
				if (op == "-") return static_cast<int64>(ua - ub);
				if (op == "*") return static_cast<int64>(ua * ub);
				if (op == "/") return (a == INT64_MIN && b == -1) ? a : a / b;
				if (op == "%") return (a == INT64_MIN && b == -1) ? 0 : a % b;
				if (op == "<<") return static_cast<int64>(ua << b);
				if (op == ">>") return a >> b;
				if (op == "<") return a < b;
				if (op == "<=") return a <= b;
				if (op == ">") return a > b;
				if (op == ">=") return a >= b;
				if (op == "==") return a == b;
				if (op == "!=") return a != b;
				if (op == "&") return a & b;
				if (op == "^") return a ^ b;
				if (op == "|") return a | b;
				if (op == "&&") return a != 0 && b != 0;
				return a != 0 || b != 0;
			}

			std::vector<ConditionToken> m_Tokens;
			size_t m_Position = 0;
			std::string m_Error;
		};
	}

	ConditionValue EvaluateCondition(const std::string& condition, const MacroTable& table, const ExpansionSite& site)
	{
		ConditionValue result;
		const std::string withDefined = ReplaceDefined(condition, table, result.error);
		if (!result.Ok()) return result;
		const ExpansionResult expanded = MacroExpander::Expand(withDefined, table, site);
		if (!expanded.errors.empty()) { result.error = expanded.errors.front(); return result; }
		std::vector<ConditionToken> tokens = Tokenize(expanded.text, result.error);
		if (!result.Ok()) return result;
		return ConditionParser(std::move(tokens)).Run();
	}
}
