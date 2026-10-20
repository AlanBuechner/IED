#include "AstNode.h"

namespace Scripting
{
	Core::String NodeTypeToName(NodeType type)
	{
		switch (type)
		{
		case NodeType::IncludeDeclaration:       return "IncludeDeclaration";
		case NodeType::ExternDeclaration:        return "ExternDeclaration";
		case NodeType::Namespace:                return "Namespace";
		case NodeType::LiteralExpression:        return "LiteralExpression";
		case NodeType::IdentifierExpression:     return "IdentifierExpression";
		case NodeType::BinaryExpression:         return "BinaryExpression";
		case NodeType::UnaryExpression:          return "UnaryExpression";
		case NodeType::TernaryExpression:        return "TernaryExpression";
		case NodeType::FunctionCall:             return "FunctionCall";
		case NodeType::MemberAccess:             return "MemberAccess";
		case NodeType::StaticAccess:             return "StaticAccess";
		case NodeType::ArrayIndex:               return "ArrayIndex";
		case NodeType::AssignmentExpression:     return "AssignmentExpression";
		case NodeType::ConditionalAnd:           return "ConditionalAnd";
		case NodeType::ConditionalOr:            return "ConditionalOr";
		case NodeType::TypeCheck:                return "TypeCheck";
		case NodeType::SafeCast:                 return "SafeCast";
		case NodeType::CastExpression:           return "CastExpression";
		case NodeType::TypeOfExpression:         return "TypeOfExpression";
		case NodeType::VaArgExpression:          return "VaArgExpression";
		case NodeType::ParenthesizedExpression:  return "ParenthesizedExpression";
		case NodeType::Annotation:               return "Annotation";
		case NodeType::AnnotationItem:           return "AnnotationItem";
		case NodeType::NamespaceQualifiedName:   return "NamespaceQualifiedName";
		case NodeType::TemplateArgumentList:     return "TemplateArgumentList";
		case NodeType::TemplateParameter:        return "TemplateParameter";
		case NodeType::TemplateDefinition:       return "TemplateDefinition";
		case NodeType::QualType:                 return "QualType";
		case NodeType::Parameter:                return "Parameter";
		case NodeType::CodeBlock:                return "CodeBlock";
		case NodeType::ExpressionStatement:      return "ExpressionStatement";
		case NodeType::EmptyStatement:           return "EmptyStatement";
		case NodeType::VariableDefinition:       return "VariableDefinition";
		case NodeType::FunctionDefinition:       return "FunctionDefinition";
		case NodeType::ObjectDefinition:         return "ObjectDefinition";
		case NodeType::EnumDefinition:           return "EnumDefinition";
		case NodeType::EnumMember:               return "EnumMember";
		case NodeType::UnionDefinition:          return "UnionDefinition";
		case NodeType::AliasDefinition:          return "AliasDefinition";
		case NodeType::FunPtrType:               return "FunPtrType";
		case NodeType::IfStatement:              return "IfStatement";
		case NodeType::WhileLoop:                return "WhileLoop";
		case NodeType::ForLoop:                  return "ForLoop";
		case NodeType::DoWhileLoop:              return "DoWhileLoop";
		case NodeType::SwitchStatement:          return "SwitchStatement";
		case NodeType::SwitchCase:               return "SwitchCase";
		case NodeType::TryCatch:                 return "TryCatch";
		case NodeType::CatchClause:              return "CatchClause";
		case NodeType::ReturnStatement:          return "ReturnStatement";
		case NodeType::BreakStatement:           return "BreakStatement";
		case NodeType::ContinueStatement:        return "ContinueStatement";
		case NodeType::DeferStatement:           return "DeferStatement";
		case NodeType::AsmStatement:             return "AsmStatement";
		case NodeType::FallthroughStatement:     return "FallthroughStatement";
		case NodeType::GotoStatement:            return "GotoStatement";
		case NodeType::LabelStatement:           return "LabelStatement";
		case NodeType::ThrowStatement:           return "ThrowStatement";
		case NodeType::BracedInitializer:        return "BracedInitializer";
		case NodeType::NewExpression:            return "NewExpression";
		case NodeType::SwizzleView:              return "SwizzleView";
		case NodeType::LambdaExpression:         return "LambdaExpression";
		case NodeType::PackExpansion:            return "PackExpansion";
		case NodeType::FoldExpression:           return "FoldExpression";
		case NodeType::ValueList:                return "ValueList";
		case NodeType::Destructuring:            return "Destructuring";
		case NodeType::Unknown:                  return "Unknown";
		}
		return "Unknown";
	}
} // namespace Scripting
