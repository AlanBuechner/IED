#include "Features/DefinitionWriter.h"
#include "Features/DeclarationText.h"
#include "Server/CompilationSession.h"
#include "Model/SymbolTable.h"
#include "Model/TypeHandle.h"
#include "Model/Types/FunctionType.h"
#include "Parser/AstNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/NamespaceNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/TemplateDefinitionNode.h"

namespace C4LSP::Features
{
	using Scripting::AstNode;
	using Scripting::FunctionDefinitionNode;
	using Scripting::NodeType;
	using Scripting::ObjectDefinitionNode;
	using Scripting::TypeKind;

	namespace
	{
		bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

		// Written across lines, as one: each run of spaces that holds a line break is one space.
		std::string OnOneLine(const std::string& text)
		{
			std::string out;
			for (size_t i = 0; i < text.size();)
			{
				if (!IsSpace(text[i]))
				{
					out += text[i++];
					continue;
				}
				size_t end = i;
				bool breaks = false;
				for (; end < text.size() && IsSpace(text[end]); ++end)
					breaks = breaks || text[end] == '\n';
				out += breaks ? std::string(" ") : text.substr(i, end - i);
				i = end;
			}
			const size_t first = out.find_first_not_of(' ');
			const size_t last = out.find_last_not_of(' ');
			return first == std::string::npos ? std::string() : out.substr(first, last - first + 1);
		}

		std::string TextBetween(std::string_view text, size_t start, size_t end)
		{
			return start <= end && end <= text.size() ? std::string(text.substr(start, end - start)) : std::string();
		}

		// A type's name as an out-of-line definition writes its owner: a specialization's with the arguments it is for.
		std::string OwnerName(Server::CompilationSession& session, const ObjectDefinitionNode& type)
		{
			std::string name = type.GetNameToken().GetText();
			if (type.IsSpecialization())
				name += DefinitionWriter::TextOf(session, *type.GetSpecializationArguments());
			return name;
		}
	} // namespace

	std::string DefinitionWriter::TextOf(Server::CompilationSession& session, const AstNode& node)
	{
		return TextBetween(DeclarationText::SourceText(session, node.startLocation.sourceId), node.startLocation.byteOffset, node.endLocation.byteOffset);
	}

	DeclarationOwners DefinitionWriter::OwnersOf(Server::CompilationSession& session, const FunctionDefinitionNode& declaration)
	{
		DeclarationOwners owners;
		for (const AstNode* node : DeclarationText::PathTo(session, declaration))
		{
			if (node->Type() == NodeType::Namespace && !static_cast<const Scripting::NamespaceNode*>(node)->IsRoot())
				owners.namespaces.push_back(static_cast<const Scripting::NamespaceNode*>(node)->Name());
			else if (node->Type() == NodeType::ObjectDefinition)
				owners.types.push_back(static_cast<const ObjectDefinitionNode*>(node));
		}
		return owners;
	}

	bool DefinitionWriter::IsTemplated(const FunctionDefinitionNode& declaration, const DeclarationOwners& owners)
	{
		if (declaration.IsTemplated())
			return true;
		for (const ObjectDefinitionNode* type : owners.types)
			if (type->IsTemplated() || type->IsSpecialization())
				return true;
		return false;
	}

	std::string DefinitionWriter::Parameters(Server::CompilationSession& session, const FunctionDefinitionNode& declaration)
	{
		const std::string_view text = DeclarationText::SourceText(session, declaration.startLocation.sourceId);
		const size_t open = declaration.GetOpenParenToken().GetEndLocation().byteOffset;
		const size_t close = declaration.GetCloseParenToken().GetStartLocation().byteOffset;
		std::string written = TextBetween(text, open, close);
		// Each default goes, from the `=` before it to its end: the last first, so the earlier ones' offsets still hold.
		const auto& parameters = declaration.GetParameters();
		for (uint32 i = parameters.Count(); i-- > 0;)
		{
			if (!parameters[i] || !parameters[i]->GetDefaultValue())
				continue;
			const size_t valueStart = parameters[i]->GetDefaultValue()->startLocation.byteOffset;
			const size_t valueEnd = parameters[i]->GetDefaultValue()->endLocation.byteOffset;
			const size_t equals = valueStart > open ? text.rfind('=', valueStart - 1) : std::string_view::npos;
			if (equals == std::string_view::npos || equals < open || valueEnd > close)
				continue;
			size_t cut = equals;
			while (cut > open && IsSpace(text[cut - 1]))
				--cut;
			written.erase(cut - open, valueEnd - cut);
		}
		// A comment ends at its line's end, so a list with one keeps its lines.
		return written.find("//") == std::string::npos ? OnOneLine(written) : written;
	}

	std::string DefinitionWriter::TrailingQualifiers(Server::CompilationSession& session, const FunctionDefinitionNode& declaration)
	{
		std::string out;
		if (declaration.IsConstMethod())
			out += " const";
		if (declaration.IsNoexcept())
			out += " noexcept";
		if (declaration.IsExtensionMethod())
			out += " extends " + TextOf(session, *declaration.GetExtendsTypeName());
		return out;
	}

	std::string DefinitionWriter::Signature(Server::CompilationSession& session, const MissingDefinition& missing, const DeclarationOwners& owners,
		const std::vector<std::string>& namespaces, const DefinitionStyle& style)
	{
		const FunctionDefinitionNode& declaration = *missing.declaration;
		if (!declaration.GetReturnType() || namespaces.size() > owners.namespaces.size())
			return {};
		for (size_t i = 0; i < namespaces.size(); ++i)
			if (namespaces[i] != owners.namespaces[i])
				return {}; // the place is in other namespaces than the declaration's: no name written there reaches it

		std::string out;
		for (const ObjectDefinitionNode* type : owners.types)
			if (type->GetTemplateDefinition())
				out += TextOf(session, *type->GetTemplateDefinition()) + " ";
		if (declaration.GetTemplateDefinition())
			out += TextOf(session, *declaration.GetTemplateDefinition()) + " ";
		if (style.writesModifiers)
		{
			const std::string access = DeclarationText::AccessOf(declaration);
			if (!access.empty())
				out += access + " ";
			if (declaration.IsStatic())
				out += "static ";
		}
		out += TextOf(session, *declaration.GetReturnType()) + " ";
		for (size_t i = namespaces.size(); i < owners.namespaces.size(); ++i)
			out += owners.namespaces[i] + "::";
		for (const ObjectDefinitionNode* type : owners.types)
			out += OwnerName(session, *type) + "::";
		out += DeclarationText::NameOf(declaration);
		out += "(" + Parameters(session, declaration) + ")";
		return out + TrailingQualifiers(session, declaration);
	}

	WrittenDefinition DefinitionWriter::Body(const std::string& opening, const DefinitionStyle& style, const std::string& statement)
	{
		WrittenDefinition out;
		out.text = opening + (style.braceOnOwnLine ? "\n" + style.indent + "{\n" : std::string(" {\n"));
		out.text += style.indent + style.indentUnit;
		out.caret = out.text.size();
		out.text += statement + "\n" + style.indent + "}";
		return out;
	}

	WrittenDefinition DefinitionWriter::OutOfLine(Server::CompilationSession& session, const MissingDefinition& missing,
		const std::vector<std::string>& namespaces, const DefinitionStyle& style)
	{
		const DeclarationOwners owners = OwnersOf(session, *missing.declaration);
		const std::string signature = Signature(session, missing, owners, namespaces, style);
		if (signature.empty())
			return {};
		const std::string returnType = TextOf(session, *missing.declaration->GetReturnType());
		return Body(style.indent + signature, style, DefaultReturn(missing.record, returnType, IsTemplated(*missing.declaration, owners)));
	}

	WrittenDefinition DefinitionWriter::InPlaceBody(Server::CompilationSession& session, const MissingDefinition& missing, const DefinitionStyle& style)
	{
		const FunctionDefinitionNode& declaration = *missing.declaration;
		const std::string returnType = declaration.GetReturnType() ? TextOf(session, *declaration.GetReturnType()) : std::string();
		return Body("", style, DefaultReturn(missing.record, returnType, IsTemplated(declaration, OwnersOf(session, declaration))));
	}

	std::string DefinitionWriter::DefaultReturn(const Scripting::FunctionRecord* record, std::string_view writtenReturnType, bool templated)
	{
		if (templated || !record || !record->type)
			return {}; // a template's instances decide what its type is
		const Scripting::Type* type = Scripting::TypeHandle::UnwrapAliases(record->type->GetReturnType());
		if (!type)
			return {};
		switch (type->GetKind())
		{
		case TypeKind::Bool:
			return "return false;";
		case TypeKind::Int8: case TypeKind::Int16: case TypeKind::Int32: case TypeKind::Int64:
		case TypeKind::UInt8: case TypeKind::UInt16: case TypeKind::UInt32: case TypeKind::UInt64:
		case TypeKind::SNorm8: case TypeKind::SNorm16: case TypeKind::SNorm32: case TypeKind::SNorm64:
		case TypeKind::UNorm8: case TypeKind::UNorm16: case TypeKind::UNorm32: case TypeKind::UNorm64:
		case TypeKind::Char8: case TypeKind::Char16: case TypeKind::Char32:
			return "return 0;";
		case TypeKind::Float32:
			return "return 0.0f;";
		case TypeKind::Float64:
			return "return 0.0;";
		case TypeKind::Pointer: case TypeKind::FunPtr: case TypeKind::Func:
			return "return nullptr;";
		case TypeKind::Enum:
			return "return (" + std::string(writtenReturnType) + ")0;"; // a braced list or `Mode()` does not make one
		case TypeKind::Object: case TypeKind::Union: case TypeKind::Swizzle: case TypeKind::Array:
			return "return {};";
		default:
			return {}; // void needs nothing, and a reference has no value to give
		}
	}
} // namespace C4LSP::Features
