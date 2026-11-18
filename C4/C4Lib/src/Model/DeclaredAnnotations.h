#pragma once
#include <string>
#include <unordered_map>

namespace Scripting
{
	class ObjectType;
	class ProgramModel;
	struct AstNode;

	/// Whether the declaration carries [annotation] (docs/language/12 section 4).
	bool DeclaresAnnotation(const AstNode* declaration);

	/// The annotations a program declares: each struct or class marked [annotation], by its own name - `[annotation] struct Requires {}`
	/// declares `[Requires]`, whatever namespace it is written in.
	class DeclaredAnnotations
	{
	public:
		explicit DeclaredAnnotations(const ProgramModel& model);

		/// The type declaring the annotation `name` (`Requires`, or `Scene::Requires` by its last part); null when none does.
		const ObjectType* Find(const std::string& name) const;
		/// The type declaring the annotation `name` is a typo of: none is named so, and one is a near miss of it (the closest);
		/// null otherwise.
		const ObjectType* MisspeltBy(const std::string& name) const;

	private:
		static std::string OwnName(const std::string& name);

		std::unordered_map<std::string, const ObjectType*> m_Declaring;
	};
} // namespace Scripting
