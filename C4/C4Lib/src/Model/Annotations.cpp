#include "Model/Annotations.h"
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
			const auto* typed = dynamic_cast<const Node*>(node);
			return typed ? &typed->GetAnnotations() : nullptr;
		}

		bool Reflectable(ConstantValue::Kind kind)
		{
			return kind == ConstantValue::Kind::Int64 || kind == ConstantValue::Kind::Float64 || kind == ConstantValue::Kind::Bool;
		}

		QueryResult<ConstantValue> FoldArgument(const ProgramModel& model, const AstNode& argument, const Scope* scope)
		{
			using Result = QueryResult<ConstantValue>;
			if (const auto* literal = dynamic_cast<const LiteralNode*>(&argument); literal && literal->GetKind() == LiteralNode::LiteralKind::String)
			{
				if (literal->GetUnitWidth() != 1)
					return Result::Fail(QueryError::NotAValue, "an annotation string argument is a UTF-8 literal", argument.startLocation);
				return Result::Success(ConstantValue::String(literal->GetValue()));
			}
			auto value = ConstEvaluator(model, scope).Evaluate(&argument, argument.startLocation.sourceId);
			if (value.Ok() && Reflectable(value.value.kind)) return value;
			Core::String why = "an annotation argument must be a constant number, bool or string";
			if (!value.Ok() && !value.message.empty()) why += ": " + value.message;
			return Result::Fail(QueryError::NotAValue, why, argument.startLocation);
		}

		QueryResult<void> Collect(const ProgramModel& model, const AnnotationNode& node, const Scope* scope, Core::Vector<ReflectedAnnotation>& out)
		{
			for (const auto& group : node.GetBracketGroups())
				for (const auto& item : group.annotations)
				{
					if (!item || item->IsCompilerAnnotation()) continue;
					ReflectedAnnotation annotation;
					annotation.name = item->GetName();
					for (const auto& argument : item->GetArguments())
					{
						auto value = FoldArgument(model, *argument, scope);
						if (!value.Ok()) return QueryResult<void>::Fail(value.error, value.message, value.loc);
						annotation.arguments.Push(value.value);
					}
					out.Push(std::move(annotation));
				}
			return QueryResult<void>::Success();
		}
	}

	const Core::Vector<Ref<AnnotationNode>>* DeclarationAnnotations(const AstNode* declaration)
	{
		if (const auto* found = AnnotationsAs<FunctionDefinitionNode>(declaration)) return found;
		if (const auto* found = AnnotationsAs<ObjectDefinitionNode>(declaration)) return found;
		if (const auto* found = AnnotationsAs<VariableDefinitionNode>(declaration)) return found;
		if (const auto* found = AnnotationsAs<ParameterNode>(declaration)) return found;
		if (const auto* found = AnnotationsAs<EnumDefinitionNode>(declaration)) return found;
		if (const auto* found = AnnotationsAs<UnionDefinitionNode>(declaration)) return found;
		return AnnotationsAs<AliasDefinitionNode>(declaration);
	}

	QueryResult<Core::Vector<ReflectedAnnotation>> ReflectedAnnotations(const ProgramModel& model,
		const Core::Vector<const AstNode*>& declarations, const Scope* scope)
	{
		using Result = QueryResult<Core::Vector<ReflectedAnnotation>>;
		Core::Vector<ReflectedAnnotation> out;
		for (const AstNode* declaration : declarations)
			if (const auto* annotations = DeclarationAnnotations(declaration))
				for (const auto& node : *annotations)
					if (node)
						if (auto collected = Collect(model, *node, scope, out); !collected.Ok())
							return Result::Fail(collected.error, collected.message, collected.loc);
		return Result::Success(std::move(out));
	}
} // namespace Scripting
