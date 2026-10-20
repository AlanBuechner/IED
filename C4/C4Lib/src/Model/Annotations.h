#pragma once
#include "Model/ConstantValue.h"
#include "Model/QueryResult.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Core/Core.h"

namespace Scripting
{
	class ProgramModel;
	class Scope;

	/// An annotation the compiler does not act on, kept for reflection (D-42): its name as written
	/// (`Game::Serialize`) and its arguments folded where the declaration is written.
	struct ReflectedAnnotation
	{
		Core::String name;
		Core::Vector<ConstantValue> arguments;
	};

	/// The annotations a declaration node carries (function, object, union, enum, variable, parameter, alias);
	/// null for any other node.
	const Core::Vector<Ref<AnnotationNode>>* DeclarationAnnotations(const AstNode* declaration);

	/// The reflected annotations of `declarations`, in order (a function declared and defined apart carries
	/// both lists), folded in `scope`: a string literal is a string, any other argument a constant expression.
	/// A failure names the first argument that is not constant, at its location.
	QueryResult<Core::Vector<ReflectedAnnotation>> ReflectedAnnotations(const ProgramModel& model,
		const Core::Vector<const AstNode*>& declarations, const Scope* scope);
} // namespace Scripting
