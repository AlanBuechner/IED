#include "Model/Builder/AnnotationCheck.h"
#include "Compiler/ErrorTracker.h"
#include "Model/Annotations.h"
#include "Model/ProgramModel.h"
#include "Model/ReflectedEntities.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/UnionType.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"

namespace Scripting
{
	void AnnotationCheck::CheckNew()
	{
		CheckTypes();
		CheckGlobals();
		CheckFunctions();
	}

	// By index: folding an annotation can make a template instance, which joins the list.
	void AnnotationCheck::CheckTypes()
	{
		const Core::Vector<const ObjectType*>& objects = m_Model.ObjectTypes();
		for (uint32 i = 0; i < objects.Count(); ++i)
			if (ReflectedType(m_Model, objects[i]) && IsNew(objects[i])) CheckObject(*objects[i]);
		for (const EnumType* en : m_Model.EnumTypes())
			if (en && !en->GetName().empty() && IsNew(en)) Check(en->GetDeclNode(), en->GetDeclScope());
		for (const UnionType* un : m_Model.UnionTypes())
		{
			if (!un || un->GetName().empty() || !IsNew(un)) continue;
			Check(un->GetDeclNode(), un->GetDeclScope());
			for (const auto& field : un->GetFields())
				if (!field.m_IsStatic) Check(field.m_DeclaringNode, un->GetDeclScope());
		}
	}

	void AnnotationCheck::CheckObject(const ObjectType& object)
	{
		const Scope* scope = m_Model.TypeScope(&object);
		Check(object.GetDeclNode(), scope);
		for (const auto& field : object.GetFields())
			if (!field.m_IsStatic) Check(field.m_DeclaringNode, scope);
	}

	// As reflection registers them: not a thread_local, which has no one address.
	void AnnotationCheck::CheckGlobals()
	{
		m_Model.Symbols()->CollectGlobalVariables([this](const SymbolInfo& variable) {
			if (variable.kind != SymbolKind::Variable || !variable.type || variable.isThreadLocal || variable.mangledName.empty()) return;
			if (IsNew(&variable)) Check(variable.declNode, variable.declaringScope);
		});
	}

	void AnnotationCheck::CheckFunctions()
	{
		Core::Vector<const FunctionRecord*> records;
		m_Model.AllFunctionRecords(records);
		for (const FunctionRecord* function : records)
			if (ReflectedFunction(*function) && IsNew(function)) CheckFunction(*function);
	}

	// The function's annotations and each parameter's, from every declaration that writes them.
	void AnnotationCheck::CheckFunction(const FunctionRecord& function)
	{
		Check(function.declarations, function.scope);
		for (const AstNode* declaration : function.declarations)
			if (const auto* written = dynamic_cast<const FunctionDefinitionNode*>(declaration))
				for (const auto& parameter : written->GetParameters())
					Check(parameter.Get(), function.scope);
	}

	void AnnotationCheck::Check(const AstNode* declaration, const Scope* scope)
	{
		if (!declaration) return;
		Core::Vector<const AstNode*> declarations;
		declarations.Push(declaration);
		Check(declarations, scope);
	}

	void AnnotationCheck::Check(const Core::Vector<const AstNode*>& declarations, const Scope* scope)
	{
		auto folded = ReflectedAnnotations(m_Model, declarations, scope);
		if (folded.Ok()) return;
		if (m_Reported.insert({ folded.loc.sourceId, folded.loc.line, folded.loc.column }).second) m_Errors.AddError(folded.loc, folded.message);
	}
} // namespace Scripting
