#pragma once
#include "Core/Core.h"
#include <set>
#include <tuple>
#include <unordered_set>

namespace Scripting
{
	class ErrorTracker;
	class ObjectType;
	class ProgramModel;
	struct AstNode;
	struct FunctionRecord;
	struct Scope;

	/// The annotations reflection registers, folded while the model is built (D-42): an argument that is neither a constant
	/// nor a type is an error where it is written - in an editor too - and a template instance an annotation names is made
	/// before any body is lowered. Each CheckNew checks what earlier ones did not: the template instances made since.
	class AnnotationCheck
	{
	public:
		AnnotationCheck(const ProgramModel& model, ErrorTracker& errors) : m_Model(model), m_Errors(errors) {}

		void CheckNew();

	private:
		void CheckTypes();
		void CheckGlobals();
		void CheckFunctions();
		void CheckObject(const ObjectType& object);
		void CheckFunction(const FunctionRecord& function);
		void Check(const Core::Vector<const AstNode*>& declarations, const Scope* scope);
		void Check(const AstNode* declaration, const Scope* scope);
		bool IsNew(const void* entity) { return m_Checked.insert(entity).second; }

		const ProgramModel& m_Model;
		ErrorTracker& m_Errors;
		std::unordered_set<const void*> m_Checked;               // the types, globals and functions checked
		std::set<std::tuple<uint32, uint32, uint32>> m_Reported; // failures said, by location: a template's once for all instances
	};
} // namespace Scripting
