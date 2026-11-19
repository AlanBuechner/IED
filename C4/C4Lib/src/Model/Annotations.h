#pragma once
#include "Model/AnnotationPlaces.h"
#include "Model/DeclaredAnnotations.h"
#include "Model/QueryResult.h"
#include "Parser/Nodes/Declarations/AnnotationNode.h"
#include "Core/Core.h"

namespace Scripting
{
	class ProgramModel;
	struct Scope;

	/// An annotation the compiler does not act on, kept for reflection (D-42): its name as written (`Game::Serialize`) and
	/// its arguments folded where the declaration is written - as the declaration it takes has them (docs/language/12 section
	/// 4.3), or as written when none declares it.
	struct ReflectedAnnotation
	{
		Core::String name; // a declared annotation's qualified name (`Scene::Range`); else as written
		Core::Vector<ReflectedArgument> arguments;
		const AnnotationSignature* declaration = nullptr; // the overload it takes; null when none declares it
	};

	/// The annotations a declaration node carries (function, object, union, enum, variable, parameter, alias);
	/// null for any other node.
	const Core::Vector<Ref<AnnotationNode>>* DeclarationAnnotations(const AstNode* declaration);

	/// What is written as an annotation argument, or as a parameter's default, folded in `scope`: a string literal is a
	/// string, any other expression a constant, else the type `asType` names (the same tokens read as a type, when they do).
	QueryResult<ReflectedArgument> FoldAnnotationArgument(const ProgramModel& model, const AstNode& written, const QualTypeNode* asType,
		const Scope* scope);

	/// The annotation `item` (not a compiler annotation), written on `place` and folded in `scope`, matched to the declaration
	/// it takes when one declares it. A failure names the argument that does not fold or fit, or why no declaration takes it.
	QueryResult<ReflectedAnnotation> FoldAnnotation(const ProgramModel& model, const AnnotationItem& item, AnnotationPlace place,
		const Scope* scope);

	/// The reflected annotations of `declarations`, in order (a function declared and defined apart carries both lists),
	/// written on `place` and folded in `scope`. A failure is the first annotation's that fails.
	QueryResult<Core::Vector<ReflectedAnnotation>> ReflectedAnnotations(const ProgramModel& model,
		const Core::Vector<const AstNode*>& declarations, AnnotationPlace place, const Scope* scope);
} // namespace Scripting
