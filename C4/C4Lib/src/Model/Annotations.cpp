#include "Model/Annotations.h"
#include "Model/AnnotationOverloads.h"
#include "Model/ConstEvaluator.h"
#include "Model/ProgramModel.h"
#include "Parser/Nodes/Declarations/AliasDefinitionNode.h"
#include "Parser/Nodes/Declarations/EnumDefinitionNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"
#include "Parser/Nodes/Declarations/UnionDefinitionNode.h"
#include "Parser/Nodes/Declarations/VariableDefinitionNode.h"
#include "Parser/Nodes/Expressions/LiteralNode.h"

namespace Scripting
{
	namespace
	{
		template<typename Node> const Core::Vector<Ref<AnnotationNode>>* AnnotationsAs(const AstNode* node)
		{
			return &static_cast<const Node*>(node)->GetAnnotations();
		}

		using ArgumentResult = QueryResult<ReflectedArgument>;
		constexpr const char* kArgumentRule = "an annotation argument must be a constant number, bool or string, or a type";

		bool Reflectable(const ConstantValue& value)
		{
			switch (value.kind)
			{
			case ConstantValue::Kind::Int64: case ConstantValue::Kind::Float64: case ConstantValue::Kind::Bool: return true;
			case ConstantValue::Kind::String: return value.unitWidth == 1; // nameof(T)
			default: return false;
			}
		}

		ArgumentResult Constant(const ConstantValue& value)
		{
			ReflectedArgument argument;
			argument.value = value;
			return ArgumentResult::Success(argument);
		}

		ArgumentResult Named(const Type* type)
		{
			ReflectedArgument argument;
			argument.type = type;
			return ArgumentResult::Success(argument);
		}

		ArgumentResult StringLiteral(const LiteralNode& literal)
		{
			if (literal.GetUnitWidth() != 1)
				return ArgumentResult::Fail(QueryError::NotAValue, "an annotation string argument is a UTF-8 literal", literal.startLocation);
			return Constant(ConstantValue::String(literal.GetValue()));
		}

		// What the argument reads as is a constant, or else the type its tokens name.
		ArgumentResult FoldExpression(const ProgramModel& model, const AstNode& argument, const QualTypeNode* type, const Scope* scope)
		{
			const uint32 unit = argument.startLocation.sourceId;
			auto value = ConstEvaluator(model, scope).Evaluate(&argument, unit);
			if (value.Ok() && Reflectable(value.value)) return Constant(value.value);
			const Core::String constant = value.Ok() ? "'" + argument.ToString() + "' is no number, bool or string" : value.message;
			if (!type) return ArgumentResult::Fail(QueryError::NotAValue, Core::String(kArgumentRule) + ": " + constant, argument.startLocation);
			auto named = model.ResolveTypeInUnitContext(*type, unit, scope);
			if (named.Ok()) return Named(named.value);
			return ArgumentResult::Fail(QueryError::NotAValue, Core::String(kArgumentRule) + ": '" + argument.ToString() + "' is neither a constant (" +
				constant + ") nor a type (" + named.message + ")", argument.startLocation);
		}

		ArgumentResult FoldType(const ProgramModel& model, const QualTypeNode& type, const Scope* scope)
		{
			auto named = model.ResolveTypeInUnitContext(type, type.startLocation.sourceId, scope);
			if (named.Ok()) return Named(named.value);
			return ArgumentResult::Fail(QueryError::NotFound, Core::String(kArgumentRule) + ": '" + type.ToString() + "' names no type (" + named.message + ")",
				type.startLocation);
		}

		QueryResult<void> Collect(const ProgramModel& model, const AnnotationNode& node, AnnotationPlace place, const Scope* scope,
			Core::Vector<ReflectedAnnotation>& out)
		{
			for (const auto& group : node.GetBracketGroups())
				for (const auto& item : group.annotations)
				{
					if (!item || item->IsCompilerAnnotation()) continue;
					auto annotation = FoldAnnotation(model, *item, place, scope);
					if (!annotation.Ok()) return QueryResult<void>::Fail(annotation.error, annotation.message, annotation.loc);
					out.Push(std::move(annotation.value));
				}
			return QueryResult<void>::Success();
		}
	}

	QueryResult<ReflectedArgument> FoldAnnotationArgument(const ProgramModel& model, const AstNode& written, const QualTypeNode* asType,
		const Scope* scope)
	{
		if (const auto* literal = dynamic_cast<const LiteralNode*>(&written); literal && literal->GetKind() == LiteralNode::LiteralKind::String)
			return StringLiteral(*literal);
		if (&written == asType) return FoldType(model, *asType, scope);
		return FoldExpression(model, written, asType, scope);
	}

	QueryResult<ReflectedAnnotation> FoldAnnotation(const ProgramModel& model, const AnnotationItem& item, AnnotationPlace place, const Scope* scope)
	{
		using Result = QueryResult<ReflectedAnnotation>;
		Core::Vector<ReflectedArgument> folded;
		for (uint32 i = 0; i < item.GetArguments().Count(); ++i)
		{
			auto argument = FoldAnnotationArgument(model, *item.GetArguments()[i], item.GetArgumentType(i).Get(), scope);
			if (!argument.Ok()) return Result::Fail(argument.error, argument.message, argument.loc);
			folded.Push(argument.value);
		}
		ReflectedAnnotation annotation;
		annotation.name = item.GetName();
		const DeclaredAnnotations::Overloads* overloads = model.Annotations().Find(item.GetName().c_str());
		if (!overloads)
		{
			annotation.arguments = std::move(folded);
			return Result::Success(std::move(annotation));
		}
		auto match = ChooseAnnotationOverload(*overloads, item, place, folded);
		if (!match.Ok()) return Result::Fail(match.error, match.message, match.loc);
		annotation.arguments = std::move(match.value.arguments);
		annotation.declaration = match.value.signature;
		return Result::Success(std::move(annotation));
	}

	// Reflection asks this of every entity it registers: the node's kind is read, not cast.
	const Core::Vector<Ref<AnnotationNode>>* DeclarationAnnotations(const AstNode* declaration)
	{
		if (!declaration) return nullptr;
		switch (declaration->Type())
		{
		case NodeType::FunctionDefinition: return AnnotationsAs<FunctionDefinitionNode>(declaration);
		case NodeType::ObjectDefinition: return AnnotationsAs<ObjectDefinitionNode>(declaration);
		case NodeType::VariableDefinition: return AnnotationsAs<VariableDefinitionNode>(declaration);
		case NodeType::Parameter: return AnnotationsAs<ParameterNode>(declaration);
		case NodeType::EnumDefinition: return AnnotationsAs<EnumDefinitionNode>(declaration);
		case NodeType::UnionDefinition: return AnnotationsAs<UnionDefinitionNode>(declaration);
		case NodeType::AliasDefinition: return AnnotationsAs<AliasDefinitionNode>(declaration);
		default: return nullptr;
		}
	}

	QueryResult<Core::Vector<ReflectedAnnotation>> ReflectedAnnotations(const ProgramModel& model,
		const Core::Vector<const AstNode*>& declarations, AnnotationPlace place, const Scope* scope)
	{
		using Result = QueryResult<Core::Vector<ReflectedAnnotation>>;
		Core::Vector<ReflectedAnnotation> out;
		for (const AstNode* declaration : declarations)
			if (const auto* annotations = DeclarationAnnotations(declaration))
				for (const auto& node : *annotations)
					if (node)
						if (auto collected = Collect(model, *node, place, scope, out); !collected.Ok())
							return Result::Fail(collected.error, collected.message, collected.loc);
		return Result::Success(std::move(out));
	}
} // namespace Scripting
