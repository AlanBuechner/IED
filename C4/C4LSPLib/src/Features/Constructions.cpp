#include "Features/Constructions.h"
#include "Server/SemanticResolver.h"
#include "Model/ProgramModel.h"
#include "Model/Types/AliasType.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Expressions/FunctionCallNode.h"
#include "Parser/Nodes/Expressions/NewExpressionNode.h"

namespace C4LSP::Features
{
	namespace
	{
		using Path = Core::Vector<const Scripting::AstNode*>;

		// The object type `type` is, through aliases only: a pointer or a reference to one builds nothing.
		const Scripting::ObjectType* ObjectOf(const Scripting::Type* type)
		{
			while (type && type->GetKind() == Scripting::TypeKind::Alias) type = static_cast<const Scripting::AliasType*>(type)->GetAliasedType();
			return type && type->GetKind() == Scripting::TypeKind::Object ? static_cast<const Scripting::ObjectType*>(type) : nullptr;
		}

		ReferenceScanner::NameRange TypeName(const Scripting::QualTypeNode& type)
		{
			if (!type.BaseName()) return { type.startLocation, type.endLocation };
			const Scripting::Token& last = type.BaseName()->GetLastIdentifierToken();
			return { last.GetStartLocation(), last.GetEndLocation() };
		}

		Core::Vector<Constructions::Construction> OfNew(const Scripting::NewExpressionNode& n, const Path& path, const Server::SemanticResolver& resolver)
		{
			if (n.IsArray() || n.HasBraces() || !n.GetAllocType()) return {};
			const Scripting::ObjectType* type = ObjectOf(resolver.ResolveQualType(*n.GetAllocType(), path));
			if (!type) return {};
			return { { type, n.GetArguments().Count(), TypeName(*n.GetAllocType()) } };
		}

		// Each declarator built by its type's create: `T x(args)`, or `T x;` (an initializer `= value` copies). A field is
		// built with the object that holds it.
		Core::Vector<Constructions::Construction> OfVariable(const Scripting::VariableDefinitionNode& n, const Path& path,
			const Server::SemanticResolver& resolver)
		{
			const bool field = path.Count() >= 2 && path[path.Count() - 2]->Type() == Scripting::NodeType::ObjectDefinition;
			if (field || !n.GetType()) return {};
			const Scripting::ObjectType* type = ObjectOf(resolver.ResolveQualType(*n.GetType(), path));
			if (!type) return {};
			Core::Vector<Constructions::Construction> out;
			for (const Scripting::Declarator& declarator : n.GetDeclarators())
				if (!declarator.m_Initializer)
					out.Push({ type, declarator.m_CtorArgs.Count(), { declarator.m_NameToken.GetStartLocation(), declarator.m_NameToken.GetEndLocation() } });
			return out;
		}

		// `T(args)`: a call whose callee names a type. `T({a, b})` is `T{a, b}`, which calls no create.
		Core::Vector<Constructions::Construction> OfCall(const Scripting::FunctionCallNode& n, const Path& path, const Server::SemanticResolver& resolver)
		{
			const Scripting::AstNode* callee = n.GetCallee().Get();
			if (!callee || !ReferenceScanner::IsReferenceNode(*callee)) return {};
			const auto& arguments = n.GetArguments();
			if (arguments.Count() == 1 && arguments[0] && arguments[0]->Type() == Scripting::NodeType::BracedInitializer) return {};
			Path calleePath = path;
			calleePath.Push(callee);
			const Server::NodeResolution resolved = resolver.ResolveReference(*callee, calleePath);
			const Scripting::ObjectType* type = resolved.isFunction ? nullptr : ObjectOf(resolved.type);
			if (!type || !resolved.declaration || resolved.declaration->Type() != Scripting::NodeType::ObjectDefinition) return {};
			return { { type, arguments.Count(), ReferenceScanner::NameOf(*callee) } };
		}
	}

	Core::Vector<Constructions::Construction> Constructions::Of(const Scripting::AstNode& node, const Path& path, const Server::SemanticResolver& resolver)
	{
		switch (node.Type())
		{
		case Scripting::NodeType::NewExpression: return OfNew(static_cast<const Scripting::NewExpressionNode&>(node), path, resolver);
		case Scripting::NodeType::VariableDefinition: return OfVariable(static_cast<const Scripting::VariableDefinitionNode&>(node), path, resolver);
		case Scripting::NodeType::FunctionCall: return OfCall(static_cast<const Scripting::FunctionCallNode&>(node), path, resolver);
		default: return {};
		}
	}

	std::vector<const Scripting::AstNode*> Constructions::Called(const Scripting::ProgramModel& model, const Construction& construction)
	{
		std::vector<const Scripting::AstNode*> declarations;
		for (uint32 i = 0; i < construction.type->GetMethodCount(); ++i)
		{
			const Scripting::ObjectType::MethodInfo& method = construction.type->GetMethods()[i];
			if (!method.HasOperator("create")) continue;
			const Scripting::FunctionRecord* record = model.RecordForMethod(construction.type, &method);
			const uint32 parameters = record && record->type ? record->type->GetParameterCount() : 0;
			if (!record || construction.argumentCount < record->minArity || construction.argumentCount > parameters) continue;
			for (const Scripting::AstNode* declaration : record->declarations) declarations.push_back(declaration);
		}
		return declarations;
	}
} // namespace C4LSP::Features
