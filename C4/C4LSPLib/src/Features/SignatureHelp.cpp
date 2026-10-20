#include "Features/SignatureHelp.h"
#include "Server/CompilationSession.h"
#include "Server/SemanticResolver.h"
#include "SourceManager/Source.h"
#include "Model/ProgramModel.h" // the model is the only thing phases query (doc 03)
#include "Model/SymbolTable.h"      // project-wide symbol scan fallback
#include "Model/Types.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/IdentifierExpressionNode.h"
#include "Parser/Nodes/Expressions/MemberAccessNode.h"
#include "Parser/Nodes/Expressions/StaticAccessNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include <algorithm>
#include <cctype>

namespace C4LSP::Features
{
	SignatureHelpResult SignatureHelp::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		SignatureHelpResult result;
		uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return result;

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return result;

		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, unit->GetRootAST(), position);

		const Scripting::AstNode* callee = nullptr;
		uint32 activeArg = 0;
		if (located.Found() && FindEnclosingCall(source, located, sourceId, position, callee, activeArg))
		{
			auto info = BuildSignature(session.GetResolver(), located.path, *callee);
			if (!info.label.empty())
			{
				result.found = true;
				result.signatures.Push(info);
				result.activeSignature = 0;
				result.activeParameter = activeArg;
				return result;
			}
		}

		// Lexical fallback: the call is still being typed, so it has NOT parsed into a FunctionCall node yet. C4
		// requires a trailing ';', which means every intermediate state of a call (`Add(`, `Add(1`, an auto-closed
		// `Add()` without its semicolon) is syntactically incomplete and produces no call node at all - but that is
		// EXACTLY when VS2022 / Visual Assist show parameter hints (on the instant you type '('). Recover the callee
		// from raw text instead of the AST.
		size_t caretOffset = source.LineToByteOffset(position.line + 1) + position.character;
		std::string calleeName;
		uint32 lexicalArg = 0;
		if (LexicalCall(source, std::min(caretOffset, source.Size()), calleeName, lexicalArg))
		{
			const Scripting::AstNode* declaration = FindFunctionByName(session, sourceId, calleeName);
			if (declaration)
			{
				auto info = BuildFromDeclaration(static_cast<const Scripting::FunctionDefinitionNode&>(*declaration), calleeName);
				result.found = true;
				result.signatures.Push(info);
				result.activeSignature = 0;
				result.activeParameter = lexicalArg;
			}
		}
		return result;
	}

	bool SignatureHelp::FindEnclosingCall(const Scripting::Source& source, const Server::LocatedNode& located, uint32 sourceId,
		const Protocol::Position& position, const Scripting::AstNode*& outCallee, uint32& activeArg)
	{
		for (int i = static_cast<int>(located.path.Count()) - 1; i >= 0; --i)
		{
			const Scripting::AstNode* node = located.path[i];
			if (node->Type() != Scripting::NodeType::FunctionCall || node->startLocation.sourceId != sourceId)
				continue;

			const auto& call = static_cast<const Scripting::FunctionCallNode&>(*node);
			if (!call.GetCallee())
				return false;

			// Which argument the caret is in: count argument subtrees that start before it.
			size_t offset = source.LineToByteOffset(position.line + 1) + position.character;
			offset = std::min(offset, source.Size());
			activeArg = 0;
			for (const auto& arg : call.GetArguments())
				if (arg && arg->startLocation.byteOffset < offset)
					++activeArg;

			outCallee = call.GetCallee().Get();
			return true;
		}
		return false;
	}

	bool SignatureHelp::LexicalCall(const Scripting::Source& source, size_t caretOffset, std::string& outCalleeName, uint32& activeArg)
	{
		const auto& src = source; // Source is non-copyable; the lambda must capture by reference.
		const auto ch = [&src](size_t i) -> int { return static_cast<int>(i < src.Size() ? src.ReadCharAt(i) : -1); };

		// Walk backward to the nearest '(' that is still open at the caret (its matching ')' lies AFTER the caret,
		// or has not been typed yet). Any closer paren that closes before/at the caret rules out a call there.
		int depth = 0;
		size_t open = std::string::npos;
		for (size_t i = caretOffset; i-- > 0;)
		{
			int c = ch(i);
			if (c == ')') ++depth;
			else if (c == '(') { if (depth == 0) { open = i; break; } --depth; }
		}
		if (open == std::string::npos || open + 1 >= source.Size()) // require at least one char inside the parens
			return false;

		// The callee is the identifier / member chain immediately before '('. Scan back over whitespace, then stop at
		// the first character that is neither an identifier character nor a '.' or ':' (the access operators). This
		// tolerates chains like `obj.Method(` and `Ns::Method(`.
		size_t end = open;
		while (end > 0 && std::isspace(static_cast<unsigned char>(ch(end - 1)))) --end;
		const size_t nameEndExclusive = end; // one past the last identifier character of the callee chain
		while (end > 0)
		{
			int c = ch(end - 1);
			if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '.' || c == ':'))
				break;
			--end;
		}
		size_t nameStart = end; // first character of the callee chain (or == nameEndExclusive when there is none)

		// Simple name: the LAST segment of the chain, since a method/namespace lookup keys off it. `Add` -> "Add";
		// `obj.Method` -> "Method". This ignores any leading qualifier which we do not need to resolve here.
		size_t lastDot = std::string::npos;
		for (size_t i = nameStart; i < nameEndExclusive; ++i)
			if (ch(i) == '.' || ch(i) == ':')
				lastDot = i;
		const size_t simpleBegin = (lastDot != std::string::npos && lastDot + 1 < nameEndExclusive) ? lastDot + 1 : nameStart;

		std::string calleeName;
		for (size_t i = simpleBegin; i < nameEndExclusive; ++i)
			calleeName.push_back(static_cast<char>(ch(i)));
		if (calleeName.empty() || !std::isalpha(static_cast<unsigned char>(calleeName.front())))
			return false; // a bare '(' with no identifier before it is not a call

		// Active argument slot: count top-level commas between the open paren and the caret. Nested parens/brackets
		// (e.g. `f(g(1, 2),`) must NOT advance the outer slot - track depth so only depth-0 commas count.
		uint32 commaCount = 0;
		int innerDepth = 0;
		for (size_t i = open + 1; i < caretOffset && i < source.Size(); ++i)
		{
			int c = ch(i);
			if (c == '(' || c == '[' || c == '{') ++innerDepth;
			else if (c == ')' || c == ']' || c == '}') { if (innerDepth > 0) --innerDepth; }
			else if (c == ',' && innerDepth == 0) ++commaCount;
		}

		outCalleeName = std::move(calleeName);
		activeArg = commaCount;
		return true;
	}

	const Scripting::AstNode* SignatureHelp::FindFunctionByName(Server::CompilationSession& session, uint32 sourceId, const std::string& name)
	{
		// The model is program-wide and every unit shares it, so one scope-tree scan covers the whole
		// project - no per-unit loop needed. A name declared in an included file is still found here
		// (build-time visibility), which is exactly what a call to such a function needs.
		std::vector<const Scripting::AstNode*> decls;
		session.GetResolver().CollectDeclarationsByName(name, decls);
		for (const Scripting::AstNode* decl : decls)
			if (decl && decl->Type() == Scripting::NodeType::FunctionDefinition)
				return decl;
		return nullptr;
	}

	SignatureInfo SignatureHelp::BuildSignature(const Server::SemanticResolver& resolver, const Core::Vector<const Scripting::AstNode*>& path, const Scripting::AstNode& callee)
	{
		std::string name = CalleeName(callee);
		if (name.empty())
			return {};

		const Server::NodeResolution res = resolver.ResolveReference(callee, path);

		// Prefer the resolved declaration: it carries parameter names and types.
		if (res.declaration && res.declaration->Type() == Scripting::NodeType::FunctionDefinition)
			return BuildFromDeclaration(static_cast<const Scripting::FunctionDefinitionNode&>(*res.declaration), name);

		// Fallback: a built-in / extern function with no C4 declaration. The callee's resolved type is
		// its signature; parameter names are unavailable there.
		if (res.type)
		{
			const Scripting::Type* t = res.type;
			if (t->GetKind() == Scripting::TypeKind::Function)
			{
				const auto& ft = static_cast<const Scripting::FunctionType*>(t);
				Core::Vector<const Scripting::Type*> types;
				for (const auto& p : ft->GetParameters())
					types.Push(p.m_Type);
				return BuildFromParameterTypes(types, ft->IsVariadic(), name);
			}
			if (t->GetKind() == Scripting::TypeKind::FunPtr)
				return BuildFromParameterTypes(static_cast<const Scripting::FunPtrType*>(t)->GetParameterTypes(), false, name);
		}
		return {};
	}

	std::string SignatureHelp::CalleeName(const Scripting::AstNode& callee)
	{
		switch (callee.Type())
		{
		case Scripting::NodeType::IdentifierExpression:
			return static_cast<const Scripting::IdentifierExpressionNode&>(callee).GetNameToken().GetText();
		case Scripting::NodeType::MemberAccess:
			return static_cast<const Scripting::MemberAccessNode&>(callee).GetMemberNameToken().GetText();
		case Scripting::NodeType::StaticAccess:
			return static_cast<const Scripting::StaticAccessNode&>(callee).GetMemberNameToken().GetText();
		default:
			return std::string();
		}
	}

	SignatureInfo SignatureHelp::BuildFromDeclaration(const Scripting::FunctionDefinitionNode& decl, const std::string& name)
	{
		SignatureInfo info;
		info.label = name + "(";
		const auto& params = decl.GetParameters();
		for (uint32 i = 0; i < params.Count(); ++i)
		{
			if (i > 0)
				info.label += ", ";
			std::string text = ParameterText(*params[i]);
			info.label += text;
			SignatureParameter p;
			p.label = text;
			info.parameters.Push(p);
		}
		info.label += ")";
		return info;
	}

	SignatureInfo SignatureHelp::BuildFromParameterTypes(const Core::Vector<const Scripting::Type*>& paramTypes, bool variadic, const std::string& name)
	{
		SignatureInfo info;
		info.label = name + "(";
		for (uint32 i = 0; i < paramTypes.Count(); ++i)
		{
			if (i > 0)
				info.label += ", ";
			std::string text = paramTypes[i] ? paramTypes[i]->ToString() : "";
			info.label += text;
			SignatureParameter p;
			p.label = text;
			info.parameters.Push(p);
		}
		if (variadic)
			info.label += info.parameters.Count() > 0 ? ", ..." : "...";
		info.label += ")";
		return info;
	}

	std::string SignatureHelp::ParameterText(const Scripting::ParameterNode& param)
	{
		std::string text;
		if (param.GetType())
			text = param.GetType()->ToString();
		if (param.HasName())
			text += text.empty() ? param.GetNameToken().GetText() : " " + param.GetNameToken().GetText();
		return text;
	}
} // namespace C4LSP::Features
