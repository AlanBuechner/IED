#include "QualTypeNode.h"
#include "../Declarations/NamespaceQualifiedNameNode.h"
#include "../Declarations/FunPtrTypeNode.h"
#include "../Expressions/LiteralNode.h"
#include "Parser/PrattParser.h"
#include "Core/StringFormat.h"

namespace Scripting
{
	// The array size in type position (int32[N]) is a full constant expression: a literal,
	// a const var / enum member name, or an expression over them (W+10). Semantic folds it via
	// EvaluateConstant; the parser must not restrict it to a single token.
	static Ref<AstNode> ParseArraySizeExpression(ParserState& state)
	{
		return PrattParser::ParseExpression(state);
	}

	static bool IsIndirectionOperator(TokenType type)
	{
		// `*` is the only pointer indirection: every C4 pointer is GC-managed. There is no
		// separate managed-pointer syntax (the old `^` was removed). `**` and deeper runs are
		// lexed as a DoubleStar compound token; we accept it here and expand it into multiple
		// Star layers in ParseBody so nested pointers (void**, int32***, ...) work.
		return type == TokenType::Star ||
			type == TokenType::DoubleStar ||
			type == TokenType::Ampersand || type == TokenType::OpenBracket;
	}

	Core::String QualTypeNode::ToString() const
	{
		Core::String result;
		if (IsReturnList())
		{
			result = "[";
			for (uint32 i = 0; i < m_ReturnList.Count(); ++i)
				result += (i ? ", " : "") + m_ReturnList[i]->ToString() + (m_ReturnList[i]->m_ExpandsPack ? "..." : "");
			return result + "]";
		}

		if (m_IsLeadingConst)
			result += "const ";
		if (m_IsPacked)
			result += "packed ";

		if (m_DelegateType)
			result += m_DelegateType->ToString();
		else if (m_BaseName)
			result += m_BaseName->ToString();

		for (uint32 i = 0; i < m_Indirections.Count(); ++i)
		{
			const auto& layer = m_Indirections[i];

			if (layer.isConst)
				result += " const";

			switch (layer.indirectionToken)
			{
			case TokenType::Star: result += "*"; break;
			case TokenType::Ampersand: result += "&"; break;
			case TokenType::OpenBracket:
				result += "[";
				if (layer.arraySizeExpr)
					result += layer.arraySizeExpr->ToString();
				result += "]";
				break;
			default: break;
			}
		}

		return result;
	}

	bool QualTypeNode::HasArrayLayer() const
	{
		for (const IndirectionLayer& layer : m_Indirections)
			if (layer.indirectionToken == TokenType::OpenBracket) return true;
		return false;
	}

	Ref<QualTypeNode> QualTypeNode::TryParse(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		Token first = lexer.PeekCompound();

		if (first.GetType() != TokenType::Const &&
			first.GetType() != TokenType::Identifier &&
			first.GetType() != TokenType::FunPtr)
		{
			return nullptr;
		}

		auto undo = LexerUndoScope(lexer);
		state.GetErrorTracker().SuppressErrors();

		auto node = CreateRef<QualTypeNode>();
		node->ParseBody(state);

		bool hasBaseName = node->m_BaseName && !node->m_BaseName->GetSegments().Empty();
		if (hasBaseName)
		{
			auto& firstSegment = node->m_BaseName->GetSegments()[0];
		if (firstSegment.nameToken.GetType() != TokenType::Identifier &&
			firstSegment.nameToken.GetType() != TokenType::FunPtr)
		{
			state.GetErrorTracker().DiscardSuppressedErrors();
			return nullptr;
		}
	}

	if (!hasBaseName && !node->m_DelegateType)
	{
		state.GetErrorTracker().DiscardSuppressedErrors();
		return nullptr;
	}

	state.GetErrorTracker().CommitSuppressedErrors();
		undo.Commit();
		return node;
	}

	void QualTypeNode::ParseBody(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		ErrorTracker& errorTracker = state.GetErrorTracker();

		m_IsLeadingConst = false;
		m_IsPacked = false;
		m_BaseName = nullptr;
		m_DelegateType = nullptr;
		m_Indirections.Clear();

		Token first = lexer.PeekCompound();
		if (first.GetType() == TokenType::Const)
		{
			m_IsLeadingConst = true;
			m_LeadingConstToken = first;
			if (startLocation.line == 0) startLocation = first.GetStartLocation(); // the type starts at its const (BUG-241)
			lexer.NextCompoundToken();
		}
		// `packed T[N]`: a contextual keyword before an element type name.
		if (Token packed = lexer.PeekCompound(); packed.GetType() == TokenType::Identifier && packed.GetText() == "packed" &&
			lexer.PeekCompound(1).GetType() == TokenType::Identifier)
		{
			m_IsPacked = true;
			m_PackedToken = packed;
			if (startLocation.line == 0) startLocation = packed.GetStartLocation();
			lexer.NextCompoundToken();
		}

		Token next = lexer.PeekCompound();
		if (FunPtrTypeNode::StartsSignatureType(lexer))
		{
			auto funcPtrType = FunPtrTypeNode::TryParse(state);
			if (funcPtrType)
			{
				m_DelegateType = funcPtrType; // FuncPtr type stored in delegate field for backward compat
				if (startLocation.line == 0)
					startLocation = m_DelegateType->startLocation;
				endLocation = m_DelegateType->endLocation;
			}
			else
			{
				// Delegate parsing failed - fall back to treating 'delegate' as identifier
				auto baseName = CreateRef<NamespaceQualifiedNameNode>();
				baseName->ParseBody(state);
				m_BaseName = baseName;
				if (startLocation.line == 0)
					startLocation = m_BaseName->startLocation;
			}
		}
		else if (next.GetType() == TokenType::Identifier)
		{
			auto baseName = CreateRef<NamespaceQualifiedNameNode>();
			baseName->ParseBody(state);
			m_BaseName = baseName;
			if (startLocation.line == 0)
				startLocation = m_BaseName->startLocation;
		}
		else
		{
			errorTracker.AddError(
				next.GetStartLocation(),
				Core::StringFormat::Format("Expected type name, got '{0}'", next.GetText()),
				ErrorSeverity::Error);
			if (startLocation.line == 0)
				startLocation = next.GetStartLocation();
			return;
		}

		while (true)
		{
			IndirectionLayer layer;

			Token next = lexer.PeekCompound();
			if (next.GetType() == TokenType::Const)
			{
				layer.isConst = true;
				layer.constToken = next;
				lexer.NextCompoundToken();
				next = lexer.PeekCompound();
			}

			if (!IsIndirectionOperator(next.GetType()))
			{
				// D-30: a `const` qualifies the layer written after it, so one with nothing after it applies
				// to nothing (`int32* const p`).
				if (layer.isConst)
					errorTracker.AddError(layer.constToken.GetStartLocation(),
						"'const' after a type must come before a '*' or '[': write 'const T' for a const value, or 'T const*' for a const pointer",
						ErrorSeverity::Error);
				break;
			}
			if (layer.isConst && next.GetType() == TokenType::Ampersand)
				errorTracker.AddError(layer.constToken.GetStartLocation(),
					"a reference cannot be const, it is never reseated: write 'const T&' for a reference to const",
					ErrorSeverity::Error);

			endLocation = next.GetEndLocation();

		// A DoubleStar compound token (`**`) is expanded into two separate pointer layers at
		// parse time, so downstream type construction (which wraps one layer per PointerType)
			// needs no changes and nested pointers like `void**` or `int32***` work. Deeper runs
			// are handled by the loop continuing: after consuming `**`, a following `*` becomes its
			// own Star layer on the next iteration.
			if (next.GetType() == TokenType::DoubleStar)
			{
				lexer.NextCompoundToken(); // consume the merged `**` token

				for (int star = 0; star < 2; ++star)
				{
					IndirectionLayer sl;
					sl.isConst = layer.isConst && star == 0;
					sl.constToken = layer.constToken;
					sl.indirectionToken = TokenType::Star;
					sl.indirectionStartToken = next;
					m_Indirections.Push(sl);
				}
				continue; // re-evaluate the next token (may be another `*`)
			}

			layer.indirectionToken = next.GetType();
			layer.indirectionStartToken = next;
			lexer.NextCompoundToken();

			if (next.GetType() == TokenType::OpenBracket)
			{
				Token afterBracket = lexer.PeekCompound();
				if (afterBracket.GetType() != TokenType::CloseBracket)
				{
					layer.arraySizeExpr = ParseArraySizeExpression(state);
					if (!layer.arraySizeExpr)
					{
						Token bad = lexer.PeekCompound();
						errorTracker.AddError(
							bad.GetStartLocation(),
							Core::StringFormat::Format("Expected array size expression, got '{0}'", bad.GetText()),
							ErrorSeverity::Error);
						while (true)
						{
							Token t = lexer.PeekCompound();
							if (t.GetType() == TokenType::CloseBracket ||
								t.GetType() == TokenType::EndOfFile ||
								t.GetType() == TokenType::SemiColon)
								break;
							lexer.NextCompoundToken();
						}
					}
				}

				Token close = lexer.PeekCompound();
				if (close.GetType() == TokenType::CloseBracket)
				{
					layer.closingBracketToken = close;
					endLocation = close.GetEndLocation();
					lexer.NextCompoundToken();
				}
				else
				{
					errorTracker.AddError(
						close.GetStartLocation(),
						Core::StringFormat::Format("Expected ']' to close array size, got '{0}'", close.GetText()),
						ErrorSeverity::Error);
					layer.closingBracketToken = close;
					endLocation = close.GetEndLocation();
				}
			}

			m_Indirections.Push(layer);
		}

		m_Children.Clear();
		if (m_BaseName)
			m_Children.Push(m_BaseName);
		if (m_DelegateType)
			m_Children.Push(m_DelegateType);
		for (uint32 i = 0; i < m_Indirections.Count(); ++i)
		{
			if (m_Indirections[i].arraySizeExpr)
				m_Children.Push(m_Indirections[i].arraySizeExpr);
		}

		if (endLocation.line == 0)
		{
			if (m_DelegateType)
				endLocation = m_DelegateType->endLocation;
			else if (m_BaseName)
				endLocation = m_BaseName->endLocation;
		}
	}

	Ref<QualTypeNode> QualTypeNode::ParseReturnList(ParserState& state)
	{
		Lexer& lexer = state.GetLexer();
		auto node = CreateRef<QualTypeNode>();
		const Token open = lexer.PeekCompound();
		node->startLocation = open.GetStartLocation();
		lexer.NextCompoundToken(); // [
		while (true)
		{
			auto element = CreateRef<QualTypeNode>();
			element->ParseBody(state);
			if (lexer.PeekCompound().GetType() == TokenType::Ellipsis)
			{
				lexer.NextCompoundToken();
				element->m_ExpandsPack = true;
			}
			node->m_ReturnList.Push(element);
			node->m_Children.Push(element);
			if (lexer.PeekCompound().GetType() != TokenType::Comma) break;
			lexer.NextCompoundToken();
		}
		const Token close = lexer.PeekCompound();
		if (close.GetType() != TokenType::CloseBracket)
			state.GetErrorTracker().AddError(close.GetStartLocation(),
				Core::StringFormat::Format("Expected ']' to close the return list, got '{0}'", close.GetText()), ErrorSeverity::Error);
		else
		{
			node->endLocation = close.GetEndLocation();
			lexer.NextCompoundToken();
		}
		return node;
	}

	Ref<QualTypeNode> QualTypeNode::ParseReturnType(ParserState& state)
	{
		if (state.GetLexer().PeekCompound().GetType() == TokenType::OpenBracket) return ParseReturnList(state);
		auto node = CreateRef<QualTypeNode>();
		node->ParseBody(state);
		return node;
	}

	bool QualTypeNode::StartsReturnList(const Lexer& lexer)
	{
		// `[` ... `]`, then a name - possibly qualified or with template arguments - and its `(`.
		constexpr uint32 kMaxLookahead = 256;
		if (lexer.PeekCompound().GetType() != TokenType::OpenBracket) return false;
		uint32 i = 0;
		for (int32 depth = 0; i < kMaxLookahead; ++i)
		{
			const TokenType type = lexer.PeekCompound(i).GetType();
			if (type == TokenType::EndOfFile) return false;
			if (type == TokenType::OpenBracket || type == TokenType::OpenParen || type == TokenType::OpenCurl) ++depth;
			else if ((type == TokenType::CloseBracket || type == TokenType::CloseParen || type == TokenType::CloseCurl) && --depth == 0) break;
		}
		if (lexer.PeekCompound(++i).GetType() != TokenType::Identifier) return false;
		for (++i; i < kMaxLookahead;)
		{
			const TokenType type = lexer.PeekCompound(i).GetType();
			if (type == TokenType::DoubleColon && lexer.PeekCompound(i + 1).GetType() == TokenType::Identifier) { i += 2; continue; }
			if (type != TokenType::Less) break;
			for (int32 angles = 0; i < kMaxLookahead; ++i)
			{
				const TokenType inner = lexer.PeekCompound(i).GetType();
				if (inner == TokenType::EndOfFile) return false;
				angles += inner == TokenType::Less ? 1 : inner == TokenType::Greater ? -1 : inner == TokenType::DoubleGreater ? -2 : 0;
				if (angles <= 0) { ++i; break; }
			}
		}
		return lexer.PeekCompound(i).GetType() == TokenType::OpenParen;
	}

	bool QualTypeNode::DeclaresConstValue() const
	{
		bool valueConst = m_IsLeadingConst;
		for (const IndirectionLayer& layer : m_Indirections)
			valueConst = layer.indirectionToken == TokenType::OpenBracket ? valueConst || layer.isConst : layer.isConst;
		return valueConst;
	}

	Ref<QualTypeNode> QualTypeNode::FromParts(
		const Ref<NamespaceQualifiedNameNode>& baseName,
		const Ref<AstNode>& delegateType,
		const Core::Vector<IndirectionLayer>& indirections,
		bool leadingConst)
	{
		auto node = CreateRef<QualTypeNode>();
		node->m_IsLeadingConst = leadingConst;
		node->m_BaseName = baseName;
		node->m_DelegateType = delegateType;
		node->m_Indirections = indirections;

		node->m_Children.Clear();
		if (node->m_BaseName)
			node->m_Children.Push(node->m_BaseName);
		if (node->m_DelegateType)
			node->m_Children.Push(node->m_DelegateType);
		for (uint32 i = 0; i < node->m_Indirections.Count(); ++i)
			if (node->m_Indirections[i].arraySizeExpr)
				node->m_Children.Push(node->m_Indirections[i].arraySizeExpr);

		const Ref<AstNode> first = (node->m_BaseName != nullptr) ? Ref<AstNode>(node->m_BaseName) : node->m_DelegateType;
		if (first)
			node->startLocation = first->startLocation;
		if (!indirections.Empty())
			node->endLocation = indirections.Back().closingBracketToken.GetEndLocation();
		else if (first)
			node->endLocation = first->endLocation;
		return node;
	}
} // namespace Scripting
