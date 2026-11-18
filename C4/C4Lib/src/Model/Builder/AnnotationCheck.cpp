#include "Model/Builder/AnnotationCheck.h"
#include "Compiler/ErrorTracker.h"
#include "Model/Annotations.h"
#include "Model/ProgramModel.h"
#include "Model/ReflectedEntities.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/QualifiedName.h"
#include "Model/Types/UnionType.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ObjectDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"

namespace Scripting
{
	void AnnotationCheck::CheckNew()
	{
		if (!m_Declared) m_Declared = std::make_unique<DeclaredAnnotations>(m_Model);
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
		for (const AstNode* declaration : declarations)
			if (declaration) CheckNames(*declaration);
		auto folded = ReflectedAnnotations(m_Model, declarations, scope);
		if (!folded.Ok()) Report(folded.loc, folded.message);
	}

	// Each annotation's name, and [annotation] only where it declares one: a struct or class (a template is said where it registers).
	void AnnotationCheck::CheckNames(const AstNode& declaration)
	{
		const Core::Vector<Ref<AnnotationNode>>* annotations = DeclarationAnnotations(&declaration);
		if (!annotations) return;
		const bool object = declaration.Type() == NodeType::ObjectDefinition;
		for (const auto& node : *annotations)
		{
			if (!node) continue;
			for (const auto& group : node->GetBracketGroups())
				for (const auto& item : group.annotations)
				{
					if (!item) continue;
					if (item->GetName() == kAnnotationDeclaration && !object)
						Report(item->startLocation, "[annotation] declares an annotation by the struct or class it marks: put it on one");
					else if (!item->IsCompilerAnnotation()) CheckName(*item);
				}
		}
	}

	void AnnotationCheck::CheckName(const AnnotationItem& item)
	{
		const ObjectType* meant = m_Declared->MisspeltBy(item.GetName().c_str());
		if (!meant) return;
		const auto* written = dynamic_cast<const ObjectDefinitionNode*>(meant->GetDeclNode());
		const Core::String name = written ? written->GetNameToken().GetText() : meant->GetName();
		Report(item.startLocation, "unknown annotation '" + item.GetName() + "': did you mean '" + name + "', which " + QualifiedName(meant) +
			" declares? (an annotation no [annotation] declares is kept for reflection, and nothing that reads '" + name + "' sees it)");
	}

	void AnnotationCheck::Report(const SourceLocation& at, const Core::String& message)
	{
		if (m_Reported.insert({ at.sourceId, at.line, at.column }).second) m_Errors.AddError(at, message);
	}
} // namespace Scripting
