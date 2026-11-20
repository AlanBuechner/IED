#include "Features/Completion.h"
#include "Features/AnnotationCompletion.h"
#include "Features/CompletionCandidates.h"
#include "Features/MemberChain.h"
#include "Server/CompilationSession.h"
#include "Server/PlaceInAnalysis.h"
#include "Server/SemanticResolver.h"
#include "SourceManager/Source.h"
#include "Features/TypeText.h" // FunctionSignature, TypeText
#include "Model/ProgramModel.h" // the model is the only thing phases query (doc 03)
#include "Model/TypeHandle.h"   // UnwrapToValue (alias/reference -> value type)
#include "Model/Types.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/QualTypeNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Statements/CodeBlockNode.h"
#include <cctype>
#include <algorithm>
#include <set>

namespace C4LSP::Features
{
	// LSP CompletionItemKind values.
	namespace Kind { constexpr int Function = 3; constexpr int Variable = 6; constexpr int Field = 5; constexpr int Class = 7; constexpr int Method = 2; constexpr int Enum = 14; constexpr int Module = 9; }

	/// Member access sees through pointers / references / aliases (the analyzer's own rule), so
	/// completion unwraps the base type identically.
	static const Scripting::Type* Deref(const Scripting::Type* type) { return Scripting::TypeHandle::UnwrapToValue(type); }

	Core::Vector<CompletionItem> Completion::At(Server::CompilationSession& session, const std::string& uri, const Protocol::Position& position)
	{
		const uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return {};
		return At(session, uri, std::string(session.RawTextOf(sourceId)), position);
	}

	Core::Vector<CompletionItem> Completion::At(Server::CompilationSession& session, const std::string& uri, const std::string& text, const Protocol::Position& position)
	{
		const uint32 sourceId = session.FindSourceIdByUri(uri);
		if (sourceId == 0)
			return {};

		auto unit = session.GetCompiler().GetCompilationUnit(sourceId);
		if (!unit || !unit->GetRootAST())
			return {};

		const Scripting::Source& source = *session.GetSourceManager().GetInfo(sourceId).source;
		const Server::PlaceInAnalysis place = Server::PlaceInAnalyzedText(session.RawTextOf(sourceId), text, position);
		const Server::LocatedNode located = Server::NodeLocator::Locate(source, sourceId, unit->GetRootAST(), place.position);
		const size_t offset = OffsetAt(text, position);
		const std::string prefix = WordBefore(text, offset);
		CompletionCandidates candidates(prefix);

		const Server::SemanticResolver& resolver = session.GetResolver();
		const std::optional<AnnotationCompletion::Site> annotation = AnnotationCompletion::Find(text, offset, InTypeBody(located.path));
		const Scripting::AnnotationContext annotationAt = resolver.AnnotationContextAt(located.path, sourceId);
		if (annotation && !annotation->qualifier.empty()) // `[Scene::Ra`: only an annotation is named there
		{
			Core::Vector<CompletionItem> named;
			AnnotationCompletion::Add(resolver, *annotation, annotationAt, named);
			Core::Vector<CompletionItem> matching;
			for (CompletionItem& item : named)
				if (CompletionCandidates::StartsWith(item.label, prefix)) matching.Push(std::move(item));
			return matching;
		}
		if (const std::string qualifier = QualifierBefore(text, offset); !qualifier.empty())
			if (CollectQualified(resolver, sourceId, located.path, qualifier, candidates))
				return candidates.Items();

		// After an accessor only the members of what it is made on fit - through calls, indexes and other members
		// (`GetTransform()->`, `m_Body.Shape.`), read from the text, so a chain typed since the analysis resolves too.
		if (const std::optional<MemberChain> chain = MemberChain::Before(text, offset))
		{
			CollectMembersOf(chain->TypeIn(resolver, sourceId, located.path), candidates);
			return candidates.Items();
		}

		Core::Vector<CompletionItem> items;
		const bool places = AnnotationCompletion::InPlaces(text, offset);
		if (places)
			AnnotationCompletion::AddPlaces(items);
		else if (annotation)
			AnnotationCompletion::Add(resolver, *annotation, annotationAt, items);
		Core::Vector<CompletionItem> named;
		for (CompletionItem& item : items)
			if (CompletionCandidates::StartsWith(item.label, prefix)) named.Push(std::move(item));
		if (places) // a place is one of a few words, never a name from code
			return named;

		if (located.Found())
		{
			CollectEnclosingContext(located.path, candidates);
			CollectClassMembers(located.path, candidates);
			CollectMembersOf(resolver.ImplicitThis(located.path), candidates); // an implicit `this`, inherited members too
		}
		CollectGlobalSymbols(resolver, candidates);
		for (CompletionItem& item : candidates.Items()) named.Push(std::move(item));
		return named;
	}

	bool Completion::InTypeBody(const Core::Vector<const Scripting::AstNode*>& path)
	{
		for (uint32 i = path.Count(); i-- > 0;)
		{
			const Scripting::NodeType type = path[i]->Type();
			if (type == Scripting::NodeType::FunctionDefinition || type == Scripting::NodeType::LambdaExpression) return false;
			if (type == Scripting::NodeType::ObjectDefinition || type == Scripting::NodeType::UnionDefinition) return true;
		}
		return false;
	}

	std::string Completion::WordBefore(const std::string& text, size_t offset)
	{
		size_t start = std::min(offset, text.size());
		const size_t end = start;
		while (start > 0 && IsIdentChar(text[start - 1])) --start;
		return text.substr(start, end - start);
	}

	size_t Completion::OffsetAt(const std::string& text, const Protocol::Position& position)
	{
		size_t offset = 0;
		for (uint32 line = 0; line < position.line && offset < text.size(); ++line)
		{
			const size_t newline = text.find('\n', offset);
			if (newline == std::string::npos)
				return text.size();
			offset = newline + 1;
		}
		return std::min(offset + position.character, text.size());
	}

	void Completion::CollectMembersOf(const Scripting::Type* type, CompletionCandidates& out)
	{
		type = Deref(type);
		if (!type)
			return;
		switch (type->GetKind())
		{
		case Scripting::TypeKind::Object: {
			// Derived first, so a member it declares wins over a base's of the same name.
			std::set<const Scripting::ObjectType*> seen;
			Core::Vector<const Scripting::ObjectType*> pending;
			pending.Push(static_cast<const Scripting::ObjectType*>(type));
			for (uint32 i = 0; i < pending.Count(); ++i)
			{
				if (!seen.insert(pending[i]).second)
					continue;
				AddMembersOf(pending[i], out);
				for (const Scripting::ObjectType* base : pending[i]->GetBaseTypes())
					if (base)
						pending.Push(base);
			}
			break;
		}
		case Scripting::TypeKind::Union: {
			for (const auto& field : static_cast<const Scripting::UnionType*>(type)->GetFields())
				out.Add(field.mName, Kind::Field, [&] { return TypeText(field.m_Type); });
			break;
		}
		case Scripting::TypeKind::Enum: {
			for (const auto& member : static_cast<const Scripting::EnumType*>(type)->GetMembers())
				out.Add(member.mName, Kind::Enum, [&] { return type->ToString(); });
			break;
		}
		case Scripting::TypeKind::Pointer:
			// `p->`: the pointee's members.
			CollectMembersOf(static_cast<const Scripting::PointerType*>(type)->GetPointeeType(), out);
			break;
		default:
			break; // scalars have no members
		}
	}

	void Completion::AddMembersOf(const Scripting::ObjectType* obj, CompletionCandidates& out)
	{
		for (const auto& field : obj->GetFields())
			out.Add(field.mName, Kind::Field, [&] { return TypeText(field.m_Type); });
		auto addMethods = [&out](const Core::Vector<Scripting::ObjectType::MethodInfo>& methods) {
			for (const auto& method : methods)
				out.Add(method.mName, Kind::Method, [&] { return MethodSignature(method); });
		};
		addMethods(obj->GetMethods());
		addMethods(obj->GetExtensionMethods()); // operators + CBind foreign members are extensions
	}

	std::string Completion::MethodSignature(const Scripting::ObjectType::MethodInfo& method)
	{
		std::string text = TypeText(method.m_ReturnType);
		if (method.m_ParameterTypes.Count() == 0)
			return text + "()";
		text += "(";
		for (uint32 i = 0; i < method.m_ParameterTypes.Count(); ++i)
		{
			if (i > 0)
				text += ", ";
			text += TypeText(method.m_ParameterTypes[i]);
		}
		return text + ")";
	}

	bool Completion::IsIdentChar(char c)
	{
		return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '_';
	}

	void Completion::CollectEnclosingContext(const Core::Vector<const Scripting::AstNode*>& path, CompletionCandidates& out)
	{
		for (int i = static_cast<int>(path.Count()) - 1; i >= 0; --i)
		{
			const Scripting::AstNode* node = path[i];
			switch (node->Type())
			{
			case Scripting::NodeType::FunctionDefinition: {
				const auto& func = static_cast<const Scripting::FunctionDefinitionNode&>(*node);
				for (const auto& param : func.GetParameters())
					out.Add(param->GetNameToken().GetText(), Kind::Variable,
						[&] { return param->GetType() ? param->GetType()->ToString() : std::string(); });
				break;
			}
			case Scripting::NodeType::CodeBlock: {
				const auto& block = static_cast<const Scripting::CodeBlockNode&>(*node);
				for (const auto& stmt : block.GetStatements())
					if (stmt && stmt->Type() == Scripting::NodeType::VariableDefinition)
					{
						const auto& var = static_cast<const Scripting::VariableDefinitionNode&>(*stmt.Get());
						out.Add(var.GetNameToken().GetText(), Kind::Variable,
							[&] { return var.GetType() ? var.GetType()->ToString() : std::string(); });
					}
				break;
			}
			default:
				break;
			}
		}
	}

	void Completion::CollectClassMembers(const Core::Vector<const Scripting::AstNode*>& path, CompletionCandidates& out)
	{
		for (int i = static_cast<int>(path.Count()) - 1; i >= 0; --i)
		{
			const Scripting::AstNode* node = path[i];
			if (node->Type() != Scripting::NodeType::ObjectDefinition)
				continue;

			const auto& obj = static_cast<const Scripting::ObjectDefinitionNode&>(*node);
			for (const auto& member : obj.GetBodyMembers())
			{
				if (!member)
					continue;
				switch (member->Type())
				{
				case Scripting::NodeType::VariableDefinition: {
					const auto& field = static_cast<const Scripting::VariableDefinitionNode&>(*member.Get());
					out.Add(field.GetNameToken().GetText(), Kind::Field,
						[&] { return field.GetType() ? field.GetType()->ToString() : std::string(); });
					break;
				}
				case Scripting::NodeType::FunctionDefinition: {
					const auto& method = static_cast<const Scripting::FunctionDefinitionNode&>(*member.Get());
					if (method.GetName())
						out.Add(method.GetName()->GetFirstIdentifierToken().GetText(), Kind::Method,
							[&] { return method.GetReturnType().Get() ? method.GetReturnType()->ToString() + "()" : std::string("()"); });
					break;
				}
				default:
					break;
				}
			}
			break; // only the innermost class
		}
	}

	void Completion::CollectGlobalSymbols(const Server::SemanticResolver& resolver, CompletionCandidates& out)
	{
		resolver.ForEachGlobalSymbol([&out](const Core::String& name, const Scripting::SymbolInfo& info) {
			if (!out.Wants(name))
				return;
			out.Add(name, SymbolKindToLsp(info.kind), [&] {
				if (!info.functions.Empty()) return FunctionSignature(info.functions[0]->type);
				return info.type ? info.type->ToString() : std::string();
			});
		});
	}

	std::string Completion::QualifierBefore(const std::string& text, size_t offset)
	{
		size_t at = std::min(offset, text.size());
		while (at > 0 && IsIdentChar(text[at - 1])) --at; // the word being typed
		std::string qualifier;
		while (at >= 2 && text[at - 1] == ':' && text[at - 2] == ':')
		{
			const size_t nameEnd = at - 2;
			size_t nameStart = nameEnd;
			while (nameStart > 0 && IsIdentChar(text[nameStart - 1])) --nameStart;
			if (nameStart == nameEnd)
				return {};
			const std::string name = text.substr(nameStart, nameEnd - nameStart);
			qualifier = qualifier.empty() ? name : name + "::" + qualifier;
			at = nameStart;
		}
		return qualifier;
	}

	bool Completion::CollectQualified(const Server::SemanticResolver& resolver, uint32 unitId, const Core::Vector<const Scripting::AstNode*>& path,
		const std::string& qualifier, CompletionCandidates& out)
	{
		const Scripting::SymbolInfo* named = resolver.Lookup(qualifier, unitId, &path);
		if (!named)
			return false;
		if (named->kind == Scripting::SymbolKind::Namespace && named->scope)
		{
			AddScopeSymbols(*named->scope, out);
			return true;
		}
		if (named->kind == Scripting::SymbolKind::Type && named->type)
		{
			CollectMembersOf(named->type, out);
			return true;
		}
		return false;
	}

	void Completion::AddScopeSymbols(const Scripting::Scope& scope, CompletionCandidates& out)
	{
		Core::Vector<std::pair<Core::String, Scripting::SymbolInfo*>> entries;
		scope.Enumerate(entries);
		for (const auto& [name, info] : entries)
		{
			if (Scripting::IsExtensionMethodKey(name)) continue; // a member of the type it extends
			out.Add(name, SymbolKindToLsp(info->kind), [&] {
				if (!info->functions.Empty()) return FunctionSignature(info->functions[0]->type);
				return info->type ? info->type->ToString() : std::string();
			});
		}
	}

	int Completion::SymbolKindToLsp(Scripting::SymbolKind kind)
	{
		switch (kind)
		{
		case Scripting::SymbolKind::Function:  return Kind::Function;
		case Scripting::SymbolKind::Type:      return Kind::Class;
		case Scripting::SymbolKind::Namespace: return Kind::Module;
		case Scripting::SymbolKind::Variable:  return Kind::Variable;
		default:                               return Kind::Variable;
		}
	}
} // namespace C4LSP::Features
