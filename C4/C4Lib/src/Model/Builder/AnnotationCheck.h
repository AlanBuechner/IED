#pragma once
#include "Core/Core.h"
#include "Compiler/ErrorTracker.h"
#include "Model/AnnotationPlaces.h"
#include <set>
#include <tuple>
#include <unordered_set>

namespace C4Compiler
{
	class ObjectType;
	class ProgramModel;
	struct AccessSection;
	struct AnnotationContext;
	struct AnnotationItem;
	struct AstNode;
	struct FunctionRecord;
	struct SourceLocation;
	struct Scope;

	/// The annotations reflection registers, checked while the model is built (D-42, D-87), each an error where it is written -
	/// in an editor too: an argument that is neither a constant nor a type, a use no declaration of its annotation takes (the
	/// wrong place, count or kind of arguments), a misspelt declared annotation, and a declaration that declares nothing as
	/// written; an access label's annotation that no field or method under it takes. An undeclared annotation is a warning. A template instance an argument names is made before any body is
	/// lowered. Each CheckNew checks what earlier ones did not: the instances made since.
	class AnnotationCheck
	{
	public:
		AnnotationCheck(const ProgramModel& model, ErrorTracker& errors) : m_Model(model), m_Errors(errors) {}

		void CheckNew();

	private:
		void CheckDeclarations();
		void CheckTypes();
		void CheckGlobals();
		void CheckFunctions();
		void CheckObject(const ObjectType& object);
		void CheckSection(const AccessSection& section, const Scope* scope);
		void CheckSectionItem(const AnnotationItem& item, const AccessSection& section, const Scope* scope);
		void CheckFunction(const FunctionRecord& function);
		void Check(const Core::Vector<const AstNode*>& declarations, AnnotationPlace place, const Scope* scope);
		void Check(const AstNode* declaration, AnnotationPlace place, const Scope* scope);
		void CheckItem(const AnnotationItem& item, AnnotationPlace place, const Scope* scope);
		void CheckUndeclared(const AnnotationItem& item, const AnnotationContext& at);
		void Report(const SourceLocation& at, const Core::String& message, ErrorSeverity severity = ErrorSeverity::Error);
		bool IsNew(const void* entity) { return m_Checked.insert(entity).second; }

		const ProgramModel& m_Model;
		ErrorTracker& m_Errors;
		bool m_DeclarationsChecked = false;
		std::unordered_set<const void*> m_Checked;               // the types, globals and functions checked
		std::set<std::tuple<uint32, uint32, uint32>> m_Reported; // diagnostics said, by location: a template's once for all instances
	};
} // namespace C4Compiler
