#pragma once
#include "Core/Core.h"
#include "Model/DeclaredAnnotations.h"
#include <memory>
#include <set>
#include <tuple>
#include <unordered_set>

namespace Scripting
{
	class ErrorTracker;
	class ObjectType;
	class ProgramModel;
	struct AnnotationItem;
	struct AstNode;
	struct FunctionRecord;
	struct SourceLocation;
	struct Scope;

	/// The annotations reflection registers, checked while the model is built (D-42), each an error where it is written - in an
	/// editor too: an argument that is neither a constant nor a type, a misspelt declared annotation (`[Requiers]` where
	/// `[annotation] struct Requires` is declared), and [annotation] on anything but a struct or class. A template instance an
	/// argument names is made before any body is lowered. Each CheckNew checks what earlier ones did not: the instances made since.
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
		void CheckNames(const AstNode& declaration);
		void CheckName(const AnnotationItem& item);
		void Report(const SourceLocation& at, const Core::String& message);
		bool IsNew(const void* entity) { return m_Checked.insert(entity).second; }

		const ProgramModel& m_Model;
		ErrorTracker& m_Errors;
		std::unique_ptr<DeclaredAnnotations> m_Declared;          // made at the first CheckNew: every declaring type is there by then
		std::unordered_set<const void*> m_Checked;               // the types, globals and functions checked
		std::set<std::tuple<uint32, uint32, uint32>> m_Reported; // failures said, by location: a template's once for all instances
	};
} // namespace Scripting
