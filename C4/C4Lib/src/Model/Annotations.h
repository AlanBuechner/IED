#pragma once
#include "Model/ConstantValue.h"
#include "Model/QueryResult.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Core/Core.h"

namespace Scripting
{
	class ProgramModel;
	class Scope;
	class Type;

	/// One argument of a reflected annotation: a constant folded where the declaration is written, or the type it names
	/// (`[Requires(Render::MeshRenderer)]`).
	struct ReflectedArgument
	{
		ConstantValue value;        // the constant; None for a type
		const Type* type = nullptr; // the type the argument names; null for a constant
	};

	/// An annotation the compiler does not act on, kept for reflection (D-42): its name as written
	/// (`Game::Serialize`) and its arguments folded where the declaration is written.
	struct ReflectedAnnotation
	{
		Core::String name;
		Core::Vector<ReflectedArgument> arguments;
	};

	/// The annotations a declaration node carries (function, object, union, enum, variable, parameter, alias);
	/// null for any other node.
	const Core::Vector<Ref<AnnotationNode>>* DeclarationAnnotations(const AstNode* declaration);

	/// The reflected annotations of `declarations`, in order (a function declared and defined apart carries
	/// both lists), folded in `scope`: a string literal is a string, any other argument a constant expression, else the
	/// type it names. A failure names the first argument that is neither, at its location.
	QueryResult<Core::Vector<ReflectedAnnotation>> ReflectedAnnotations(const ProgramModel& model,
		const Core::Vector<const AstNode*>& declarations, const Scope* scope);
} // namespace Scripting
