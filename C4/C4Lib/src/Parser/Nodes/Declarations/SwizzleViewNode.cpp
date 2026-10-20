#include "SwizzleViewNode.h"

namespace Scripting
{
	Core::String SwizzleViewNode::ToString() const
	{
		Core::String result = (m_Packed ? "packed " : "unpacked ");
		if (m_ElementType) result += m_ElementType->ToString();
		for (const auto& lane : m_Lanes)
			result += Core::String(" ") + lane.GetText();
		return result;
	}

	std::span<const Ref<AstNode>> SwizzleViewNode::Children() const { return {}; }

	Ref<SwizzleViewNode> SwizzleViewNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		Token first = lexer.PeekCompound();
		bool isPacked = (first.GetType() == TokenType::Identifier && first.GetText() == "packed");
		bool isUnpacked = (first.GetType() == TokenType::Identifier && first.GetText() == "unpacked");
		if (!isPacked && !isUnpacked) return nullptr;

		auto node = CreateRef<SwizzleViewNode>();
		node->m_Packed = isPacked;
		node->startLocation = first.GetStartLocation();
		lexer.NextCompoundToken();

		// Element type (a scalar for named-lane SIMD views).
		Token typeTok = lexer.PeekCompound();
		if (typeTok.GetType() != TokenType::Identifier)
		{
			errorTracker.AddError(
				typeTok.GetStartLocation(),
				Core::StringFormat::Format("Expected an element type after '{0}', got '{1}'",
					isPacked ? "packed" : "unpacked", typeTok.GetText()),
				ErrorSeverity::Error);
			return node;
		}
		node->m_ElementType = CreateRef<QualTypeNode>();
		node->m_ElementType->ParseBody(state);

		// Lane names: ident (',' ident)* ';'
		Token name = lexer.PeekCompound();
		if (name.GetType() != TokenType::Identifier)
		{
			errorTracker.AddError(
				name.GetStartLocation(),
				Core::StringFormat::Format("Expected a lane name after '{0} {1}', got '{2}'",
					isPacked ? "packed" : "unpacked", node->m_ElementType->ToString(), name.GetText()),
				ErrorSeverity::Error);
			return node;
		}
		node->m_Lanes.Push(name);
		lexer.NextCompoundToken();
		auto checkLane = [&](const Token& lane) {
			// D-37: `v.xy` reads lanes x and y, so a lane name is one character. An array view (`packed T[N] data`)
			// names the whole region instead.
			if (lane.GetText().size() != 1 && !node->m_ElementType->HasArrayLayer())
				errorTracker.AddError(lane.GetStartLocation(), "a lane name is one character, so 'v." + lane.GetText() +
					"' would read lanes: rename '" + lane.GetText() + "'", ErrorSeverity::Error);
		};
		checkLane(name);
		while (lexer.PeekCompound().GetType() == TokenType::Comma)
		{
			lexer.NextCompoundToken();
			Token n2 = lexer.PeekCompound();
			if (n2.GetType() != TokenType::Identifier)
			{
				errorTracker.AddError(
					n2.GetStartLocation(),
					Core::StringFormat::Format("Expected a lane name in view, got '{0}'", n2.GetText()),
					ErrorSeverity::Error);
				break;
			}
			node->m_Lanes.Push(n2);
			checkLane(n2);
			lexer.NextCompoundToken();
		}

		Token semi = lexer.PeekCompound();
		if (semi.GetType() == TokenType::SemiColon)
		{
			node->endLocation = semi.GetEndLocation();
			lexer.NextCompoundToken();
		}
		else if (semi.GetType() != TokenType::EndOfFile)
		{
			errorTracker.AddError(
				semi.GetStartLocation(),
				Core::StringFormat::Format("Expected ';' to end swizzle view, got '{0}'", semi.GetText()),
				ErrorSeverity::Error);
		}

		return node;
	}
} // namespace Scripting
