#include "Model/Builder/AnnotationCheck.h"
#include "Model/Annotations.h"
#include "Model/ProgramModel.h"
#include "Model/ReflectedEntities.h"
#include "Model/Types/EnumType.h"
#include "Model/Types/ObjectType.h"
#include "Model/Types/UnionType.h"
#include "Parser/Nodes/Declarations/AnnotationDeclarationNode.h"
#include "Parser/Nodes/Declarations/FunctionDefinitionNode.h"
#include "Parser/Nodes/Declarations/ParameterNode.h"

namespace Scripting
{
	void AnnotationCheck::CheckNew()
	{
		CheckDeclarations();
		CheckTypes();
		CheckGlobals();
		CheckFunctions();
	}

	void AnnotationCheck::CheckDeclarations()
	{
		if (m_DeclarationsChecked) return;
		m_DeclarationsChecked = true;
		for (const AnnotationDeclarationProblem& problem : m_Model.Annotations().Problems()) Report(problem.at, problem.message);
	}

	// By index: folding an annotation can make a template instance, which joins the list.
	void AnnotationCheck::CheckTypes()
	{
		const Core::Vector<const ObjectType*>& objects = m_Model.ObjectTypes();
		for (uint32 i = 0; i < objects.Count(); ++i)
			if (ReflectedType(m_Model, objects[i]) && IsNew(objects[i])) CheckObject(*objects[i]);
		for (const EnumType* en : m_Model.EnumTypes())
			if (en && !en->GetName().empty() && IsNew(en)) Check(en->GetDeclNode(), AnnotationPlace::Enum, en->GetDeclScope());
		for (const UnionType* un : m_Model.UnionTypes())
		{
			if (!un || un->GetName().empty() || !IsNew(un)) continue;
			Check(un->GetDeclNode(), AnnotationPlace::Union, un->GetDeclScope());
			for (const auto& field : un->GetFields())
				if (!field.m_IsStatic) Check(field.m_DeclaringNode, AnnotationPlace::Field, un->GetDeclScope());
		}
	}

	void AnnotationCheck::CheckObject(const ObjectType& object)
	{
		const Scope* scope = m_Model.TypeScope(&object);
		Check(object.GetDeclNode(), AnnotationPlace::Type, scope);
		for (const auto& field : object.GetFields())
			if (!field.m_IsStatic) Check(field.m_DeclaringNode, AnnotationPlace::Field, scope);
	}

	// As reflection registers them: not a thread_local, which has no one address. A type's static is a field.
	void AnnotationCheck::CheckGlobals()
	{
		m_Model.Symbols()->CollectGlobalVariables([this](const SymbolInfo& variable) {
			if (variable.kind != SymbolKind::Variable || !variable.type || variable.isThreadLocal || variable.mangledName.empty()) return;
			const bool member = variable.declaringScope && variable.declaringScope->GetKind() == Scope::Kind::Class;
			if (IsNew(&variable)) Check(variable.declNode, member ? AnnotationPlace::Field : AnnotationPlace::Variable, variable.declaringScope);
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
		Check(function.declarations, PlaceOfFunction(function), function.scope);
		for (const AstNode* declaration : function.declarations)
			if (const auto* written = dynamic_cast<const FunctionDefinitionNode*>(declaration))
				for (const auto& parameter : written->GetParameters())
					Check(parameter.Get(), AnnotationPlace::Parameter, function.scope);
	}

	void AnnotationCheck::Check(const AstNode* declaration, AnnotationPlace place, const Scope* scope)
	{
		if (!declaration) return;
		Core::Vector<const AstNode*> declarations;
		declarations.Push(declaration);
		Check(declarations, place, scope);
	}

	void AnnotationCheck::Check(const Core::Vector<const AstNode*>& declarations, AnnotationPlace place, const Scope* scope)
	{
		for (const AstNode* declaration : declarations)
		{
			const Core::Vector<Ref<AnnotationNode>>* annotations = DeclarationAnnotations(declaration);
			if (!annotations) continue;
			for (const auto& node : *annotations)
				if (node)
					for (const auto& group : node->GetBracketGroups())
						for (const auto& item : group.annotations)
							if (item) CheckItem(*item, place, scope);
		}
	}

	// [annotation] is the parser's error: an annotation is declared by `annotation Name(...) on places;`.
	void AnnotationCheck::CheckItem(const AnnotationItem& item, AnnotationPlace place, const Scope* scope)
	{
		if (item.IsCompilerAnnotation() || item.GetName() == kAnnotationKeyword) return;
		auto folded = FoldAnnotation(m_Model, item, place, scope);
		if (!folded.Ok()) return Report(folded.loc, folded.message);
		if (!folded.value.declaration) CheckUndeclared(item, AnnotationContext{ scope, item.startLocation.sourceId });
	}

	// A name nothing declares where it is written is a warning; one that means a declared annotation - declared where the
	// use does not see it, or misspelt - is an error.
	void AnnotationCheck::CheckUndeclared(const AnnotationItem& item, const AnnotationContext& at)
	{
		const DeclaredAnnotations& declared = m_Model.Annotations();
		if (const Core::String unseen = declared.Unseen(m_Model, item.GetName().c_str(), at); !unseen.empty())
			return Report(item.startLocation, unseen);
		if (const DeclaredAnnotations::Overloads meant = declared.MisspeltBy(item.GetName().c_str(), at); !meant.Empty())
		{
			const AnnotationSignature& signature = *meant[0];
			return Report(item.startLocation, "unknown annotation '" + item.GetName() + "': did you mean '" + signature.name + "'? (an annotation "
				"nothing declares is kept for reflection, and nothing that reads [" + signature.qualifiedName + "] sees it)");
		}
		if (item.NearCompilerAnnotation()) return; // the parser warned that it is probably a compiler annotation
		Report(item.startLocation, "annotation [" + item.GetName() + "] is not declared: declare it, what it takes and where it goes, with `annotation " +
			item.GetOwnName() + "(...) on ...;` (docs/language/12 section 4) so its uses are checked", ErrorSeverity::Warning);
	}

	void AnnotationCheck::Report(const SourceLocation& at, const Core::String& message, ErrorSeverity severity)
	{
		if (m_Reported.insert({ at.sourceId, at.line, at.column }).second) m_Errors.AddError(at, message, severity);
	}
} // namespace Scripting
